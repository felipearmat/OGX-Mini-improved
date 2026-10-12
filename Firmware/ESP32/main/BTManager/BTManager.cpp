#include "btstack_port_esp32.h"
#include "btstack_run_loop.h"
#include "btstack_stdio_esp32.h"
#include "uni.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <esp_system.h>
#include <esp_timer.h>

#include "sdkconfig.h"
#include "Board/ogxm_log.h"
#include "Board/board_api.h"
#include "Board/ModeIndicator.h"
#include "BTManager/BTManager.h"
#include "BLEServer/BLEServer.h"
#include "Bluepad32/ScanControl.h"
#include "UserSettings/DongleSettings.h"

void BTManager::run_task()
{
    board_api::init_board();
    UserSettings::get_instance().initialize_flash();
    dongle_settings::set(dongle_settings::defaults());  // until the RP2040 sends its own (GET_EXT)

    apply_profiles();

    i2c_driver_.initialize_i2c(
        static_cast<i2c_port_t>(CONFIG_I2C_PORT),
        static_cast<gpio_num_t>(CONFIG_I2C_SDA_PIN),
        static_cast<gpio_num_t>(CONFIG_I2C_SCL_PIN),
        CONFIG_I2C_BAUDRATE
    );

    xTaskCreatePinnedToCore(
        [](void* parameter)
        {
            get_instance().i2c_driver_.run_tasks();
        },
        "i2c",
        2048 * 2,
        nullptr,
        configMAX_PRIORITIES-8,
        nullptr,
        1
    );

    btstack_init();

    uni_platform_set_custom(get_bp32_driver());
    uni_init(0, nullptr);
    /* As on the Pico W path: a lone Joy-Con waits for its other half and merges with it; which
     * half of a pair provides motion comes from the dongle settings. */
    uni_hid_parser_switch_set_joycon_pairing(true);
    uni_hid_parser_switch_set_pair_imu_side(joycon_settings::get().pair_imu_right);

    /* Board LED: the output mode's blink code after boot (Board/ModeIndicator.h), then the search
     * state, as on the Pico W. */
    mode_indicator::begin(UserSettings::get_instance().get_current_driver());
    led_timer_.process = check_led_cb;
    led_timer_.context = nullptr;
    btstack_run_loop_set_timer(&led_timer_, LED_CHECK_TIME_MS);
    btstack_run_loop_add_timer(&led_timer_);

    btstack_timer_source_t driver_update_timer;
    driver_update_timer.process = driver_update_timer_cb;
    driver_update_timer.context = nullptr;
    btstack_run_loop_set_timer(&driver_update_timer, UserSettings::GP_CHECK_DELAY_MS);
    btstack_run_loop_add_timer(&driver_update_timer);

    /* The feedback loop always runs: it also fetches the RP2040's settings and checks idle pads. */
    fb_timer_running_ = true;
    fb_timer_.process = feedback_timer_cb;
    fb_timer_.context = nullptr;
    btstack_run_loop_set_timer(&fb_timer_, FEEDBACK_TIME_MS);
    btstack_run_loop_add_timer(&fb_timer_);

    BLEServer::init_server();

    //Doesn't return
    btstack_run_loop_execute();
}

void BTManager::diag_send(uint8_t kind, uint8_t slot, const void* data, size_t len)
{
    I2CDriver::PacketDiag packet;
    packet.kind = kind;
    packet.slot = slot;
    std::memcpy(packet.data, data, len < sizeof(packet.data) ? len : sizeof(packet.data));
    i2c_driver_.write_packet(Esp32Bridge::SLAVE_ADDR, packet);
}

/* An event line, also logged here; sent in parts (Diagnostics keeps kEventText characters). */
void BTManager::diag_event(const char* fmt, ...)
{
    char text[72];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    OGXM_LOG("BP32: %s\n", text);

    const size_t len = std::strlen(text);
    size_t sent = 0;
    do
    {
        uint8_t part[I2CDriver::PacketDiag::kData]{};
        const size_t n = std::min(len - sent, I2CDriver::PacketDiag::kEventPart);
        std::memcpy(&part[1], text + sent, n);
        sent += n;
        part[0] = sent >= len ? 1 : 0;
        diag_send(I2CDriver::PacketDiag::EVENT, 0, part, sizeof(part));
    } while (sent < len);
}

uint8_t BTManager::slave_addr(uint8_t output)
{
    return I2CDriver::MULTI_SLAVE ? output + Esp32Bridge::SLAVE_ADDR : Esp32Bridge::SLAVE_ADDR;
}

/* Profiles depend on the output mode too (Web App: default mapping; XInput: stock stick feel),
 * as on the Pico W path: applied at boot, on every mode change and when the web app saves one. */
