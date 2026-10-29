#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <pico/mutex.h>
#include <pico/cyw43_arch.h>
#include <pico/time.h>
#include <hardware/watchdog.h>

#include <btstack.h>
#include "btstack_run_loop.h"
#include "gap.h"
#include "uni.h"
#include "bt/uni_bt.h"
#include "bt/uni_bt_bredr.h"
#include "bt/uni_bt_le.h"
#include "uni_hid_device.h"

#include "sdkconfig.h"

#if defined(CONFIG_TARGET_PICO_W) && defined(CONFIG_EN_USB_HOST)
/** Core0 USB mux reads this while Core1 BT stack updates connections — mirror bt_devices_[].connected. */
static std::atomic<bool> s_bt_any_connected_cached{false};
#endif

#include "Bluepad32/Bluepad32.h"
#include "Bluepad32/ClassicPairingDebug.h"
#include "Board/board_api.h"
#include "Board/ogxm_log.h"
#include "Bluepad32/ScanControl.h"
#include "Bluepad32/BtPad.h"
#include "Bluepad32/BtPadIn.h"
#include "Bluepad32/LinkDiag.h"
#include "Bluepad32/ComboHold.h"
#include "Bluepad32/IdleOff.h"
#include "Diagnostics/Diagnostics.h"
#include "uni_diag_hooks.h"
#include "UserSettings/DongleSettings.h"
#include "Bluepad32/JoyConSettings.h"
#include "Board/ModeIndicator.h"
#include "Board/StatusLed.h"
#include "Gamepad/RumbleTiming.h"
#include "Board/ReportedMac.h"
#include "Input/InputSlot.h"
#include "UserSettings/UserSettings.h"
#include "USBHost/HostDriver/FlydigiApex4Wukong/FlydigiApex4WukongBtProbe.h"
#include "USBHost/HostDriver/FlydigiApex4Wukong/FlydigiApex4WukongBt.h"
#include "USBHost/HostDriver/GameSirCyclone2/Cyclone2BtProbe.h"
#include "parser/uni_hid_parser_ds5.h"
#include "controller/uni_controller.h"
#include "parser/uni_hid_parser_wii.h"
#include "Gamepad/MotionImu.h"
#include "USBDevice/DeviceDriver/MotionOutputActive.h"
#include "USBDevice/DeviceDriver/Steam/SteamActive.h"
#include "USBDevice/DeviceDriver/Steam/SteamBtReport.h"
#include "USBDevice/DeviceDriver/Steam/SteamPassthrough.h"
#include "USBDevice/DeviceDriver/Steam/SteamTouchpad.h"

#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
    #error "Pico W must use BLUEPAD32_PLATFORM_CUSTOM"
#endif

static_assert((CONFIG_BLUEPAD32_MAX_DEVICES >= MAX_GAMEPADS),
              "Bluepad32 must allow at least as many BT devices as USB gamepad slots");

