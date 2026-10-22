#include "btstack_port_esp32.h"
#include "btstack_run_loop.h"
#include "btstack_stdio_esp32.h"
#include "uni.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <esp_bt.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "sdkconfig.h"
#include "Board/ogxm_log.h"
#include "Board/board_api.h"
#include "Board/ModeIndicator.h"
#include "Board/StatusLed.h"
#include "BTManager/BTManager.h"
#include "Bluepad32/BLEServer.h"
#include "Bluepad32/ScanControl.h"
#include "UserSettings/DongleSettings.h"
#include "Bluepad32/LinkDiag.h"
#include "Diagnostics/Diagnostics.h"
#include "Diagnostics/DiagForward.h"
#include "I2C/ModeSync.h"

void BTManager::run_task()
{
    board_api::init_board();
    UserSettings::get_instance().initialize_flash();
    dongle_settings::set(dongle_settings::defaults());  // until the RP2040 sends its own (GET_EXT)

    apply_profiles();
    diag_forward::set_sender([](const I2CDriver::PacketDiag& packet)
        {
            get_instance().i2c_driver_.write_packet(Esp32Bridge::SLAVE_ADDR, packet);
        });
    UserSettings::get_instance().set_remote_store(
        [this](const UserProfile& profile, uint8_t index, DeviceDriverType driver)
        {
            i2c_driver_.write_profile(slave_addr(index), profile, index, driver);
        });

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
    link_diag::init();  // link details for the diagnostics (Bluepad32/LinkDiag, shared)
    link_diag::set_poll_latency_us(5000);  // each Classic pad polled at least every 5 ms
    uni_hid_parser_switch_set_pair_imu_side(joycon_settings::get().pair_imu_right);

    /* Board LED: the output mode's blink code after boot (Board/ModeIndicator.h), then the search
     * state, as on the Pico W. */
    begin_mode_indicator(UserSettings::get_instance().get_current_driver());
    led_timer_.process = check_led_cb;
    led_timer_.context = nullptr;
    btstack_run_loop_set_timer(&led_timer_, LED_CHECK_TIME_MS);
    btstack_run_loop_add_timer(&led_timer_);

    btstack_timer_source_t driver_update_timer;
    boot_ms_ = btstack_run_loop_get_time_ms();
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

/* An event line for the diagnostics report (Diagnostics/DiagForward: also logged here). */
void BTManager::diag_event(const char* fmt, ...)
{
    char text[diag::kEventText];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    diag::event(btstack_run_loop_get_time_ms(), "%s", text);
}

uint8_t BTManager::slave_addr(uint8_t output)
{
    return I2CDriver::MULTI_SLAVE ? output + Esp32Bridge::SLAVE_ADDR : Esp32Bridge::SLAVE_ADDR;
}

/* Profiles depend on the output mode too (XInput: stock stick feel; D-Input/Debug with the web app
 * open: the default mapping, so the web app shows the controller's own buttons), as on the Pico W
 * path: applied at boot, on every mode change, when the web app saves one and when it opens or
 * closes. */
void BTManager::apply_profiles()
{
    DeviceDriverType driver = UserSettings::get_instance().get_current_driver();
    if (web_app_open_ && driver == DeviceDriverType::DINPUT_DEBUG)
    {
        driver = DeviceDriverType::WEBAPP;  // the default mapping (Gamepad/GamepadMapper.h)
    }
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

size_t BTManager::kbm_settings(uint8_t* out, size_t len)
{
    std::lock_guard<std::mutex> lock(kbm_mutex_);
    const size_t n = std::min(len, kbm_rx_len_);
    std::memcpy(out, kbm_rx_, n);
    return n;
}

void BTManager::send_settings_blob(uint8_t kind, const uint8_t* data, size_t len)
{
    i2c_driver_.write_blob(Esp32Bridge::SLAVE_ADDR, kind, data, len);
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
 * solid = no search (every slot in use), as on the Pico W (Board/StatusLed.h), on the GPIO LED and
 * the external RGB one. Several LEDs (several RP2040s): one per output, solid when its pad is
 * connected. */
void BTManager::check_led_cb(btstack_timer_source *ts)
{
    static bool led_state = false;
    uint32_t next_ms = LED_CHECK_TIME_MS;

    /* Whether new controllers are accepted and at which rate, for the diagnostics and the
     * RP2040's status LED (forwarded on a change). */
    const bool searching = uni_bt_is_scanning();
    const bool reduced = searching && scan_control::reduced();
    diag::searching(searching, reduced);

    /* A colour saved or previewed in the web app, or other status LED options: show them now, on
     * the RGB LED and on a connected DS4 / DualSense lightbar. */
    static uint32_t s_generation = UINT32_MAX;
    static dongle_settings::Settings s_options{};
    static bool s_refresh = false;
    const dongle_settings::Settings& options = dongle_settings::get();
    if (s_generation != mode_indicator::color_generation())
    {
        s_generation = mode_indicator::color_generation();
        uint8_t r, g, b;
        mode_indicator::shown_color(UserSettings::get_instance().get_current_driver(), r, g, b);
        board_api::set_led_color(r, g, b);
        for (int i = 0; i < static_cast<int>(get_instance().slots_.size()); ++i)
        {
            apply_mode_lightbar(i);
        }
        s_refresh = true;
    }
    if (options.status_led_esp32 != s_options.status_led_esp32 ||
        options.status_led_esp32_ext != s_options.status_led_esp32_ext ||
        options.status_led_brightness != s_options.status_led_brightness)
    {
        s_options = options;
        s_refresh = true;
    }

    if constexpr (board_api::NUM_LEDS <= 1)
    {
        static status_led::Blinker s_led;
        if (s_refresh)
        {
            s_led.invalidate();
            s_refresh = false;
        }
        const status_led::Search search = !searching ? status_led::Search::NONE
                                        : reduced ? status_led::Search::REDUCED
                                        : status_led::Search::FULL;
        bool on = false;
        if (s_led.step(search, on, next_ms))
        {
            board_api::set_led(on);
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

/* The output mode's blink code; its colour follows on the next LED step (color_generation). */
void BTManager::begin_mode_indicator(DeviceDriverType driver)
{
    mode_indicator::begin(driver);
    uint8_t r, g, b;
    mode_indicator::shown_color(driver, r, g, b);
    board_api::set_led_color(r, g, b);
}

void BTManager::send_driver_type(DeviceDriverType driver_type, bool from_combo)
{
    for (uint8_t i = 0; i < (I2CDriver::MULTI_SLAVE ? MAX_GAMEPADS : 1); ++i)
    {
        I2CDriver::PacketIn packet_in = outputs_[i].packet_in;
        packet_in.packet_id = I2CDriver::PacketID::SET_DRIVER;
        packet_in.index = i;
        packet_in.device_driver = driver_type;
        packet_in.request_flags = from_combo ? I2CDriver::PacketIn::FROM_COMBO : 0;
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
            bt_manager.diag_event("search stopped (controller)");
            scan_control::request_stop();
            bt_manager.stop_search_feedback();
            bt_manager.apply_scan(false, false);
        }
        else if (result.new_driver != DeviceDriverType::NONE)
        {
            bt_manager.diag_event("mode change by button combo");
            if (settings.remote())
            {
                /* The RP2040 stores it and restarts in the new mode; sent until it reports it. */
                bt_manager.pending_driver_ = result.new_driver;
                bt_manager.pending_driver_ticks_ = 10;
                settings.set_remote_driver(result.new_driver);
            }
            else
            {
                settings.store_driver_type(result.new_driver);
            }
            bt_manager.send_driver_type(result.new_driver, true);  // now: the pads may be turned off next
            bt_manager.adopt_driver(result.new_driver, true);
        }
    }

    if (!settings.remote())
    {
        /* An RP2040 that keeps no settings (it never answers GET_EXT) takes the mode from us, after
         * a wait (I2C/ModeSync.h): a freshly started ESP32 would otherwise push the mode in its own
         * flash onto an RP2040 that just restarted in another one. */
        if (mode_sync::esp32_sends_own_mode(settings.remote(), btstack_run_loop_get_time_ms() - bt_manager.boot_ms_))
        {
            bt_manager.send_driver_type(settings.get_current_driver(), false);
        }
    }
    else if (bt_manager.pending_driver_ != DeviceDriverType::NONE)
    {
        bt_manager.send_driver_type(bt_manager.pending_driver_, true);
        if (--bt_manager.pending_driver_ticks_ == 0)
        {
            bt_manager.pending_driver_ = DeviceDriverType::NONE;
        }
    }

    btstack_run_loop_set_timer(ts, UserSettings::GP_CHECK_DELAY_MS);
    btstack_run_loop_add_timer(ts);
}

/* A new output mode: profiles (they depend on it), the blink code, the lightbar colour. A real
 * mode change (not the first copy at boot) also turns the controllers off first when the adapter
 * option asks for it, as the Pico W does. */
void BTManager::adopt_driver(DeviceDriverType driver, bool mode_change)
{
    if (mode_change && dongle_settings::get().disconnect_pads_on_mode_change)
    {
        for (uint8_t i = 0; i < slots_.size(); ++i)
        {
            uni_hid_device_t* d = slots_[i].connected ? get_connected_bp32_device(i) : nullptr;
            if (d)
            {
                bt_pad::disconnect_with_partner(d);
            }
        }
        return;  // the ESP32 restarts once the last one is gone, in the new mode
    }
    apply_profiles();
    begin_mode_indicator(driver);
    for (uint8_t i = 0; i < slots_.size(); ++i)
    {
        uni_hid_device_t* d = slots_[i].connected ? get_connected_bp32_device(i) : nullptr;
        if (d)
        {
            schedule_mode_lightbar(d, i);
        }
    }
}

void BTManager::profiles_cb(void* context)
{
    BTManager& bt_manager = get_instance();
    bt_manager.profiles_crc_ = bt_manager.fetch_crc_;
    bt_manager.profiles_active_ = bt_manager.fetch_active_;
    UserSettings::get_instance().set_remote(bt_manager.profiles_rx_, bt_manager.profiles_active_);
    bt_manager.profiles_fetching_ = false;
    bt_manager.apply_profiles();
    OGXM_LOG("BP32: Profiles from the RP2040 (crc %04x, active %u)\n", bt_manager.profiles_crc_,
             bt_manager.profiles_active_);
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
    const bool web_app_open = (ext.flags & I2CDriver::PacketOutExt::WEB_APP_OPEN) != 0;
    if (web_app_open != web_app_open_)
    {
        web_app_open_ = web_app_open;
        apply_profiles();
    }
    /* The mode colour the RP2040 resolved (the web app's colour or its preview); 0, 0, 0 from an
     * RP2040 that predates it = the default. */
    const bool mode_rgb = (ext.mode_rgb[0] | ext.mode_rgb[1] | ext.mode_rgb[2]) != 0;
    mode_indicator::set_override(mode_rgb, ext.mode_rgb[0], ext.mode_rgb[1], ext.mode_rgb[2]);
    if (ext.index < outputs_.size())
    {
        Lightbar& host = outputs_[ext.index].host_lightbar;
        host.valid = (ext.flags & I2CDriver::PacketOutExt::LIGHTBAR_VALID) != 0;
        host.r = ext.lightbar_r;
        host.g = ext.lightbar_g;
        host.b = ext.lightbar_b;
    }
    /* The RP2040 keeps the profiles and the mode (an older RP2040 sends no checksum). */
    if (ext.profiles_crc != 0)
    {
        UserSettings& user_settings = UserSettings::get_instance();
        if (pending_driver_ == ext.driver)
        {
            pending_driver_ = DeviceDriverType::NONE;
        }
        if (user_settings.remote() && pending_driver_ == DeviceDriverType::NONE &&
            ext.driver != user_settings.get_current_driver() && user_settings.is_valid_driver(ext.driver))
        {
            user_settings.set_remote_driver(ext.driver);
            adopt_driver(ext.driver, true);
        }
        const uint32_t now_ms = btstack_run_loop_get_time_ms();
        const bool stale = !user_settings.remote() || ext.profiles_crc != profiles_crc_ ||
                           ext.active_profile != profiles_active_;
        if (stale && (!profiles_fetching_ || now_ms - profiles_fetch_ms_ > 2000))
        {
            profiles_fetching_ = true;
            profiles_fetch_ms_ = now_ms;
            fetch_crc_ = ext.profiles_crc;
            fetch_active_ = ext.active_profile;
            if (!user_settings.remote() && user_settings.is_valid_driver(ext.driver) &&
                ext.driver != user_settings.get_current_driver())
            {
                user_settings.set_remote_driver(ext.driver);  // taken with the first copy
                adopt_driver(ext.driver, false);
            }
            profiles_cb_reg_.callback = profiles_cb;
            profiles_cb_reg_.context = nullptr;
            i2c_driver_.read_profiles(slave_addr(ext.index),
                [this](const I2CDriver::Profiles& profiles)
                {
                    profiles_rx_ = profiles;
                    btstack_run_loop_execute_on_main_thread(&profiles_cb_reg_);
                });
        }
    }

    dongle_settings::Settings settings;
    if (dongle_settings::decode(ext.settings, sizeof(ext.settings), settings))
    {
        apply_tx_power(settings.high_tx_power != 0);
        const bool imu_side_changed = settings.joycon_pair_imu_right != dongle_settings::get().joycon_pair_imu_right;
        dongle_settings::set(settings);
        if (imu_side_changed)
        {
            uni_hid_parser_switch_set_pair_imu_side(joycon_settings::get().pair_imu_right);
        }
    }
}

/* Adapter option "higher transmit power": Classic +9 dBm at most and BLE +9 dBm, else the ESP32's
 * defaults (Classic up to +3 dBm, BLE +3 dBm). Applied live. */
void BTManager::apply_tx_power(bool high)
{
    if (high_tx_power_ == (high ? 1 : 0))
    {
        return;
    }
    high_tx_power_ = high ? 1 : 0;
    esp_bredr_tx_power_set(ESP_PWR_LVL_N0, high ? ESP_PWR_LVL_P9 : ESP_PWR_LVL_P3);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, high ? ESP_PWR_LVL_P9 : ESP_PWR_LVL_P3);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, high ? ESP_PWR_LVL_P9 : ESP_PWR_LVL_P3);
    diag_event("TX power: %s", high ? "high (+9 dBm)" : "default (+3 dBm)");
}

/* Every 5 s, what the ESP32 measured itself, for the diagnostics report (companion). */
void BTManager::send_companion_summary()
{
    static constexpr uint32_t kPeriodS = 5;
    for (uint8_t i = 0; i < slots_.size(); ++i)
    {
        uint32_t received = 0, lost = 0;
        bool lost_valid = false;
        diag_forward::take_slot_stats(i, received, lost, lost_valid);
        if (!slots_[i].connected)
        {
            continue;
        }
        const uint32_t total = received + lost;
        const uint16_t lost_x10 = total ? static_cast<uint16_t>(lost * 1000u / total) : 0;
        // received: per physical controller (diag::slot_report), each half of a Joy-Con pair on its own
        diag::companion_slot(i, static_cast<uint16_t>(received / kPeriodS), lost_valid && total > 0, lost_x10);
    }
    diag::companion_version(OGXM_VERSION);
    diag::companion_info(btstack_run_loop_get_time_ms(), "",
                         static_cast<uint32_t>(esp_timer_get_time() / 1000000), high_tx_power_ == 1,
                         i2c_driver_.write_errors(), i2c_driver_.read_errors(), i2c_driver_.queue_drops());
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
                bt_manager.diag_event("slot %d idle %u min: off", i, minutes);
                bt_pad::turn_off(d);
            }
        }
    }
    bt_manager.apply_scan(false, false);  // also catches a Joy-Con pair completed after device_ready
    link_diag::query(now_ms, [](int slot)
        {
            return slot >= 0 && slot < static_cast<int>(get_instance().slots_.size()) && get_instance().slots_[slot].connected;
        });
    diag_forward::tick();
    static uint8_t summary_ticks = 0;
    if (++summary_ticks >= 5000 / FEEDBACK_TIME_MS)
    {
        summary_ticks = 0;
        bt_manager.send_companion_summary();
    }

    /* The ESP32's Bluetooth address for the RP2040 (DualShock 3 pairing on its USB host port),
     * every 2 s so it is back soon after the RP2040 restarts. */
    static uint8_t address_ticks = 0;
    if (++address_ticks >= 2000 / FEEDBACK_TIME_MS)
    {
        address_ticks = 0;
        /* and the mouse + keyboard mapping for the BLE web app (it changes without a restart) */
        bt_manager.i2c_driver_.read_blob(Esp32Bridge::SLAVE_ADDR, I2CDriver::PacketBlob::KBM_SETTINGS,
            [&bt_manager](const uint8_t* data, size_t len)
            {
                std::lock_guard<std::mutex> lock(bt_manager.kbm_mutex_);
                bt_manager.kbm_rx_len_ = std::min(len, sizeof(bt_manager.kbm_rx_));
                std::memcpy(bt_manager.kbm_rx_, data, bt_manager.kbm_rx_len_);
            });
        bd_addr_t address;
        gap_local_bd_addr(address);
        bt_manager.diag_send(I2CDriver::PacketDiag::ADAPTER_ADDRESS, 0, address, sizeof(address));
    }

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
    apply_mode_lightbar(static_cast<int>(reinterpret_cast<intptr_t>(ts->context)));
}