void BTManager::apply_profiles()
{
    const DeviceDriverType driver = UserSettings::get_instance().get_current_driver();
    for (uint8_t i = 0; i < MAX_GAMEPADS; ++i)
    {
        UserProfile profile = UserSettings::get_instance().get_profile_by_index(i);
        outputs_[i].mapper.set_profile(profile, driver);
    }
}

void BTManager::on_settings_saved()
{
    apply_profiles();
}

bool BTManager::is_connected(uint8_t index)
{
    if (index >= outputs_.size())
    {
        return false;
    }
    return outputs_[index].connected.load();
}

bool BTManager::any_connected()
{
    for (auto& output : outputs_)
    {
        if (output.connected.load())
        {
            return true;
        }
    }
    return false;
}

/* LED: the mode blink code first; then fast blink = full search, slow blink = reduced search,
 * solid = no search (every slot in use), as on the Pico W. Several LEDs (several RP2040s): one per
 * output, solid when its pad is connected. */
void BTManager::check_led_cb(btstack_timer_source *ts)
{
    static bool led_state = false;
    uint32_t next_ms = LED_CHECK_TIME_MS;

    /* Whether new controllers are accepted, for the diagnostics (as the Pico W records it). */
    BTManager& bt_manager = get_instance();
    const bool searching = uni_bt_enable_new_connections_is_enabled();
    if (searching != bt_manager.searching_sent_)
    {
        bt_manager.searching_sent_ = searching;
        const uint8_t flag = searching ? 1 : 0;
        bt_manager.diag_send(I2CDriver::PacketDiag::SEARCHING, 0, &flag, 1);
    }

    if constexpr (board_api::NUM_LEDS == 1)
    {
        bool on = false;
        uint32_t duration_ms = 0;
        if (mode_indicator::active() && mode_indicator::next_step(on, duration_ms))
        {
            board_api::set_led(on);
            next_ms = duration_ms;
        }
        else if (get_instance().searching_sent_)
        {
            led_state = !led_state;
            board_api::set_led(led_state);
            next_ms = scan_control::reduced() ? LED_SLOW_BLINK_MS : LED_FAST_BLINK_MS;
        }
        else
        {
            board_api::set_led(true);
        }
    }
    else
    {
        led_state = !led_state;
        for (uint8_t i = 0; i < board_api::NUM_LEDS; ++i)
        {
            board_api::set_led(i, get_instance().is_connected(i) ? true : led_state);
        }
    }

    btstack_run_loop_set_timer(ts, next_ms);
    btstack_run_loop_add_timer(ts);
}

void BTManager::send_driver_type(DeviceDriverType driver_type)
{
    for (uint8_t i = 0; i < (I2CDriver::MULTI_SLAVE ? MAX_GAMEPADS : 1); ++i)
    {
        I2CDriver::PacketIn packet_in = outputs_[i].packet_in;
        packet_in.packet_id = I2CDriver::PacketID::SET_DRIVER;
        packet_in.index = i;
        packet_in.device_driver = driver_type;
        i2c_driver_.write_packet(slave_addr(i), packet_in);
    }
}

/* The output's pad at rest: the RP2040 holds the last packet it got. */
void BTManager::send_neutral(uint8_t output)
{
    I2CDriver::PacketInExt ext;
    ext.index = output;
    outputs_[output].ext = ext;
    i2c_driver_.write_packet(slave_addr(output), ext);

    I2CDriver::PacketIn packet_in;
    packet_in.packet_id = I2CDriver::PacketID::SET_PAD;
    packet_in.index = output;
    outputs_[output].packet_in = packet_in;
    i2c_driver_.write_packet(slave_addr(output), packet_in);
}

uni_hid_device_t* BTManager::get_connected_bp32_device(uint8_t slot)
{
    uni_hid_device_t* bp_device = nullptr;
    if (!(bp_device = uni_hid_device_get_instance_for_idx(slot)) ||
        !uni_bt_conn_is_connected(&bp_device->conn))
    {
        return nullptr;
    }
    return bp_device;
}

/* A lone Joy-Con (no D-pad): its stick at the edge stands in for the D-pad in the combos. */
bool BTManager::lone_joycon()
{
    for (uint8_t i = 0; i < slots_.size(); ++i)
    {
        uni_hid_device_t* d = slots_[i].connected ? get_connected_bp32_device(i) : nullptr;
        if (d && bt_pad::is_switch_joycon(d) && bt_pad::pair_partner_idx(d) < 0)
        {
            return true;
        }
    }
    return false;
}