namespace bluepad32 {

#if defined(CONFIG_OGXM_DEBUG)
static void log_ogx_slots(const char* tag) {
    printf("\n%s\n", tag ? tag : "[INPUT SLOTS]");
    for (uint8_t i = 0; i < MAX_GAMEPADS; ++i) {
        const InputSlot::State st = InputSlot::get(i);
        if (st.transport == InputTransport::USB) {
            printf("INPUT SLOT %u\ntransport=USB\ndriver=%s\naddr=%u\ninstance=%u\n",
                   static_cast<unsigned>(i), InputSlot::driver_name(st.physical_driver),
                   static_cast<unsigned>(st.usb_addr), static_cast<unsigned>(st.usb_instance));
        } else {
            printf("INPUT SLOT %u\ntransport=%s\n", static_cast<unsigned>(i),
                   InputSlot::transport_name(st.transport));
        }
    }
}
#endif

/** Prefer a free OGX pad; never reuse a USB-owned pad for Bluetooth output. */
static int resolve_bt_output_pad_idx(int preferred_idx) {
    if (preferred_idx >= 0 && preferred_idx < static_cast<int>(MAX_GAMEPADS) &&
        !InputSlot::usb_owns(static_cast<uint8_t>(preferred_idx))) {
        return preferred_idx;
    }
    for (uint8_t i = 0; i < MAX_GAMEPADS; ++i) {
        if (!InputSlot::usb_owns(i)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/* Custom fix: the disconnect combo is detected while parsing the pad's own input report.
 * Disconnecting it (and its Joy-Con partner) right there tore the pair down under the
 * parser's feet and hung the BT core. Defer the disconnect to a run-loop timer instead. */
static btstack_timer_source_t s_disconnect_combo_timer;
static int s_disconnect_combo_idx = -1;

/* Custom: after the disconnect combo the adapter stopped accepting controller-initiated
 * reconnections (no HCI connection request ever arrived), while a fresh boot always accepts
 * them. So reboot (same output mode) for a clean radio. The reboot is armed on the hardware
 * watchdog *before* touching the pads: a run-loop timer never fired when the BT core hung
 * while the pads were going away. Nothing feeds the watchdog, so it always resets. */
static constexpr uint32_t DISCONNECT_COMBO_REBOOT_DELAY_MS = bt_pad::kComboRebootDelayMs;
/* Safety net for the reboot after the last ready pad disconnects (normally 500 ms). */
static constexpr uint32_t DISCONNECT_REBOOT_WATCHDOG_MS = bt_pad::kLastPadRebootFallbackMs;

static void disconnect_combo_timer_cb(btstack_timer_source_t* ts)
{
    (void)ts;
    const int idx = s_disconnect_combo_idx;
    s_disconnect_combo_idx = -1;
    if (idx < 0 || idx >= CONFIG_BLUEPAD32_MAX_DEVICES)
        return;
    uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(idx);
    if (!d || d->conn.handle == UNI_BT_CONN_HANDLE_INVALID)
        return;
    printf("[BP32] Disconnect combo: reboot in %lu ms for a clean reconnect\n",
           static_cast<unsigned long>(DISCONNECT_COMBO_REBOOT_DELAY_MS));
    /* Custom: in the flash log too (as on the ESP32), or it reads like the idle turn-off. */
    diag::event(to_ms_since_boot(get_absolute_time()), "slot %d: off combo, reboot in %lu ms", idx,
                static_cast<unsigned long>(DISCONNECT_COMBO_REBOOT_DELAY_MS));
    watchdog_enable(DISCONNECT_COMBO_REBOOT_DELAY_MS, true);
    bt_pad::disconnect_with_partner(d);
}

static void schedule_disconnect_combo(int idx)
{
    if (s_disconnect_combo_idx >= 0)
        return;  // already pending
    s_disconnect_combo_idx = idx;
    s_disconnect_combo_timer.process = disconnect_combo_timer_cb;
    s_disconnect_combo_timer.context = nullptr;
    btstack_run_loop_set_timer(&s_disconnect_combo_timer, 0);
    btstack_run_loop_add_timer(&s_disconnect_combo_timer);
}

static constexpr uint32_t FEEDBACK_TIME_MS = 250;
static_assert(FEEDBACK_TIME_MS == switch_rumble::kFeedbackPeriodMs, "keep Gamepad/RumbleTiming.h in sync");
static constexpr uint32_t LED_CHECK_TIME_MS = 500;
/** Idle pairing health check — restarts BR/LE scan if they died during long USB suspend (e.g. 360 standby). */
static constexpr uint32_t PAIRING_WATCHDOG_MS = 45000;
/** If no HID input report reaches us for this long while "connected", the BT link is zombie
 *  (L2CAP stops delivering; OG Xbox then holds last USB report). Force disconnect so user can reconnect. */
static constexpr uint32_t BT_INPUT_STALL_DISCONNECT_MS = 8000;

static uint32_t s_last_bt_input_ms[CONFIG_BLUEPAD32_MAX_DEVICES]{};
static bt_pad::KeepaliveState s_ble_keepalive[CONFIG_BLUEPAD32_MAX_DEVICES]{};
/* Custom fix: the per-pad state arrays below are indexed both by OGX pad and by Bluetooth slot
 * (disconnect callback, feedback loop). They were sized MAX_GAMEPADS (1), so a pad in slot 1 (the
 * right Joy-Con of a pair, on every disconnect) wrote past them into other variables. Sized for
 * every Bluetooth slot now (CONFIG_BLUEPAD32_MAX_DEVICES >= MAX_GAMEPADS). */
/** Disconnect combo per pad, ignored for a moment after connect (DS4 can glitch both on first reports). */
static ComboHold s_disconnect_combo[CONFIG_BLUEPAD32_MAX_DEVICES]{};
/** DS4 BT: delay rumble output (host can request rumble immediately; early FF reports can drop link). */
static uint32_t s_ps4_rumble_ok_ms[CONFIG_BLUEPAD32_MAX_DEVICES]{};

struct BTDevice {
    bool connected{false};
    Gamepad* gamepad{nullptr};
};

BTDevice bt_devices_[CONFIG_BLUEPAD32_MAX_DEVICES];

/* Custom: a Bluetooth controller left on without input is turned off after the dongle option's
 * idle time (Bluepad32/IdleOff.h, shared with the ESP32), per output gamepad. */
static IdleOff s_idle[MAX_GAMEPADS]{};

static int bt_output_idx(uni_hid_device_t* d) {
    int idx = bt_pad::output_idx(d);
    if (idx < 0)
        idx = uni_hid_device_get_idx_for_instance(d);
    return resolve_bt_output_pad_idx(idx);
}

static void idle_note_input(int idx, const uni_gamepad_t* gp, uint32_t now_ms) {
    if (idx < 0 || idx >= static_cast<int>(MAX_GAMEPADS))
        return;
    s_idle[idx].note_input(gp, now_ms, FEEDBACK_TIME_MS + 100);
}

static void idle_check(uint32_t now_ms) {
    const uint8_t minutes = dongle_settings::get().idle_off_minutes;
    for (int idx = 0; idx < static_cast<int>(MAX_GAMEPADS); ++idx) {
        if (!s_idle[idx].due(now_ms, minutes))
            continue;
        for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i) {
            uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
            if (!bt_devices_[i].connected || !d || d->conn.handle == UNI_BT_CONN_HANDLE_INVALID || bt_output_idx(d) != idx)
                continue;
            diag::event(now_ms, "slot %d idle %u min: off", i, minutes);
            bt_pad::turn_off(d);
        }
    }
}


/* Custom: diagnostics — remote version still to ask, and the periodic link queries. */
static bool bt_slot_connected(int slot)
{
    return slot >= 0 && slot < CONFIG_BLUEPAD32_MAX_DEVICES && bt_devices_[slot].connected;
}
static void apply_scan_policy(int exclude_idx, bool force = false);
static bool scan_reduced();


/* Custom: the reboot after the last controller disconnects stores the session summary first
 * (Diagnostics/Diagnostics.h). Flash is written from Core0 (the USB loop), which BT cannot park, so
 * the BT core asks and Core0 stores and reboots; the watchdog armed for that reboot still fires
 * if Core0 does not. */
static std::atomic<bool> s_store_session_and_reboot{false};

btstack_timer_source_t feedback_timer_;
btstack_timer_source_t led_timer_;
bool led_timer_set_{false};
bool feedback_timer_set_{false};

/** Core0 USB mux may run before Core1 finishes uni_init(); BTstack asserts if
 *  execute_on_main_thread is used with the_run_loop == NULL (boot loop with pad plugged). */
static std::atomic<bool> s_btstack_run_loop_ready{false};
/** Wired USB asked to silence BT before the stack was up — apply after init. */
static std::atomic<bool> s_bt_quiet_for_usb_pending{false};

static constexpr uint32_t GPIO_PROCESS_INTERVAL_MS = 4;
static btstack_timer_source_t gpio_process_timer_;
static void (*gpio_process_cb_)(void*) = nullptr;
static void* gpio_process_ctx_ = nullptr;

static void (*s_pico_w_pio_usb_mux_tick)(void) = nullptr;
static btstack_timer_source_t s_pico_w_usb_mux_timer_;
static btstack_context_callback_registration_t s_pico_w_usb_mux_main_reg;

// Timer callbacks must not call sleep_* (Pico panics). TinyUSB host enumeration does; run mux on main thread.
static void pico_w_usb_mux_run_on_main(void* ctx) {
    (void)ctx;
    if (s_pico_w_pio_usb_mux_tick != nullptr) {
        s_pico_w_pio_usb_mux_tick();
    }
}

static void pico_w_usb_mux_timer_cb(btstack_timer_source_t* ts) {
    s_pico_w_usb_mux_main_reg.callback = pico_w_usb_mux_run_on_main;
    s_pico_w_usb_mux_main_reg.context = nullptr;
    btstack_run_loop_execute_on_main_thread(&s_pico_w_usb_mux_main_reg);
    btstack_run_loop_set_timer(ts, 1);
    btstack_run_loop_add_timer(ts);
}

static void gpio_process_timer_cb(btstack_timer_source_t* ts) {
    if (gpio_process_cb_ != nullptr && gpio_process_ctx_ != nullptr) {
        gpio_process_cb_(gpio_process_ctx_);
    }
    btstack_run_loop_set_timer(ts, GPIO_PROCESS_INTERVAL_MS);
    btstack_run_loop_add_timer(ts);
}

// PS5: touchpad click toggles adaptive triggers (per-controller state)
static bool adaptive_trigger_enabled_[CONFIG_BLUEPAD32_MAX_DEVICES]{false};
static bool prev_touchpad_clicked_[CONFIG_BLUEPAD32_MAX_DEVICES]{false};
// Defer sending adaptive trigger effect out of BT callback to avoid l2cap_send in callback (reduces input lag)
static bool pending_adaptive_trigger_send_[CONFIG_BLUEPAD32_MAX_DEVICES]{false};
/* Regression guard: these are indexed by Bluetooth slot (see the comment on the per-pad state
 * arrays above); a pad in slot 1 wrote past them when they were sized MAX_GAMEPADS. */
template <typename T, size_t N>
constexpr size_t bt_slots_of(const T (&)[N]) { return N; }
static_assert(bt_slots_of(s_disconnect_combo) >= CONFIG_BLUEPAD32_MAX_DEVICES, "sized per BT slot");
static_assert(bt_slots_of(s_ps4_rumble_ok_ms) >= CONFIG_BLUEPAD32_MAX_DEVICES, "sized per BT slot");
static_assert(bt_slots_of(adaptive_trigger_enabled_) >= CONFIG_BLUEPAD32_MAX_DEVICES, "sized per BT slot");
static_assert(bt_slots_of(prev_touchpad_clicked_) >= CONFIG_BLUEPAD32_MAX_DEVICES, "sized per BT slot");
static_assert(bt_slots_of(pending_adaptive_trigger_send_) >= CONFIG_BLUEPAD32_MAX_DEVICES, "sized per BT slot");

bool any_connected()
{
#if defined(CONFIG_TARGET_PICO_W) && defined(CONFIG_EN_USB_HOST)
    return s_bt_any_connected_cached.load(std::memory_order_acquire);
#else
    for (auto& device : bt_devices_)
    {
        if (device.connected)
        {
            return true;
        }
    }
    return false;
#endif
}

bool is_wii_controller_connected(uint8_t idx)
{
    if (idx >= MAX_GAMEPADS) {
        return false;
    }
    uni_hid_device_t* bp_device = uni_hid_device_get_instance_for_idx(idx);
    return (bp_device != nullptr && bp_device->controller_type == CONTROLLER_TYPE_WiiController);
}

bool any_wii_controller_connected()
{
    for (uint8_t i = 0; i < MAX_GAMEPADS; ++i)
    {
        if (bt_devices_[i].connected && is_wii_controller_connected(i))
        {
            return true;
        }
    }
    return false;
}

/* Custom: last lightbar colour sent to each DS4 / DualSense (mode colour or host colour). */
using Lightbar = Gamepad::Lightbar;
static Lightbar s_lightbar_applied[CONFIG_BLUEPAD32_MAX_DEVICES];

/* Custom: send the host's lightbar colour (PS4 mode) to a DS4 / DualSense when it changes.
 * Waits out the DS4 post-connect window like rumble does. */
static void apply_host_lightbar(uni_hid_device_t* d, int bt_idx, const Lightbar& want, uint32_t now_ms)
{
    if (d->controller_type == CONTROLLER_TYPE_PS4Controller && now_ms < s_ps4_rumble_ok_ms[bt_idx])
        return;
    bt_pad::set_lightbar(d, want, s_lightbar_applied[bt_idx]);
}

/* Custom: Switch HD Rumble passthrough (Gamepad/HdRumbleFeed.h). In Switch mode the host's reports
 * go to the output's Joy-Cons / Pro Controller as they are, each once and in order (a Joy-Con of
 * a pair plays its own side's block, as on a Switch); the intensity rumble of send_feedback_cb
 * stays off for those pads while HD Rumble comes in. Other pads and modes are left as they were. */
static btstack_timer_source_t s_hd_rumble_timer;
static bool s_hd_rumble_timer_set{false};
static hd_rumble::Feed s_hd_feed[MAX_GAMEPADS];

static void hd_rumble_send(int gp_idx, const uint8_t blocks[8])
{
    for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (!bt_devices_[i].connected || !bt_pad::plays_hd_rumble(d))
            continue;
        const int out_i = bt_pad::output_idx(d);
        if ((out_i >= 0 ? out_i : i) == gp_idx)
            uni_hid_parser_switch_play_raw_rumble(d, blocks);
    }
}

static void hd_rumble_cb(btstack_timer_source_t* ts)
{
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    for (int g = 0; g < static_cast<int>(MAX_GAMEPADS); ++g) {
        Gamepad* gamepad = bt_devices_[g].gamepad;
        if (gamepad == nullptr)
            continue;
        hd_rumble::Frame frame;
        bool any = false;
        while (gamepad->pop_hd_rumble(frame)) {
            any = true;
            s_hd_feed[g].on_frame(frame, now_ms);
            hd_rumble_send(g, frame.blocks);
        }
        if (s_hd_feed[g].playing())
            s_idle[g].note_rumble(now_ms);  // the idle turn-off ignores the shaking
        if (!any && s_hd_feed[g].stop_due(now_ms)) {
            const hd_rumble::Frame neutral = hd_rumble::neutral_frame();
            hd_rumble_send(g, neutral.blocks);
        }
    }
    if (s_hd_rumble_timer_set) {
        btstack_run_loop_set_timer(ts, hd_rumble::kPeriodMs);
        btstack_run_loop_add_timer(ts);
    }
}

static void hd_rumble_start()
{
    if (s_hd_rumble_timer_set || UserSettings::get_instance().get_current_driver() != DeviceDriverType::SWITCH)
        return;
    s_hd_rumble_timer_set = true;
    for (int g = 0; g < static_cast<int>(MAX_GAMEPADS); ++g) {  // reports queued with no pad: stale
        hd_rumble::Frame stale;
        while (bt_devices_[g].gamepad != nullptr && bt_devices_[g].gamepad->pop_hd_rumble(stale)) {
        }
    }
    s_hd_rumble_timer.process = hd_rumble_cb;
    s_hd_rumble_timer.context = nullptr;
    btstack_run_loop_set_timer(&s_hd_rumble_timer, hd_rumble::kPeriodMs);
    btstack_run_loop_add_timer(&s_hd_rumble_timer);
}

static void hd_rumble_stop()
{
    if (!s_hd_rumble_timer_set)
        return;
    s_hd_rumble_timer_set = false;
    btstack_run_loop_remove_timer(&s_hd_rumble_timer);
    for (auto& feed : s_hd_feed)
        feed.reset();
}

static void send_feedback_cb(btstack_timer_source *ts)
{
    uni_hid_device_t* bp_device = nullptr;
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    /* Custom: rumble read once per gamepad per tick (both Joy-Cons of a pair share one). */
    Gamepad::PadOut pad_out[MAX_GAMEPADS];
    bool pad_out_read[MAX_GAMEPADS] = {};

    for (uint8_t i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i)
    {
        if (!bt_devices_[i].connected ||
            !(bp_device = uni_hid_device_get_instance_for_idx(i)))
        {
            continue;
        }
        /* BLE Xbox (Series) and Switch 2 BLE may not send reports every poll interval. */
        const bool skip_stall_disconnect =
            (bp_device->controller_type == CONTROLLER_TYPE_XBoxOneController && bp_device->hids_cid != 0) ||
            uni_hid_parser_switch2_is_ble_device(bp_device) ||
            uni_hid_parser_steam_triton_is_device(bp_device);
        /* Virtual slot (e.g. DS4's BT "mouse"): never gets gamepad HID → would always stall-disconnect
         * and drop the real controller. */
        if (uni_hid_device_is_virtual_device(bp_device))
            goto after_stall_check;
        if (uni_hid_parser_switch2_is_joycon_pair_secondary(bp_device))
            goto after_stall_check;
        if (bt_pad::is_joycon_pair_secondary(bp_device))
            goto after_stall_check;
        if (!skip_stall_disconnect)
        {
        const int stall_idx = bt_pad::output_idx(bp_device);
        const unsigned stall_slot =
            (stall_idx >= 0 && stall_idx < static_cast<int>(MAX_GAMEPADS))
                ? static_cast<unsigned>(stall_idx)
                : static_cast<unsigned>(i);
        
        }
    after_stall_check:

        const int out_i = bt_pad::output_idx(bp_device);
        const int gp_idx =
            (out_i >= 0 && out_i < static_cast<int>(MAX_GAMEPADS)) ? out_i : static_cast<int>(i);
        if (gp_idx < 0 || gp_idx >= static_cast<int>(MAX_GAMEPADS) || bt_devices_[gp_idx].gamepad == nullptr)
            continue;

        /* Custom: peak since the last tick, so host pulses shorter than FEEDBACK_TIME_MS still play. */
        if (!pad_out_read[gp_idx])
        {
            pad_out[gp_idx] = bt_devices_[gp_idx].gamepad->take_pad_out_peak();
            pad_out_read[gp_idx] = true;
        }
        const Gamepad::PadOut gp_out = pad_out[gp_idx];
        bt_pad::ble_keepalive(bp_device, gp_out.rumble_l > 0 || gp_out.rumble_r > 0, now_ms, s_ble_keepalive[i]);
        if (gp_out.rumble_l > 0 || gp_out.rumble_r > 0)  // Custom: idle turn-off ignores the shaking
            s_idle[gp_idx].note_rumble(now_ms);
        if ((gp_out.rumble_l > 0 || gp_out.rumble_r > 0) &&
            !(bt_pad::plays_hd_rumble(bp_device) && s_hd_feed[gp_idx].owns_rumble(now_ms)))  // HD Rumble plays it
        {
            if (bp_device->controller_type == CONTROLLER_TYPE_PS4Controller &&
                now_ms < s_ps4_rumble_ok_ms[i])
                ;
            else
                bt_pad::play_host_rumble(bp_device, static_cast<uint16_t>(FEEDBACK_TIME_MS), gp_out.rumble_l,
                                         gp_out.rumble_r, joycon_settings::get().pair_rumble_per_side);
        }
        apply_host_lightbar(bp_device, i, bt_devices_[gp_idx].gamepad->get_host_lightbar(), now_ms);
    }
    /* Custom: diagnostics rates roll over; link queries for each connected pad. */
    diag::tick(now_ms);
    link_diag::query(now_ms, bt_slot_connected);  // Bluepad32/LinkDiag, shared with the ESP32
    idle_check(now_ms);
    apply_scan_policy(-1);  // Custom: also catches a Joy-Con pair completed after device_ready
    if (feedback_timer_set_)
	{
        btstack_run_loop_set_timer(ts, FEEDBACK_TIME_MS);
        btstack_run_loop_add_timer(ts);
	}
}

static void apply_mode_lightbar(int idx);

static void check_led_cb(btstack_timer_source *ts)
{
    /* Custom: the mode's blink code, then the search for new controllers (Board/StatusLed.h);
     * a wired USB host controller active on the Pico W mux counts as no search. */
    static status_led::Blinker s_led;

    /* Custom: a colour saved or previewed in the web app, or other status LED options: show them
     * now, on the RGB LED and on a connected DS4 / DualSense lightbar. */
    static uint32_t s_generation = UINT32_MAX;
    static dongle_settings::Settings s_options{};
    const dongle_settings::Settings& options = dongle_settings::get();
    if (s_generation != mode_indicator::color_generation())
    {
        s_generation = mode_indicator::color_generation();
        uint8_t r, g, b;
        mode_indicator::shown_color(UserSettings::get_instance().get_current_driver(), r, g, b);
        board_api::set_led_color(r, g, b);
        for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i)
            apply_mode_lightbar(i);
        s_led.invalidate();
    }
    if (options.status_led_pico != s_options.status_led_pico ||
        options.status_led_pico_ext != s_options.status_led_pico_ext ||
        options.status_led_brightness != s_options.status_led_brightness)
    {
        s_options = options;
        s_led.invalidate();
    }

#if defined(CONFIG_TARGET_PICO_W) && defined(CONFIG_EN_USB_HOST)
    const bool wired_host_pad = board_api::usb::host_any_pad_mounted();
#else
    const bool wired_host_pad = false;
#endif
    const bool searching = !wired_host_pad && uni_bt_is_scanning();
    const status_led::Search search = !searching ? status_led::Search::NONE
                                    : scan_reduced() ? status_led::Search::REDUCED
                                    : status_led::Search::FULL;
    bool on = false;
    uint32_t next_ms = 0;
    if (s_led.step(search, on, next_ms)) {
        board_api::set_led(on);
    }

    btstack_run_loop_set_timer(ts, next_ms);
    btstack_run_loop_add_timer(ts);
}

//BT Driver

static void init(int argc, const char** arg_V) {
}

static void init_complete_cb(void) {
    // Keep Bluepad32 defaults (inquiry=3, max=5, min=4 in 1.28s units).
    // Shorter inquiry (2) breaks discovery for 8BitDo SN30 Pro / Pro 2 in D-input /
    // Android modes — see uni_bt_defines.h. (#86)
    uni_bt_set_gap_inquiry_length(UNI_BT_INQUIRY_LENGTH);
    uni_bt_set_gap_max_peridic_length(UNI_BT_MAX_PERIODIC_LENGTH);
    uni_bt_set_gap_min_peridic_length(UNI_BT_MIN_PERIODIC_LENGTH);

    uni_bt_start_scanning_and_autoconnect_unsafe();
    // uni_bt_del_keys_unsafe();  // use -DOGXM_BT_CLEAR_KEYS_ON_BOOT=1 or ogxm_bt_debug_clear_keys()
    uni_property_dump_all();
    ogxm_classic_pairing_debug_init();
    OGXM_LOG("BT: stack ready — BR/EDR inquiry + BLE scan (8BitDo: use Switch or Android mode)\n");
}

static uni_error_t device_discovered_cb(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
    uint8_t minor = cod & UNI_BT_COD_MINOR_MASK;

    if (!(minor & (UNI_BT_COD_MINOR_GAMEPAD |
                   UNI_BT_COD_MINOR_JOYSTICK |
                   UNI_BT_COD_MINOR_REMOTE_CONTROL))) {
        return UNI_ERROR_IGNORE_DEVICE;
    }

    /* Cyclone 2 green/"Game Pair Mode" is not a usable direct-BT XInput gamepad. */
    if (gamesir_cyclone2_bt_should_ignore_discovered(name)) {
        gamesir_cyclone2_bt_on_discovered(addr, name, cod, rssi);
        return UNI_ERROR_IGNORE_DEVICE;
    }

#if defined(CONFIG_OGXM_DEBUG)
    printf("\n[BT DEVICE FOUND]\naddr=%s\nname=%s\n", bd_addr_to_str(addr),
           name && name[0] ? name : "(none)");
    log_ogx_slots("BEFORE (discovery; BT idx != OGX slot)");
#endif
    (void)rssi;
    flydigi_apex4_bt_on_discovered(addr, name, cod, rssi);
    gamesir_cyclone2_bt_on_discovered(addr, name, cod, rssi);
    return UNI_ERROR_SUCCESS;
}

static void device_connected_cb(uni_hid_device_t* device) {
    if (device == nullptr) {
        return;
    }
    /* Safety net if name arrives only after ACL create. */
    if (gamesir_cyclone2_bt_reject_game_pair_mode_if_needed(device)) {
        return;
    }
    const int bt_idx = uni_hid_device_get_idx_for_instance(device);
#if defined(CONFIG_OGXM_DEBUG)
    printf("\n[BT DEVICE CREATE]\nbt_index=%d\nname=%s\n", bt_idx,
           device->name[0] ? device->name : "(none)");
    log_ogx_slots("BEFORE create/connect");
#endif
    flydigi_apex4_bt_on_connected(device);
    gamesir_cyclone2_bt_on_connected(device);
    if (uni_hid_parser_switch2_is_ble_device(device)) {
        OGXM_LOG("SW2: connected pid=0x%04x — waiting for encryption/GATT\n", device->product_id);
    }
#if defined(CONFIG_OGXM_DEBUG)
    log_ogx_slots("AFTER create/connect (USB slots must be unchanged)");
    if (bt_idx >= 0 && bt_idx < static_cast<int>(MAX_GAMEPADS) &&
        InputSlot::usb_owns(static_cast<uint8_t>(bt_idx))) {
        printf("[BT] NOTE: Bluepad idx=%d overlaps USB-owned OGX slot %d — "
               "will not write PadIn / reset that slot\n",
               bt_idx, bt_idx);
    }
#else
    (void)bt_idx;
#endif
}


/** CYW43: any active BLE gamepad link (HOGP or Triton Valve GATT) needs scans off. */
static bool device_is_ble_hogp(const uni_hid_device_t* d) {
    if (d == nullptr)
        return false;
    if (d->hids_cid != 0 && d->hids_cid != 0xffff)
        return true;
    /* Triton skips HIDS — still needs quiet radio while LE link is up. */
    if (uni_hid_parser_steam_triton_is_device(d) && d->conn.handle != UNI_BT_CONN_HANDLE_INVALID &&
        gap_get_connection_type(d->conn.handle) == GAP_CONNECTION_LE)
        return true;
    return false;
}

/** CYW43: periodic BR/EDR inquiry while a Classic ACL link is up can drop DS4/PS3 in ~1–2 s. */
static void maybe_restart_bredr_inquiry_after_disconnect(int disconnected_idx) {
#if defined(CONFIG_TARGET_PICO_W)
    if (!uni_bt_is_scanning())
        return;
    for (uint8_t i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i) {
        if (i == static_cast<unsigned>(disconnected_idx))
            continue;
        if (!bt_devices_[i].connected)
            continue;
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(static_cast<int>(i));
        if (!d || uni_bt_conn_get_state(&d->conn) != UNI_BT_CONN_STATE_DEVICE_READY)
            continue;
        if (gap_get_connection_type(d->conn.handle) == GAP_CONNECTION_ACL)
            return;
        /* Any BLE HOGP pad: keep BR inquiry off (same radio contention as Xbox). */
        if (device_is_ble_hogp(d))
            return;
    }
    uni_bt_bredr_scan_start();
#endif
}

/**
 * CYW43: Xbox Series (BLE) connect stops LE scan (and BR inquiry) to avoid supervision timeout,
 * but leaves Bluepad32 "new connections" enabled. On disconnect, enable_new_connections(true) is a
 * no-op, so LE scan never resumes and the pad cannot reconnect without power-cycling the Pico.
 * Restart LE scan unless another pad still requires it off.
 */
static void maybe_restart_ble_scan_after_disconnect(int disconnected_idx) {
#if defined(CONFIG_TARGET_PICO_W)
    if (!uni_bt_is_scanning())
        return;
    for (uint8_t i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i) {
        if (i == static_cast<unsigned>(disconnected_idx))
            continue;
        if (!bt_devices_[i].connected)
            continue;
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(static_cast<int>(i));
        if (!d || uni_bt_conn_get_state(&d->conn) != UNI_BT_CONN_STATE_DEVICE_READY)
            continue;
        if (device_is_ble_hogp(d))
            return;
        /* Solo Switch Joy-Con keeps LE scan off while waiting for partner over Classic BT. */
        if (uni_hid_parser_switch_solo_needs_partner(d))
            return;
    }
    uni_bt_le_scan_start();
#endif
}

/* Custom: search for new controllers only as hard as the free slots need (Bluepad32/ScanPolicy.h,
 * applied by Bluepad32/ScanControl, shared with the ESP32): a full-duty BLE scan next to a Classic
 * pad cost a DS4 7 reports in 8. exclude_idx: a pad that is going away. */
static void scan_diag_event(uint32_t now_ms, const char* text)
{
    diag::event(now_ms, "%s", text);
}

static bool scan_reduced()
{
    return scan_control::reduced();
}

static void apply_scan_policy(int exclude_idx, bool force)
{
#if defined(CONFIG_TARGET_PICO_W)
    bool used[MAX_GAMEPADS]{};
    scan_control::Pads pads;
    for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i) {
        if (i == exclude_idx || !bt_devices_[i].connected)
            continue;
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (!d)
            continue;
        if (device_is_ble_hogp(d)) pads.hogp = true;
        else if (gap_get_connection_type(d->conn.handle) == GAP_CONNECTION_ACL) pads.classic = true;
        const int out = bt_pad::output_idx(d);
        if (out >= 0 && out < static_cast<int>(MAX_GAMEPADS) && !used[out]) {
            used[out] = true;
            ++pads.outputs;
        }
    }
    pads.awaiting = uni_hid_parser_switch_any_awaiting_partner();
    scan_control::apply(pads, MAX_GAMEPADS, exclude_idx >= 0, force, to_ms_since_boot(get_absolute_time()),
                        dongle_settings::get(), false, scan_diag_event);
#else
    (void)exclude_idx;
    (void)force;
#endif
}

