#ifndef _OGXM_CUSTOM_SCAN_POLICY_H_
#define _OGXM_CUSTOM_SCAN_POLICY_H_

/*  How hard the adapter searches for new Bluetooth controllers while others are connected
 *  (custom addition, not upstream).
 *
 *  The search shares the one CYW43 radio with the connected pads. Bluepad32 keeps a BLE scan at
 *  100% duty cycle (window = interval) while a Bluetooth Classic pad is connected: a DS4 then
 *  delivered 1 report in 8 (about 30 of 250 per second, in bursts 66 ms apart); with the scan
 *  off it delivered all 250, 4 ms apart. So:
 *   - Full: no pad connected, or a slot opened less than a minute ago: Bluepad32's normal search
 *     (BLE scan at 100%; periodic BR/EDR inquiry 3.84 s in every 5-6 s, about 70% of the time);
 *   - Reduced: a slot still open after a minute (Adapter Options: 1-255 s) — a lone Joy-Con waiting for its other half, or
 *     free slots in builds with MAX_GAMEPADS > 1: BLE scan and BR/EDR inquiry at about 10%;
 *   - Off: every output slot in use (a Joy-Con pair is one): no inquiry, no BLE scan.
 *  Page scan always stays on: a pad that knows the adapter reconnects at once whatever the search
 *  does; only pairing a new controller can take longer once the search is reduced.
 */
namespace scan_policy {

    enum class Scan { Full, Reduced, Off };

    // LE scan timing in 0.625 ms units (BTstack gap_set_scan_parameters).
    constexpr unsigned kFullInterval = 48, kFullWindow = 48;        // Bluepad32's default, 100%
    constexpr unsigned kReducedInterval = 480, kReducedWindow = 48; // 10%
    // Periodic inquiry, 1.28 s units: Bluepad32's 3 in every 4-5, reduced to 3 in every 30-31.
    constexpr int kInquiryLength = 3;
    constexpr int kFullMinPeriod = 4, kFullMaxPeriod = 5;
    constexpr int kReducedMinPeriod = 30, kReducedMaxPeriod = 31;
    // How long an open slot keeps the full search by default (Adapter Options can change it).
    constexpr unsigned kFullSearchMs = 60000;

    // ms_open: how long a slot has been open with pads connected (a lone Joy-Con, free slots).
    inline Scan decide(int outputs_in_use, int max_gamepads, bool joycon_awaiting_partner, unsigned ms_open,
                       unsigned full_search_ms = kFullSearchMs)
    {
        if (!joycon_awaiting_partner) {
            if (outputs_in_use <= 0)
                return Scan::Full;
            if (outputs_in_use >= max_gamepads)
                return Scan::Off;
        }
        return ms_open < full_search_ms ? Scan::Full : Scan::Reduced;
    }

} // namespace scan_policy

#endif // _OGXM_CUSTOM_SCAN_POLICY_H_