void BTManager::driver_update_timer_cb(btstack_timer_source *ts)
{
    BTManager& bt_manager = get_instance();
    UserSettings& settings = UserSettings::get_instance();
    const I2CDriver::PacketIn& packet_in = bt_manager.outputs_.front().packet_in;

    if (bt_manager.outputs_.front().connected.load())
    {
        const auto result = bt_manager.combo_check_.update(
            packet_in.buttons, packet_in.dpad, bt_manager.lone_joycon(), packet_in.joystick_lx,
            packet_in.joystick_ly, settings.get_current_driver(), true,
            [](DeviceDriverType driver) { return UserSettings::get_instance().is_valid_driver(driver); },
            dongle_settings::get());
        if (result.stop_search)
        {
            bt_manager.diag_event("search for new controllers stopped from the controller");
            scan_control::request_stop();
            bt_manager.stop_search_feedback();
            bt_manager.apply_scan(false, false);
        }
        else if (result.new_driver != DeviceDriverType::NONE)
        {
            bt_manager.diag_event("mode changed by the button combo");
            settings.store_driver_type(result.new_driver);
            bt_manager.apply_profiles();
            mode_indicator::begin(result.new_driver);
            for (uint8_t i = 0; i < bt_manager.slots_.size(); ++i)
            {
                uni_hid_device_t* d = bt_manager.slots_[i].connected ? get_connected_bp32_device(i) : nullptr;
                if (d)
                {
                    bt_manager.schedule_mode_lightbar(d, i);
                }
            }
        }
    }

    //Notify pico of current driver type regardless of change
    bt_manager.send_driver_type(settings.get_current_driver());

    btstack_run_loop_set_timer(ts, UserSettings::GP_CHECK_DELAY_MS);
    btstack_run_loop_add_timer(ts);
}

/* Start + L3 confirmed with one short rumble, as on the Pico W. */
void BTManager::stop_search_feedback()
{
    for (uint8_t i = 0; i < slots_.size(); ++i)
    {
        uni_hid_device_t* d = slots_[i].connected ? get_connected_bp32_device(i) : nullptr;
        if (d)
        {
            bt_pad::play_host_rumble(d, STOP_SEARCH_RUMBLE_MS, 0xFF, 0xFF, joycon_settings::get().pair_rumble_per_side);
        }
    }
}

/* Settings from the RP2040 (GET_EXT): dongle options, host lightbar, motion use. */
void BTManager::apply_ext(const I2CDriver::PacketOutExt& ext)
{
    motion_output_ = (ext.flags & I2CDriver::PacketOutExt::MOTION_OUTPUT) != 0;
    if (ext.index < outputs_.size())
    {
        Lightbar& host = outputs_[ext.index].host_lightbar;
        host.valid = (ext.flags & I2CDriver::PacketOutExt::LIGHTBAR_VALID) != 0;
        host.r = ext.lightbar_r;
        host.g = ext.lightbar_g;
        host.b = ext.lightbar_b;
    }
    dongle_settings::Settings settings;
    if (dongle_settings::decode(reinterpret_cast<const uint8_t*>(&ext.settings), sizeof(ext.settings), settings))
    {
        const bool imu_side_changed = settings.joycon_pair_imu_right != dongle_settings::get().joycon_pair_imu_right;
        dongle_settings::set(settings);
        if (imu_side_changed)
        {
            uni_hid_parser_switch_set_pair_imu_side(joycon_settings::get().pair_imu_right);
        }
    }
}

void BTManager::ext_feedback_cb(void* context)
{
    BTManager& bt_manager = get_instance();
    I2CDriver::PacketOutExt ext;
    {
        std::lock_guard<std::mutex> lock(bt_manager.ext_mutex_);
        ext = bt_manager.ext_rx_;
    }
    bt_manager.apply_ext(ext);
}

/* Rumble, keepalives and the host's lightbar for the pads of one output. */
void BTManager::output_feedback_cb(void* context)
{
    BTManager& bt_manager = get_instance();
    const uint8_t output = reinterpret_cast<FBContext*>(context)->index;
    const I2CDriver::PacketOut packet_out = bt_manager.outputs_[output].packet_out.load();
    const bool rumbling = packet_out.rumble_l || packet_out.rumble_r;
    const uint32_t now_ms = btstack_run_loop_get_time_ms();

    if (rumbling)
    {
        bt_manager.outputs_[output].idle.note_rumble(now_ms);  // the idle turn-off ignores the shaking
    }
    for (uint8_t i = 0; i < bt_manager.slots_.size(); ++i)
    {
        Slot& slot = bt_manager.slots_[i];
        uni_hid_device_t* d = slot.connected && slot.output == output ? get_connected_bp32_device(i) : nullptr;
        if (!d)
        {
            continue;
        }
        bt_pad::ble_keepalive(d, rumbling, now_ms, slot.keepalive);
        /* Early output reports can drop a freshly connected DualShock 4: no rumble or lightbar yet. */
        if (now_ms < slot.host_rumble_ok_ms)
        {
            continue;
        }
        if (rumbling)
        {
            bt_pad::play_host_rumble(d, RUMBLE_DURATION_MS, packet_out.rumble_l, packet_out.rumble_r,
                                     joycon_settings::get().pair_rumble_per_side);
        }
        bt_pad::set_lightbar(d, bt_manager.outputs_[output].host_lightbar, slot.lightbar);
    }
}