/** Set in device_ready — only reboot after a fully working pad disconnects (not failed pair attempts). */
static bool s_bt_slot_was_ready[CONFIG_BLUEPAD32_MAX_DEVICES]{};
/* Custom: "battery low" logged for this connection (Bluepad32/BtPad.h battery_low). */
static bool s_bt_slot_battery_low_logged[CONFIG_BLUEPAD32_MAX_DEVICES]{};

/** Full chip reset after last ready BT pad drops — next pair is always a clean first connect. */
static btstack_timer_source_t s_bt_disconnect_reboot_timer;
static bool s_bt_disconnect_reboot_pending = false;

static void bt_disconnect_reboot_cb(btstack_timer_source_t* ts) {
    (void)ts;
    s_bt_disconnect_reboot_pending = false;
    printf("[BP32] Last controller disconnected — storing the session, then restarting\n");
    /* Custom: in the log, so a reboot by the watchdog fallback (this timer never fired) shows. */
    diag::event(to_ms_since_boot(get_absolute_time()), "last slot gone: reboot timer fired");
    s_store_session_and_reboot.store(true);  // Core0: UserSettings stores it and reboots
}

/** Xbox Series / One S over BLE (HOGP) — in-place reconnect leaves HIDS/bond state bad. */
static bool device_is_xbox_ble(const uni_hid_device_t* d) {
    return d != nullptr && d->controller_type == CONTROLLER_TYPE_XBoxOneController && d->hids_cid != 0;
}