void BTManager::apply_mode_lightbar(int slot)
{
    BTManager& bt_manager = get_instance();
    uni_hid_device_t* d = get_connected_bp32_device(static_cast<uint8_t>(slot));
    if (!d || !bt_manager.slots_[slot].connected)
    {
        return;
    }
    if (d->controller_type != CONTROLLER_TYPE_PS4Controller &&
        d->controller_type != CONTROLLER_TYPE_PS5Controller)
    {
        return;
    }
    Lightbar mode;
    mode_indicator::shown_color(UserSettings::get_instance().get_current_driver(), mode.r, mode.g, mode.b);
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

    bt_manager.diag_event("slot %d: off combo, restarting", slot);
    restart_after_ms(bt_pad::kComboRebootDelayMs);  // armed before touching the pad
    bt_pad::disconnect_with_partner(bp_device);
}

/* Restart from an esp_timer, not the BTstack run loop: it happens even if the BT stack stalls
 * while the pads go away (the Pico W path arms its hardware watchdog for the same reason). */
static esp_timer_handle_t restart_timer = nullptr;
static bool restart_for_last_pad = false;  // a pad connecting in the meantime calls it off

void BTManager::restart_after_ms(uint32_t delay_ms, bool last_pad)
{
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
        restart_for_last_pad = last_pad;
        esp_timer_start_once(restart_timer, static_cast<uint64_t>(delay_ms) * 1000u);
    }
    else if (!last_pad)
    {
        restart_for_last_pad = false;  // a combo restart is never called off
    }
}

/* A pad connected while the restart after the last one leaving was pending: the stack is in use
 * again, and restarting would drop the new pad (the RP2040 would keep its slot until then). */
bool BTManager::cancel_last_pad_restart()
{
    if (restart_timer == nullptr || !restart_for_last_pad || !esp_timer_is_active(restart_timer))
    {
        return false;
    }
    esp_timer_stop(restart_timer);
    restart_for_last_pad = false;
    return true;
}