void BTManager::feedback_timer_cb(btstack_timer_source *ts)
{
    static FBContext fb_contexts[MAX_GAMEPADS];
    BTManager& bt_manager = get_instance();
    const uint32_t now_ms = btstack_run_loop_get_time_ms();

    for (uint8_t i = 0; i < MAX_GAMEPADS; ++i)
    {
        if (i == 0 || bt_manager.outputs_[i].connected.load())
        {
            /* Settings and the host's lightbar from the RP2040, then the BTstack run loop. */
            FBContext& ext_context = bt_manager.ext_context_;
            ext_context.index = i;
            ext_context.cb_reg.callback = ext_feedback_cb;
            ext_context.cb_reg.context = &ext_context;
            bt_manager.i2c_driver_.read_ext(slave_addr(i), i,
                [&bt_manager, &ext_context](const I2CDriver::PacketOutExt& ext)
                {
                    {
                        std::lock_guard<std::mutex> lock(bt_manager.ext_mutex_);
                        bt_manager.ext_rx_ = ext;
                    }
                    btstack_run_loop_execute_on_main_thread(&ext_context.cb_reg);
                });
        }
        if (!bt_manager.outputs_[i].connected.load())
        {
            continue;
        }

        FBContext& fb_context = fb_contexts[i];
        fb_context.index = i;
        fb_context.cb_reg.callback = output_feedback_cb;
        fb_context.cb_reg.context = reinterpret_cast<void*>(&fb_context);

        //Register a read on i2c thread, with callback to send feedback on btstack thread
        bt_manager.i2c_driver_.read_packet(slave_addr(i),
            [&bt_manager, &fb_context](const I2CDriver::PacketOut& packet_out)
            {
                bt_manager.outputs_[fb_context.index].packet_out.store(packet_out);
                btstack_run_loop_execute_on_main_thread(&fb_context.cb_reg);
            });
    }

    /* Idle turn-off (Bluepad32/IdleOff.h) and the search policy, as on the Pico W. */
    const uint8_t minutes = dongle_settings::get().idle_off_minutes;
    for (uint8_t out = 0; out < MAX_GAMEPADS; ++out)
    {
        if (!bt_manager.outputs_[out].idle.due(now_ms, minutes))
        {
            continue;
        }
        for (uint8_t i = 0; i < bt_manager.slots_.size(); ++i)
        {
            uni_hid_device_t* d = bt_manager.slots_[i].connected && bt_manager.slots_[i].output == out
                                      ? get_connected_bp32_device(i) : nullptr;
            if (d)
            {
                bt_manager.diag_event("slot %d: no input for %u min, turning it off", i, minutes);
                bt_pad::turn_off(d);
            }
        }
    }
    bt_manager.apply_scan(false, false);  // also catches a Joy-Con pair completed after device_ready

    btstack_run_loop_set_timer(ts, FEEDBACK_TIME_MS);
    btstack_run_loop_add_timer(ts);
}

void BTManager::scan_event(uint32_t now_ms, const char* text)
{
    get_instance().diag_event("%s", text);
}

void BTManager::apply_scan(bool pad_leaving, bool force)
{
    bool used[MAX_GAMEPADS]{};
    scan_control::Pads pads;
    for (uint8_t i = 0; i < slots_.size(); ++i)
    {
        uni_hid_device_t* d = slots_[i].connected ? get_connected_bp32_device(i) : nullptr;
        if (!d)
        {
            continue;
        }
        if (d->hids_cid != 0 && d->hids_cid != 0xffff)
        {
            pads.hogp = true;
        }
        else if (gap_get_connection_type(d->conn.handle) == GAP_CONNECTION_ACL)
        {
            pads.classic = true;
        }
        const int out = slots_[i].output;
        if (out >= 0 && out < MAX_GAMEPADS && !used[out])
        {
            used[out] = true;
            ++pads.outputs;
        }
    }
    pads.awaiting = uni_hid_parser_switch_any_awaiting_partner();
    scan_control::apply(pads, MAX_GAMEPADS, pad_leaving, force, btstack_run_loop_get_time_ms(),
                        dongle_settings::get(), true, scan_event);
}