/**
 * Drop LE links that never reached DEVICE_READY (stuck DIS/HIDS after USB suspend).
 * Classic ACL pads are left alone so 8BitDo Android/Switch can reconnect cleanly.
 */
static void drop_incomplete_ble_slots(void) {
#if defined(CONFIG_TARGET_PICO_W)
    for (uint8_t i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(static_cast<int>(i));
        if (!d || d->conn.handle == UNI_BT_CONN_HANDLE_INVALID)
            continue;
        if (uni_bt_conn_get_state(&d->conn) == UNI_BT_CONN_STATE_DEVICE_READY)
            continue;
        if (gap_get_connection_type(d->conn.handle) != GAP_CONNECTION_LE)
            continue;
        printf("[BP32] Dropping incomplete BLE slot %u (stale after suspend)\n",
               static_cast<unsigned>(i));
        uni_hid_device_disconnect(d);
    }
#endif
}

static void restore_bt_pairing_mode(int disconnected_idx);

#if defined(CONFIG_EN_BLUETOOTH) && defined(CONFIG_TARGET_PICO_W)
/** True while a BLE pad is connected but not yet DEVICE_READY (pairing / DIS / HIDS). */
static bool any_ble_connect_in_progress() {
    for (uint8_t i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(static_cast<int>(i));
        if (!d || d->conn.handle == UNI_BT_CONN_HANDLE_INVALID)
            continue;
        if (uni_bt_conn_get_state(&d->conn) == UNI_BT_CONN_STATE_DEVICE_READY)
            continue;
        if (gap_get_connection_type(d->conn.handle) == GAP_CONNECTION_LE)
            return true;
    }
    return false;
}

static bool is_pairing_idle() {
#if defined(CONFIG_EN_USB_HOST)
    if (board_api::usb::host_any_pad_mounted())
        return false;
#endif
    /* Mid HOGP discovery still looks "disconnected" to bt_devices_[], but scans
     * must stay off or Triton's HIDS discovery never finishes. */
    if (any_ble_connect_in_progress())
        return false;
    return !any_connected();
}

/** Restart BR/LE scans when idle; no-op if a pad or wired host controller is active. */
static void ensure_idle_pairing_scans(int disconnected_idx) {
    if (!is_pairing_idle())
        return;
    maybe_restart_bredr_inquiry_after_disconnect(disconnected_idx);
    maybe_restart_ble_scan_after_disconnect(disconnected_idx);
    gap_connectable_control(1);
    if (!uni_bt_is_scanning())
        uni_bt_start_scanning_and_autoconnect_unsafe();
}

static btstack_timer_source_t s_pairing_watchdog_timer;

static void pairing_watchdog_cb(btstack_timer_source_t* ts) {
    ensure_idle_pairing_scans(-1);
    btstack_run_loop_set_timer(ts, PAIRING_WATCHDOG_MS);
    btstack_run_loop_add_timer(ts);
}

