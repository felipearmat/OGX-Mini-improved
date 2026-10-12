#include "Bluepad32/ScanControl.h"

#include <atomic>
#include <cstdio>

#include "uni.h"
#include "bt/uni_bt.h"
#include "bt/uni_bt_bredr.h"
#include "bt/uni_bt_le.h"

namespace scan_control {

static scan_policy::Scan s_scan_state = scan_policy::Scan::Full;
static std::atomic<bool> s_stop_request{false};

/* Restart the periodic inquiry with new timing. gap_inquiry_stop() only asks the controller to
 * leave periodic inquiry; gap_inquiry_periodic_start() refuses (COMMAND_DISALLOWED) until that is
 * done, so the restart is retried from a timer until BTstack accepts it. */
static btstack_timer_source_t s_inquiry_restart_timer;
static uint8_t s_inquiry_restart_tries = 0;
static uint16_t s_inquiry_min = scan_policy::kFullMinPeriod, s_inquiry_max = scan_policy::kFullMaxPeriod;

static void inquiry_restart_cb(btstack_timer_source_t* ts)
{
    /* The search may have been turned off meanwhile (the other Joy-Con paired). */
    if (s_scan_state == scan_policy::Scan::Off || !uni_bt_enable_new_connections_is_enabled())
        return;
    const uint8_t status = gap_inquiry_periodic_start(scan_policy::kInquiryLength, s_inquiry_max, s_inquiry_min);
    if (status == ERROR_CODE_COMMAND_DISALLOWED && ++s_inquiry_restart_tries < 40) {
        btstack_run_loop_set_timer(ts, 50);
        btstack_run_loop_add_timer(ts);
    }
}

static void restart_inquiry(uint16_t min_period, uint16_t max_period)
{
    s_inquiry_min = min_period;
    s_inquiry_max = max_period;
    uni_bt_set_gap_min_peridic_length(min_period);  // later starts by Bluepad32 use it too
    uni_bt_set_gap_max_peridic_length(max_period);
    gap_inquiry_stop();
    btstack_run_loop_remove_timer(&s_inquiry_restart_timer);
    s_inquiry_restart_tries = 0;
    s_inquiry_restart_timer.process = inquiry_restart_cb;
    btstack_run_loop_set_timer(&s_inquiry_restart_timer, 20);
    btstack_run_loop_add_timer(&s_inquiry_restart_timer);
}

void request_stop()
{
    s_stop_request.store(true);
}

bool reduced()
{
    return s_scan_state == scan_policy::Scan::Reduced;
}

void apply(const Pads& pads, int max_gamepads, bool pad_leaving, bool force, uint32_t now,
           const dongle_settings::Settings& settings, bool start_when_empty, EventFn event)
{
    char text[64];
    const int outputs = pads.outputs;
    const bool awaiting = pads.awaiting;

    /* How long a slot has been open with pads connected (a lone Joy-Con, free slots). A pad going
     * away opens a slot again: the count restarts and a stop from the controller ends. */
    static bool s_open = false;
    static uint32_t s_open_since = 0;
    static bool s_stopped = false;
    if (pad_leaving) {
        s_open = false;
        s_stopped = false;
    }
    const bool open = awaiting || (outputs > 0 && outputs < max_gamepads);
    if (open && !s_open) {
        s_open_since = now;
        s_stopped = false;
    }
    s_open = open;
    if (s_stop_request.exchange(false) && (open || outputs > 0)) {
        s_stopped = true;
        event(now, "search for new controllers stopped from the controller");
    }

    const uint16_t reduced_s = dongle_settings::reduced_search_s(settings);
    const scan_policy::Times times{
        1000u * dongle_settings::full_search_s(settings),
        reduced_s == dongle_settings::kSearchNoLimit ? scan_policy::kNoLimit : 1000u * reduced_s};
    const scan_policy::Scan want =
        scan_policy::decide(outputs, max_gamepads, awaiting, open ? now - s_open_since : 0, times, s_stopped);

    /* Bluepad32 turns the search back on by itself for a lone Joy-Con: an Off state is applied
     * again when that happened, and on every pad change (force). */
    const bool off_undone = want == scan_policy::Scan::Off && uni_bt_enable_new_connections_is_enabled();
    if (want == s_scan_state && !force && !off_undone)
        return;
    const scan_policy::Scan previous = s_scan_state;
    s_scan_state = want;

    switch (want) {
        case scan_policy::Scan::Off:
            uni_bt_enable_new_connections_unsafe(false);  // stops BR/EDR inquiry and the BLE scan
            uni_bt_bredr_scan_stop();                     // also when started outside that flag
            uni_bt_le_scan_stop();
            if (previous != scan_policy::Scan::Off) {
                if (!open) {
                    std::snprintf(text, sizeof(text), "search for new controllers off: all %d slot(s) in use", outputs);
                    event(now, text);
                } else if (!s_stopped) {
                    event(now, "search for new controllers off: search time over");
                }
            }
            break;
        case scan_policy::Scan::Reduced:
        case scan_policy::Scan::Full: {
            const bool is_reduced = want == scan_policy::Scan::Reduced;
            gap_set_scan_parameters(0, is_reduced ? scan_policy::kReducedInterval : scan_policy::kFullInterval,
                                    is_reduced ? scan_policy::kReducedWindow : scan_policy::kFullWindow);
            const uint16_t min_period = is_reduced ? scan_policy::kReducedMinPeriod : scan_policy::kFullMinPeriod;
            const uint16_t max_period = is_reduced ? scan_policy::kReducedMaxPeriod : scan_policy::kFullMaxPeriod;
            if (outputs <= 0 && !awaiting) {
                /* No pad: the pairing paths run the search (the platform's, or here). */
                uni_bt_set_gap_min_peridic_length(min_period);
                uni_bt_set_gap_max_peridic_length(max_period);
                if (start_when_empty && !uni_bt_enable_new_connections_is_enabled())
                    uni_bt_enable_new_connections_unsafe(true);
                break;
            }
            if (!uni_bt_enable_new_connections_is_enabled())
                uni_bt_enable_new_connections_unsafe(true);  // starts BR/EDR inquiry and the BLE scan
            if (awaiting) {
                /* Lone Joy-Con: BLE scan off (as its own path does), inquiry with this timing. */
                uni_bt_le_scan_stop();
                if (force || previous != want)
                    restart_inquiry(min_period, max_period);
            } else {
                uni_bt_set_gap_min_peridic_length(min_period);
                uni_bt_set_gap_max_peridic_length(max_period);
                uni_bt_le_scan_stop();  // new parameters apply when the scan starts
                if (!pads.hogp)
                    uni_bt_le_scan_start();
                if (pads.classic || pads.hogp)
                    uni_bt_bredr_scan_stop();  // as device_ready does with a pad connected
            }
            if (previous != want)
                event(now, is_reduced ? "search for new controllers reduced" : "search for new controllers full");
            break;
        }
    }
}

} // namespace scan_control