/* The mode colour on a DS4 / DualSense lightbar, a while after it connects or the mode changes. */
void BTManager::schedule_mode_lightbar(uni_hid_device_t* device, int slot)
{
    if (!bt_pad::has_lightbar(device))
    {
        return;
    }
    btstack_timer_source_t* ts = &slots_[slot].mode_lightbar_timer;
    btstack_run_loop_remove_timer(ts);
    ts->process = mode_lightbar_cb;
    ts->context = reinterpret_cast<void*>(static_cast<intptr_t>(slot));
    btstack_run_loop_set_timer(ts, bt_pad::kModeLightbarDelayMs);
    btstack_run_loop_add_timer(ts);
}

void BTManager::mode_lightbar_cb(btstack_timer_source *ts)
{
    BTManager& bt_manager = get_instance();
    const int slot = static_cast<int>(reinterpret_cast<intptr_t>(ts->context));
    uni_hid_device_t* d = get_connected_bp32_device(static_cast<uint8_t>(slot));
    if (!d || !bt_manager.slots_[slot].connected)
    {
        return;
    }
    Lightbar mode;
    mode_indicator::lightbar_color(UserSettings::get_instance().get_current_driver(), mode.r, mode.g, mode.b);
    mode.valid = true;
    bt_manager.slots_[slot].lightbar = Lightbar{};  // the mode colour always goes out once
    bt_pad::set_lightbar(d, mode, bt_manager.slots_[slot].lightbar);
}

I2CDriver::PacketIn BTManager::get_packet_in(uint8_t index)
{
    if (index >= outputs_.size())
    {
        return I2CDriver::PacketIn();
    }
    return outputs_[index].packet_in;
}

/* The combo is seen while Bluepad32 parses the pad's own report: disconnecting right there tore
 * the link down under the parser on the Pico W path, so it runs from a run-loop timer. */
void BTManager::schedule_disconnect_combo(uint8_t slot)
{
    if (disconnect_combo_idx_ >= 0)
    {
        return;  // already pending
    }
    disconnect_combo_idx_ = slot;
    disconnect_combo_timer_.process = disconnect_combo_timer_cb;
    disconnect_combo_timer_.context = nullptr;
    btstack_run_loop_set_timer(&disconnect_combo_timer_, 0);
    btstack_run_loop_add_timer(&disconnect_combo_timer_);
}

/* Then the ESP32 restarts (Bluepad32/BtPad.h). The RP2040 keeps its USB mode and gets a neutral
 * pad first. */
void BTManager::disconnect_combo_timer_cb(btstack_timer_source *ts)
{
    BTManager& bt_manager = get_instance();
    const int slot = bt_manager.disconnect_combo_idx_;
    bt_manager.disconnect_combo_idx_ = -1;
    if (slot < 0 || slot >= static_cast<int>(bt_manager.slots_.size()))
    {
        return;
    }
    uni_hid_device_t* bp_device = uni_hid_device_get_instance_for_idx(slot);
    if (!bp_device || bp_device->conn.handle == UNI_BT_CONN_HANDLE_INVALID)
    {
        return;
    }

    const int output = bt_manager.slots_[slot].output;
    if (output >= 0 && output < MAX_GAMEPADS)
    {
        bt_manager.send_neutral(static_cast<uint8_t>(output));
    }

    bt_manager.diag_event("slot %d: disconnect combo, restart in %lu ms", slot,
                          static_cast<unsigned long>(bt_pad::kComboRebootDelayMs));
    restart_after_ms(bt_pad::kComboRebootDelayMs);  // armed before touching the pad
    bt_pad::disconnect_with_partner(bp_device);
}

/* Restart from an esp_timer, not the BTstack run loop: it happens even if the BT stack stalls
 * while the pads go away (the Pico W path arms its hardware watchdog for the same reason). */
void BTManager::restart_after_ms(uint32_t delay_ms)
{
    static esp_timer_handle_t restart_timer = nullptr;
    if (restart_timer == nullptr)
    {
        const esp_timer_create_args_t args = {
            .callback = [](void*) { esp_restart(); },
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "bt_restart",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&args, &restart_timer) != ESP_OK)
        {
            restart_timer = nullptr;
            return;
        }
    }
    if (!esp_timer_is_active(restart_timer))
    {
        esp_timer_start_once(restart_timer, static_cast<uint64_t>(delay_ms) * 1000u);
    }
}