static btstack_context_callback_registration_t s_usb_resume_bt_reg;

static void usb_resume_on_bt_main(void* ctx) {
    (void)ctx;
    drop_incomplete_ble_slots();
    if (!is_pairing_idle())
        return;
    printf("[BP32] USB resume — restoring BT pairing scans\n");
    restore_bt_pairing_mode(-1);
}
#endif /* CONFIG_EN_BLUETOOTH && CONFIG_TARGET_PICO_W */

static void restore_bt_pairing_mode(int disconnected_idx) {
    if (led_timer_set_)
        btstack_run_loop_remove_timer(&led_timer_);
    led_timer_set_ = true;
    led_timer_.process = check_led_cb;
    led_timer_.context = nullptr;
    /* Custom fix: no board_api::set_led() here. On Pico W the LED sits on the CYW43 chip, and
     * this runs inside the HCI/L2CAP disconnect event: the LED ioctl deadlocked the BT core
     * when the last pad of a Joy-Con pair disconnected. check_led_cb (timer context) takes
     * over the LED right away. */
    btstack_run_loop_set_timer(&led_timer_, 1);
    btstack_run_loop_add_timer(&led_timer_);

#if defined(CONFIG_EN_BLUETOOTH) && defined(CONFIG_TARGET_PICO_W)
    ensure_idle_pairing_scans(disconnected_idx);
#else
    maybe_restart_bredr_inquiry_after_disconnect(disconnected_idx);
    maybe_restart_ble_scan_after_disconnect(disconnected_idx);
    gap_connectable_control(1);
    uni_bt_start_scanning_and_autoconnect_unsafe();
#endif
}

static void device_disconnected_cb(uni_hid_device_t* device) {
    int idx = uni_hid_device_get_idx_for_instance(device);
    if (idx >= CONFIG_BLUEPAD32_MAX_DEVICES || idx < 0) {
        return;
    }
    {
        /* Custom: the output gamepad starts its idle time again with its next controller. */
        const int out = bt_output_idx(device);
        if (out >= 0 && out < static_cast<int>(MAX_GAMEPADS)) {
            s_idle[out].reset();
        }
    }
    {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        /* Custom: the last ready controller going away ends the session: freeze its summary
         * while the controller is still in it (it is stored before the reboot below). */
        bool others = false;
        for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i)
            others |= i != idx && bt_devices_[i].connected;
        if (!others && s_bt_slot_was_ready[idx])
            diag::session_freeze(now);
        diag::slot_disconnected(static_cast<size_t>(idx), now);
        diag::event(now, "slot %d gone: %s", idx, device->name);
        s_bt_slot_battery_low_logged[idx] = false;
    }

    if (uni_hid_parser_switch2_is_ble_device(device)) {
        OGXM_LOG("SW2: disconnected slot %d\n", idx);
    }
    flydigi_apex4_bt_on_disconnected(device);
    gamesir_cyclone2_bt_on_disconnected(device);

    const bool was_ready = s_bt_slot_was_ready[idx];
    s_bt_slot_was_ready[idx] = false;

    bt_devices_[idx].connected = false;
    bool any_other_connected = false;
    for (uint8_t i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i) {
        if (bt_devices_[i].connected) {
            any_other_connected = true;
            break;
        }
    }
#if defined(CONFIG_TARGET_PICO_W) && defined(CONFIG_EN_USB_HOST)
    s_bt_any_connected_cached.store(any_other_connected, std::memory_order_release);
#endif
    s_last_bt_input_ms[idx] = 0;
    s_ble_keepalive[idx] = bt_pad::KeepaliveState{};
    s_disconnect_combo[idx].reset();
    s_ps4_rumble_ok_ms[idx] = 0;
    s_lightbar_applied[idx] = Lightbar();
    prev_touchpad_clicked_[idx] = false;
    pending_adaptive_trigger_send_[idx] = false;
    /* Never clear PadIn for an OGX slot owned by USB (BT idx often equals USB pad 0). */
    const int out_for_reset = bt_pad::output_idx(device);
    const int pad_for_reset =
        (out_for_reset >= 0 && out_for_reset < static_cast<int>(MAX_GAMEPADS)) ? out_for_reset
        : (idx < static_cast<int>(MAX_GAMEPADS) ? idx : -1);
    if (pad_for_reset >= 0 && !InputSlot::usb_owns(static_cast<uint8_t>(pad_for_reset))) {
        if (pad_for_reset < CONFIG_BLUEPAD32_MAX_DEVICES &&
            bt_devices_[pad_for_reset].gamepad != nullptr) {
            bt_devices_[pad_for_reset].gamepad->reset_pad_in();
            bt_devices_[pad_for_reset].gamepad->set_combo_stick_as_dpad(false);
        }
    } else if (pad_for_reset >= 0) {
#if defined(CONFIG_OGXM_DEBUG)
        printf("[BT DISCONNECT] skip reset_pad_in: OGX slot %d owned by USB\n", pad_for_reset);
#endif
    }
    SteamPassthrough::clear();

    if (feedback_timer_set_ && !any_other_connected) {
        feedback_timer_set_ = false;
        btstack_run_loop_remove_timer(&feedback_timer_);
        hd_rumble_stop();
    }

    apply_scan_policy(idx, true);  // Custom: a slot is free again
    if (any_other_connected)
        return;

    /* Immediately show pairing mode (LED blink + BT scan). */
    restore_bt_pairing_mode(idx);

   /*
	 * Reboot after a controller that previously reached DEVICE_READY disconnects.
	 *
	 * The original behavior intentionally avoided rebooting Classic BT and non-Xbox
	 * BLE devices because, on OG Xbox, the adapter briefly disappearing and
	 * re-enumerating can look like a short freeze.
	 *
	 * We are changing that behavior because some Classic HID controllers can leave
	 * Bluepad32 / BTstack in a stale state after a real disconnect. In that state the
	 * controller may reconnect at the Bluetooth level but never resume usable input
	 * until the adapter itself is power-cycled.
	 *
	 * A brief reboot after a confirmed, previously-ready disconnect is preferable to
	 * requiring the user to physically unplug and reconnect the adapter.
	 *
	 * Failed pairing attempts must NOT reboot. A device that never reached
	 * DEVICE_READY leaves was_ready == false, which allows pairing mode to remain
	 * active instead of entering a reboot loop after every failed connection attempt.
	 */
	if (was_ready && !s_bt_disconnect_reboot_pending) {
		s_bt_disconnect_reboot_pending = true;
		s_bt_disconnect_reboot_timer.process = bt_disconnect_reboot_cb;
		s_bt_disconnect_reboot_timer.context = nullptr;
		btstack_run_loop_set_timer(&s_bt_disconnect_reboot_timer, bt_pad::kLastPadRebootDelayMs);
		btstack_run_loop_add_timer(&s_bt_disconnect_reboot_timer);
		/* Custom fix: the reboot above is a run-loop timer on the BT core, which never fires
		 * if that core hangs while the pads go away (seen with the disconnect combo, and a
		 * suspected freeze after turning a Joy-Con pair off). Arm the hardware watchdog too
		 * (nothing feeds it), like the combo does, so the board always reboots. */
		watchdog_enable(DISCONNECT_REBOOT_WATCHDOG_MS, true);
		printf("[BP32] Pairing mode on — reboot in 500 ms for clean reconnect\n");
		diag::event(to_ms_since_boot(get_absolute_time()), "last slot gone: reboot in %lu ms (watchdog %lu ms)",
		            static_cast<unsigned long>(bt_pad::kLastPadRebootDelayMs),
		            static_cast<unsigned long>(DISCONNECT_REBOOT_WATCHDOG_MS));
	}
}

/* Custom: lightbar colour per output mode (DS4 / DualSense). Deferred like the connect rumble:
 * early BT output reports can drop a freshly connected DS4. A colour requested by the host
 * (PS4 mode) replaces it from the feedback loop (apply_host_lightbar). */
static constexpr uint32_t MODE_LIGHTBAR_DELAY_MS = bt_pad::kModeLightbarDelayMs;
static btstack_timer_source_t s_mode_lightbar_timer[CONFIG_BLUEPAD32_MAX_DEVICES];

static void apply_mode_lightbar(int idx)
{
    uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(idx);
    if (d == nullptr || d->conn.handle == UNI_BT_CONN_HANDLE_INVALID)
        return;
    if (d->controller_type != CONTROLLER_TYPE_PS4Controller &&
        d->controller_type != CONTROLLER_TYPE_PS5Controller)
        return;
    Lightbar mode;
    mode_indicator::shown_color(UserSettings::get_instance().get_current_driver(), mode.r, mode.g, mode.b);
    mode.valid = true;
    s_lightbar_applied[idx] = Lightbar{};  // the mode colour always goes out once
    bt_pad::set_lightbar(d, mode, s_lightbar_applied[idx]);
}

static void mode_lightbar_cb(btstack_timer_source_t* ts)
{
    apply_mode_lightbar(static_cast<int>(reinterpret_cast<intptr_t>(ts->context)));
}

static void schedule_mode_lightbar(uni_hid_device_t* device, int idx)
{
    if (device->controller_type != CONTROLLER_TYPE_PS4Controller &&
        device->controller_type != CONTROLLER_TYPE_PS5Controller)
        return;
    btstack_timer_source_t* ts = &s_mode_lightbar_timer[idx];
    btstack_run_loop_remove_timer(ts);
    ts->process = mode_lightbar_cb;
    ts->context = reinterpret_cast<void*>(static_cast<intptr_t>(idx));
    btstack_run_loop_set_timer(ts, MODE_LIGHTBAR_DELAY_MS);
    btstack_run_loop_add_timer(ts);
}

static uni_error_t device_ready_cb(uni_hid_device_t* device) {
    if (bt_pad::reject_on_ready(device))  // DS4 / DualSense "virtual mouse" slot (Bluepad32/BtPad.h)
        return UNI_ERROR_INVALID_CONTROLLER;

    int idx = uni_hid_device_get_idx_for_instance(device);
    if (idx < 0 || idx >= CONFIG_BLUEPAD32_MAX_DEVICES) {
        return UNI_ERROR_SUCCESS;
    }

    /* Custom: "MAC address per controller" dongle option (PS4 / STEAM pairing info). */
    reported_mac::on_pad_ready(device->conn.btaddr);

    const int out_idx = bt_pad::output_idx(device);
    const int pad_idx = resolve_bt_output_pad_idx(out_idx >= 0 ? out_idx : idx);
    if (pad_idx < 0) {
#if defined(CONFIG_OGXM_DEBUG)
        printf("\n[BT READY REJECTED]\nbt_idx=%d preferred_out=%d — no free OGX slot "
               "(USB occupies all MAX_GAMEPADS=%u)\n",
               idx, out_idx, static_cast<unsigned>(MAX_GAMEPADS));
        log_ogx_slots("REJECT reason: USB ownership");
#endif
        return UNI_ERROR_NO_SLOTS;
    }
#if defined(CONFIG_OGXM_DEBUG)
    if (pad_idx != idx || pad_idx != out_idx) {
        printf("[BT READY] bt_idx=%d out_idx=%d -> OGX pad %d (avoid USB collision)\n",
               idx, out_idx, pad_idx);
    }
#endif

    if (gamesir_cyclone2_bt_is_game_pair_mode(device->name)) {
        gamesir_cyclone2_bt_on_ready(device);
        gamesir_cyclone2_bt_reject_game_pair_mode_if_needed(device);
        return UNI_ERROR_INVALID_CONTROLLER;
    }

    bt_devices_[idx].connected = true;
    {
        /* Custom: diagnostics (Diagnostics/Diagnostics.h). */
        const bool le = gap_get_connection_type(device->conn.handle) == GAP_CONNECTION_LE;
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        diag::slot_connected(static_cast<size_t>(idx), now, device->name, device->vendor_id,
                             device->product_id, static_cast<uint8_t>(device->controller_type), le,
                             device->conn.handle, device->conn.btaddr);
        link_diag::on_slot_ready(idx);
        diag::event(now, "slot %d ready %04x:%04x %s: %s", idx, device->vendor_id,
                    device->product_id, le ? "LE" : "Classic", device->name);
    }
    s_bt_slot_was_ready[idx] = true;
    flydigi_apex4_bt_on_ready(device);
    gamesir_cyclone2_bt_on_ready(device);
#if defined(CONFIG_OGXM_DEBUG)
    if (uni_hid_parser_switch2_is_ble_device(device)) {
        OGXM_LOG("SW2: READY slot %d pid=0x%04x out=%d — input active\n", idx, device->product_id, pad_idx);
    } else if (bt_pad::is_switch_joycon(device)) {
        OGXM_LOG("SW1: READY slot %d Joy-Con out=%d — input active\n", idx, pad_idx);
    }
#endif
#if defined(CONFIG_TARGET_PICO_W) && defined(CONFIG_EN_USB_HOST)
    s_bt_any_connected_cached.store(true, std::memory_order_release);
#endif
#if defined(CONFIG_TARGET_PICO_W)
    if (device_is_ble_hogp(device)) {
        /* CYW43439: BLE scan + BR inquiry during an active LE HOGP link stalls GATT / drops link. */
        uni_bt_le_scan_stop();
        uni_bt_bredr_scan_stop();
        /* BLE advertising: off from the search policy (Bluepad32/ScanControl). */
    } else if (gap_get_connection_type(device->conn.handle) == GAP_CONNECTION_ACL) {
        /* BLE advertising follows the search policy (Bluepad32/ScanControl). */
        if (uni_hid_parser_switch_solo_needs_partner(device)) {
            /* Solo Joy-Con (L or R): keep inquiry + page scan for the partner; pause BLE scan. */
            uni_bt_le_scan_stop();
            gap_connectable_control(1);
            uni_bt_bredr_scan_start();
        } else if (!uni_hid_parser_switch_any_awaiting_partner()) {
            uni_bt_bredr_scan_stop();
        }
    }
#endif
    apply_scan_policy(-1, true);  // Custom: a pad is ready
    const uint32_t tnow = to_ms_since_boot(get_absolute_time());
    /* pad_idx already resolved above to avoid USB-owned OGX slots. */
    s_last_bt_input_ms[pad_idx] = tnow;
    s_disconnect_combo[pad_idx].reset(tnow + bt_pad::kDisconnectComboGraceMs);
    if (device->controller_type == CONTROLLER_TYPE_PS4Controller)
        s_ps4_rumble_ok_ms[pad_idx] = tnow + bt_pad::host_rumble_grace_ms(device);
    s_ble_keepalive[idx] = bt_pad::KeepaliveState{};  // first keepalive on the next feedback tick

    /* Player LED, Wiimote held upright, DualSense adaptive triggers off (Bluepad32/BtPad.h). */
    bt_pad::setup_on_ready(device, static_cast<uint8_t>(pad_idx), MotionOutputActive::is_enabled());
    adaptive_trigger_enabled_[pad_idx] = false;  // the touchpad press toggles them

    /* Custom: the LED timer keeps running: check_led_cb shows the search state (solid once the
     * search stops, after the boot mode blink code). */
    schedule_mode_lightbar(device, idx);
    if (!feedback_timer_set_) {
        feedback_timer_set_ = true;
        feedback_timer_.process = send_feedback_cb;
        feedback_timer_.context = nullptr;
        btstack_run_loop_set_timer(&feedback_timer_, FEEDBACK_TIME_MS);
        btstack_run_loop_add_timer(&feedback_timer_);
    }
    hd_rumble_start();

    bt_pad::play_connection_rumble(device);

    return UNI_ERROR_SUCCESS;
}

static void oob_event_cb(uni_platform_oob_event_t event, void* data) {
	return;
}

// Set to 1 to print all Bluepad32 controller inputs to UART (only when state changes)
#ifndef BLUEPAD32_UART_LOG_INPUT
#if defined(CONFIG_OGXM_DEBUG)
#define BLUEPAD32_UART_LOG_INPUT 1
#else
#define BLUEPAD32_UART_LOG_INPUT 0
#endif
#endif

static void controller_data_cb(uni_hid_device_t* device, uni_controller_t* controller) {
    static uni_gamepad_t prev_uni_gp[MAX_GAMEPADS] = {};

    if (controller->klass != UNI_CONTROLLER_CLASS_GAMEPAD){
        return;
    }

    uni_gamepad_t *uni_gp = &controller->gamepad;
    const int bt_slot = uni_hid_device_get_idx_for_instance(device);
    if (bt_slot >= 0) {  /* Custom: battery for the web app's diagnostics (timing: uni_diag_on_input_report) */
        if (controller->battery != UNI_CONTROLLER_BATTERY_NOT_AVAILABLE)
            diag::slot_battery(static_cast<size_t>(bt_slot), controller->battery);
        if (bt_slot < CONFIG_BLUEPAD32_MAX_DEVICES && bt_pad::battery_low(controller->battery) &&
            !s_bt_slot_battery_low_logged[bt_slot]) {
            s_bt_slot_battery_low_logged[bt_slot] = true;
            diag::event(to_ms_since_boot(get_absolute_time()), "slot %d battery low (%u/255)", bt_slot,
                        static_cast<unsigned>(controller->battery));
        }
    }
    int idx = bt_pad::output_idx(device);
    if (idx < 0)
        idx = bt_slot;
    idx = resolve_bt_output_pad_idx(idx);
    if (idx < 0)
        return;
    {
        const uint32_t now_cb = to_ms_since_boot(get_absolute_time());
        idle_note_input(idx, uni_gp, now_cb);  // Custom: idle turn-off
        s_last_bt_input_ms[static_cast<unsigned>(idx)] = now_cb;
        if (bt_slot >= 0 && bt_slot < CONFIG_BLUEPAD32_MAX_DEVICES && bt_slot != idx)
            s_last_bt_input_ms[static_cast<unsigned>(bt_slot)] = now_cb;
    }

    /* USB-owned OGX pad must not receive Bluetooth PadIn (MAX_GAMEPADS=1 collision). */
    if (InputSlot::usb_owns(static_cast<uint8_t>(idx))) {
        return;
    }

#if defined(CONFIG_OGXM_DEBUG)
    if (flydigi_apex4_bt_parser_installed(device)) {
        static uint32_t s_apex_plat_log_ms[MAX_GAMEPADS]{};
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        if (idx >= 0 && idx < static_cast<int>(MAX_GAMEPADS) &&
            (now - s_apex_plat_log_ms[idx] >= 500u ||
             (uni_gp->buttons | uni_gp->misc_buttons | uni_gp->dpad) != 0)) {
            if (now - s_apex_plat_log_ms[idx] >= 250u) {
                s_apex_plat_log_ms[idx] = now;
                OGXM_LOG("[3] BLUEPAD PLATFORM CALLBACK idx=%d buttons=0x%04x misc=0x%02x "
                         "Lx=%ld Ly=%ld Rx=%ld Ry=%ld\n",
                         idx, uni_gp->buttons, uni_gp->misc_buttons,
                         static_cast<long>(uni_gp->axis_x), static_cast<long>(uni_gp->axis_y),
                         static_cast<long>(uni_gp->axis_rx), static_cast<long>(uni_gp->axis_ry));
            }
        }
    }
#endif

#if BLUEPAD32_UART_LOG_INPUT
    {
        bool changed = std::memcmp(uni_gp, &prev_uni_gp[idx], sizeof(uni_gamepad_t)) != 0;
        if (changed && device->controller_type != CONTROLLER_TYPE_Switch2ProController) {
            printf("[BP32 idx=%d] dpad=0x%02x btns=0x%04x misc=0x%02x brake=%u throttle=%u "
                   "Lx=%d Ly=%d Rx=%d Ry=%d\n",
                   idx, (unsigned)uni_gp->dpad, (unsigned)uni_gp->buttons, (unsigned)uni_gp->misc_buttons,
                   (unsigned)uni_gp->brake, (unsigned)uni_gp->throttle,
                   (int)uni_gp->axis_x, (int)uni_gp->axis_y, (int)uni_gp->axis_rx, (int)uni_gp->axis_ry);
        }
    }
#endif

    Gamepad* gamepad = bt_devices_[idx].gamepad;

    /* Disconnect combo (Bluepad32/BtPad.h): Start + Select, L3 + R3 on an OUYA pad. On Pico W,
     * Start + Select is not used for input-source cycling. */
    const uint32_t now_cb = to_ms_since_boot(get_absolute_time());
    if (s_disconnect_combo[idx].update(bt_pad::disconnect_combo_pressed(device, uni_gp), now_cb,
                                       bt_pad::kDisconnectComboHoldMs)) {
        printf("[BP32] Disconnect combo detected, disconnecting controller %d\n", idx);
        schedule_disconnect_combo(idx);
        return; // Don't process further input after disconnect
    }

    /* Buttons, triggers, sticks, touchpad, motion and battery (Bluepad32/BtPadIn.h, shared with
     * the fixture tests). */
    static MotionImu::WiiPseudoGyroState s_wii_gyro[MAX_GAMEPADS]{};
    Gamepad::PadIn gp_in = bt_pad::pad_in_from(device, uni_gp, *gamepad, s_wii_gyro[idx],
                                               to_ms_since_boot(get_absolute_time()));
    MotionImu::report_motion(gp_in, to_ms_since_boot(get_absolute_time()));

    if (SteamActive::is_enabled()) {
        SteamPassthrough::input_has_touchpad =
            (device->controller_type == CONTROLLER_TYPE_PS5Controller);
        SteamBtReport::store_ps5_core(bt_pad::ps5_core_from(device, uni_gp));  // Bluepad32/BtPad.h, shared
        if (device->controller_type == CONTROLLER_TYPE_PS5Controller)
            SteamTouchpad::apply_to_passthrough(gp_in.touch_raw, gp_in.touchpad_click != 0);
    }

    /* Custom: a lone Joy-Con has no D-pad, so mode combos use its stick instead. */
    gamepad->set_combo_stick_as_dpad(bt_pad::is_switch_joycon(device) &&
                                     bt_pad::pair_partner_idx(device) < 0);
    gamepad->set_pad_in_from_bluetooth(gp_in);

#if defined(CONFIG_OGXM_DEBUG)
    if (flydigi_apex4_bt_parser_installed(device)) {
        static uint32_t s_apex_ogx_log_ms[MAX_GAMEPADS]{};
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        if (idx >= 0 && idx < static_cast<int>(MAX_GAMEPADS) && now - s_apex_ogx_log_ms[idx] >= 500u) {
            s_apex_ogx_log_ms[idx] = now;
            OGXM_LOG("[4] OGX INPUT idx=%d buttons=0x%04x dpad=0x%02x lx=%d ly=%d rx=%d ry=%d\n",
                     idx, gp_in.buttons, gp_in.dpad,
                     static_cast<int>(gp_in.joystick_lx), static_cast<int>(gp_in.joystick_ly),
                     static_cast<int>(gp_in.joystick_rx), static_cast<int>(gp_in.joystick_ry));
        }
    }
#endif

#if BLUEPAD32_UART_LOG_INPUT
    if (idx >= 0 && idx < static_cast<int>(MAX_GAMEPADS) &&
        device->controller_type != CONTROLLER_TYPE_Switch2ProController) {
        std::memcpy(&prev_uni_gp[idx], uni_gp, sizeof(uni_gamepad_t));
    }
#endif

    // PS5: defer adaptive trigger send to main loop so callback never does l2cap_send (reduces input lag)
    if (device->controller_type == CONTROLLER_TYPE_PS5Controller && idx >= 0 && idx < static_cast<int>(MAX_GAMEPADS)) {
        bool touchpad_clicked = (uni_gp->misc_buttons & MISC_BUTTON_CAPTURE) != 0;
        if (touchpad_clicked && !prev_touchpad_clicked_[idx]) {
            adaptive_trigger_enabled_[idx] = !adaptive_trigger_enabled_[idx];
            pending_adaptive_trigger_send_[idx] = true;
        }
        prev_touchpad_clicked_[idx] = touchpad_clicked;
    }
}

const uni_property_t* get_property_cb(uni_property_idx_t idx) 
{
    return nullptr;
}

void process_pending_adaptive_triggers()
{
    for (uint8_t i = 0; i < MAX_GAMEPADS; ++i) {
        if (!pending_adaptive_trigger_send_[i])
            continue;
        pending_adaptive_trigger_send_[i] = false;
        uni_hid_device_t* device = uni_hid_device_get_instance_for_idx(static_cast<int>(i));
        if (!device || device->controller_type != CONTROLLER_TYPE_PS5Controller)
            continue;
        if (adaptive_trigger_enabled_[i]) {
            ds5_adaptive_trigger_effect_t on = ds5_new_adaptive_trigger_effect_feedback(5, 4);
            ds5_set_adaptive_trigger_effect(device, UNI_ADAPTIVE_TRIGGER_TYPE_LEFT, &on);
            ds5_set_adaptive_trigger_effect(device, UNI_ADAPTIVE_TRIGGER_TYPE_RIGHT, &on);
        } else {
            ds5_adaptive_trigger_effect_t off = ds5_new_adaptive_trigger_effect_off();
            ds5_set_adaptive_trigger_effect(device, UNI_ADAPTIVE_TRIGGER_TYPE_LEFT, &off);
            ds5_set_adaptive_trigger_effect(device, UNI_ADAPTIVE_TRIGGER_TYPE_RIGHT, &off);
        }
    }
}

uni_platform* get_driver() 
{
    static uni_platform driver = 
    {
        .name = "OGXMiniW",
        .init = init,
        .on_init_complete = init_complete_cb,
        .on_device_discovered = device_discovered_cb,
        .on_device_connected = device_connected_cb,
        .on_device_disconnected = device_disconnected_cb,
        .on_device_ready = device_ready_cb,
        .on_controller_data = controller_data_cb,
        .get_property = get_property_cb,
        .on_oob_event = oob_event_cb,
    };
    return &driver;
}

//Public API

void set_gpio_device_process_callback(void (*callback)(void* ctx), void* ctx) {
    gpio_process_cb_ = callback;
    gpio_process_ctx_ = ctx;
}

void set_pico_w_pio_usb_mux_tick(void (*tick_cb)(void)) {
    s_pico_w_pio_usb_mux_tick = tick_cb;
}


/* Custom: after a mode-change reboot the BT stack needs ~9 s to come back. Joy-Cons that just
 * lost their link usually stop paging before that and hang with the LED on until power-cycled.
 * Disconnecting them first makes them sleep cleanly and reconnect on a button press. Other pads
 * are disconnected too: they turn off at once (LED off, a visible sign the mode changed)
 * instead of paging a rebooting adapter. */
static btstack_timer_source_t s_reboot_disc_poll_timer;
/* gap_disconnect() on a live Joy-Con link never completed (links still open after 2 s), so
 * first ask a Joy-Con to disconnect itself and sleep; fall back to gap_disconnect() later.
 * Other pads get gap_disconnect() right away. */
static constexpr uint32_t REBOOT_DISC_FALLBACK_MS = 1000;
static uint32_t s_reboot_disc_started_ms = 0;
static bool s_reboot_disc_fallback_done = false;
static hci_con_handle_t s_reboot_disc_handles[CONFIG_BLUEPAD32_MAX_DEVICES];
static uint8_t s_reboot_disc_count = 0;
/* Set on the BT core once every pad ACL link is really gone (HCI disconnection complete). */
static std::atomic<bool> s_reboot_disc_done{false};

static void reboot_disc_poll_cb(btstack_timer_source_t* ts)
{
    /* Poll BTstack's own connection table with the saved handles (gap_disconnect() would
     * invalidate d->conn.handle immediately) until the controller reports them closed. */
    bool open = false;
    for (uint8_t i = 0; i < s_reboot_disc_count; ++i) {
        if (gap_get_connection_type(s_reboot_disc_handles[i]) != GAP_CONNECTION_INVALID)
            open = true;
    }
    if (open) {
        if (!s_reboot_disc_fallback_done &&
            btstack_run_loop_get_time_ms() - s_reboot_disc_started_ms >= REBOOT_DISC_FALLBACK_MS) {
            s_reboot_disc_fallback_done = true;
            printf("[BP32] Mode change: pad still linked, falling back to gap_disconnect\n");
            for (uint8_t i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i) {
                uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
                if (d && d->conn.handle != UNI_BT_CONN_HANDLE_INVALID && !uni_hid_device_is_virtual_device(d))
                    uni_hid_device_disconnect(d);
            }
        }
        btstack_run_loop_set_timer(ts, 20);
        btstack_run_loop_add_timer(ts);
        return;
    }
    printf("[BP32] Mode change: pad links closed\n");
    s_reboot_disc_done.store(true, std::memory_order_release);
}

/* Custom fix: Core0 only raises this flag; the BT core picks it up from its own timer. Core0 used
 * to queue the work with btstack_run_loop_execute_on_main_thread(), which takes the BT stack's
 * lock from the other core: now and then that call never returned and the mode change froze
 * with USB still up (the mode was not saved, a key stayed held in mouse + keyboard mode). */
static std::atomic<bool> s_reboot_disc_request{false};
static btstack_timer_source_t s_reboot_disc_request_timer;
static constexpr uint32_t REBOOT_DISC_REQUEST_POLL_MS = 20;

static void reboot_disconnect_pads_on_bt_main()
{
    s_reboot_disc_count = 0;
    for (uint8_t i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (!d || d->conn.handle == UNI_BT_CONN_HANDLE_INVALID || uni_hid_device_is_virtual_device(d))
            continue;
        s_reboot_disc_handles[s_reboot_disc_count++] = d->conn.handle;
        if (bt_pad::is_switch_joycon(d)) {
            printf("[BP32] Mode change: asking Joy-Con slot %u to sleep before reboot\n", i);
            uni_hid_parser_switch_request_sleep(d);
        } else {
            printf("[BP32] Mode change: disconnecting pad slot %u before reboot\n", i);
            uni_hid_device_disconnect(d);
        }
    }
    s_reboot_disc_started_ms = btstack_run_loop_get_time_ms();
    s_reboot_disc_fallback_done = false;
    s_reboot_disc_poll_timer.process = reboot_disc_poll_cb;
    s_reboot_disc_poll_timer.context = nullptr;
    btstack_run_loop_set_timer(&s_reboot_disc_poll_timer, 20);
    btstack_run_loop_add_timer(&s_reboot_disc_poll_timer);
}

static void reboot_disc_request_timer_cb(btstack_timer_source_t* ts)
{
    if (s_reboot_disc_request.exchange(false, std::memory_order_acq_rel)) {
        reboot_disconnect_pads_on_bt_main();
        return;  // one shot: the board reboots next
    }
    btstack_run_loop_set_timer(ts, REBOOT_DISC_REQUEST_POLL_MS);
    btstack_run_loop_add_timer(ts);
}

/* Read from Core0 while Core1 owns the table: only used to skip the wait when idle. */
static bool any_pad_link_open()
{
    for (uint8_t i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (d && d->conn.handle != UNI_BT_CONN_HANDLE_INVALID && !uni_hid_device_is_virtual_device(d))
            return true;
    }
    return false;
}

void disconnect_pads_before_reboot()
{
    static constexpr uint32_t LINK_DOWN_TIMEOUT_MS = 2000;

    if (!s_btstack_run_loop_ready.load(std::memory_order_acquire) || !any_pad_link_open())
        return;

    s_reboot_disc_done.store(false, std::memory_order_release);
    s_reboot_disc_request.store(true, std::memory_order_release);

    const uint32_t start = board_api::ms_since_boot();
    while (!s_reboot_disc_done.load(std::memory_order_acquire) &&
           board_api::ms_since_boot() - start < LINK_DOWN_TIMEOUT_MS)
        sleep_ms(10);
    if (!s_reboot_disc_done.load(std::memory_order_acquire))
        printf("[BP32] Mode change: pad links still open after %lu ms, rebooting anyway\n",
               static_cast<unsigned long>(LINK_DOWN_TIMEOUT_MS));
}

void wired_usb_takeover_disconnect_bt() {
#if defined(CONFIG_TARGET_PICO_W) && defined(CONFIG_EN_USB_HOST)
    /* Core0 mux uses this atomic; disconnect callbacks run async on Core1. Clear immediately so
     * we do not treat BT as active and tuh_deinit() wired USB during the disconnect window. */
    s_bt_any_connected_cached.store(false, std::memory_order_release);
#endif
    s_bt_quiet_for_usb_pending.store(true, std::memory_order_release);
    if (!s_btstack_run_loop_ready.load(std::memory_order_acquire)) {
        /* Pad plugged during CYW43/uni_init — defer until run loop exists (avoids btstack_assert). */
        return;
    }
    for (uint8_t i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i) {
        uni_bt_disconnect_device_safe(i);
    }
    uni_bt_stop_scanning_safe();
}

/* Custom: the reboot after the last controller disconnects, asked for by the BT core. */
bool take_store_session_and_reboot() {
    return s_store_session_and_reboot.exchange(false);
}

/* Custom: Start + L3 — stop searching for new controllers until a pad goes away (Core0). */
void request_stop_search() {
    scan_control::request_stop();
}

void wired_usb_release_enable_bt_pairing() {
    s_bt_quiet_for_usb_pending.store(false, std::memory_order_release);
    if (!s_btstack_run_loop_ready.load(std::memory_order_acquire)) {
        return;
    }
#if defined(CONFIG_EN_BLUETOOTH) && defined(CONFIG_TARGET_PICO_W)
    s_usb_resume_bt_reg.callback = usb_resume_on_bt_main;
    s_usb_resume_bt_reg.context = nullptr;
    btstack_run_loop_execute_on_main_thread(&s_usb_resume_bt_reg);
#else
    uni_bt_start_scanning_and_autoconnect_safe();
#endif
}

void on_usb_device_resume() {
    if (!s_btstack_run_loop_ready.load(std::memory_order_acquire)) {
        return;
    }
#if defined(CONFIG_EN_BLUETOOTH) && defined(CONFIG_TARGET_PICO_W)
    s_usb_resume_bt_reg.callback = usb_resume_on_bt_main;
    s_usb_resume_bt_reg.context = nullptr;
    btstack_run_loop_execute_on_main_thread(&s_usb_resume_bt_reg);
#endif
}

void init(Gamepad(&gamepads)[MAX_GAMEPADS])
{
    for (uint8_t i = 0; i < MAX_GAMEPADS; ++i)
    {
        bt_devices_[i].gamepad = &gamepads[i];
    }

    uni_platform_set_custom(get_driver());
    uni_init(0, nullptr);
    link_diag::init();
    /* Custom: which half of a merged Joy-Con pair provides motion. */
    uni_hid_parser_switch_set_pair_imu_side(joycon_settings::get().pair_imu_right);
    /* Custom: a lone Joy-Con waits for its other half (the search times decide how hard the
     * adapter looks for it; a known partner always reconnects). */
    uni_hid_parser_switch_set_joycon_pairing(true);

    mode_indicator::begin(UserSettings::get_instance().get_current_driver());
    led_timer_set_ = true;
    led_timer_.process = check_led_cb;
    led_timer_.context = nullptr;
    btstack_run_loop_set_timer(&led_timer_, LED_CHECK_TIME_MS);
    btstack_run_loop_add_timer(&led_timer_);

    if (gpio_process_cb_ != nullptr) {
        gpio_process_timer_.process = gpio_process_timer_cb;
        gpio_process_timer_.context = nullptr;
        btstack_run_loop_set_timer(&gpio_process_timer_, GPIO_PROCESS_INTERVAL_MS);
        btstack_run_loop_add_timer(&gpio_process_timer_);
    }

    if (s_pico_w_pio_usb_mux_tick != nullptr) {
        s_pico_w_usb_mux_timer_.process = pico_w_usb_mux_timer_cb;
        s_pico_w_usb_mux_timer_.context = nullptr;
        btstack_run_loop_set_timer(&s_pico_w_usb_mux_timer_, 1);
        btstack_run_loop_add_timer(&s_pico_w_usb_mux_timer_);
    }

    s_reboot_disc_request_timer.process = reboot_disc_request_timer_cb;
    s_reboot_disc_request_timer.context = nullptr;
    btstack_run_loop_set_timer(&s_reboot_disc_request_timer, REBOOT_DISC_REQUEST_POLL_MS);
    btstack_run_loop_add_timer(&s_reboot_disc_request_timer);

#if defined(CONFIG_EN_BLUETOOTH) && defined(CONFIG_TARGET_PICO_W)
    s_pairing_watchdog_timer.process = pairing_watchdog_cb;
    s_pairing_watchdog_timer.context = nullptr;
    btstack_run_loop_set_timer(&s_pairing_watchdog_timer, PAIRING_WATCHDOG_MS);
    btstack_run_loop_add_timer(&s_pairing_watchdog_timer);
#endif
}

void run_task(Gamepad(&gamepads)[MAX_GAMEPADS])
{
    init(gamepads);
    /* uni_init has installed the run loop — safe for Core0 USB mux BT calls. */
    s_btstack_run_loop_ready.store(true, std::memory_order_release);
    if (s_bt_quiet_for_usb_pending.load(std::memory_order_acquire)) {
        for (uint8_t i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; ++i) {
            uni_bt_disconnect_device_safe(i);
        }
        uni_bt_stop_scanning_safe();
        OGXM_LOG("BT: applied deferred USB quiet (pad was plugged during BT bring-up)\n");
    }
    /* Custom: back to btstack_run_loop_execute() (upstream #146 polled the run loop every 2 ms).
     * The lost wake-up it worked around is the Pico SDK's: its async context left alarm_pending
     * set when it cancelled the alarm with no timer left, so the next timer was never armed and
     * BTstack timers ran only on incoming BT traffic (an idle Xbox Series pad's 1 s connect rumble
     * ran until a button press). Fixed at the source by pico_sdk_async_context_alarm_pending.diff
     * (pico-sdk #2838). */
    btstack_run_loop_execute();
}

} // namespace bluepad32 