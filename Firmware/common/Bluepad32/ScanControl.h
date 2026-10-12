#ifndef _OGXM_SCAN_CONTROL_H_
#define _OGXM_SCAN_CONTROL_H_

#include <cstdint>

#include "Bluepad32/ScanPolicy.h"
#include "UserSettings/DongleSettings.h"

/* Applies the search policy (Bluepad32/ScanPolicy.h) to Bluepad32 / BTstack, for every board that
 * runs Bluepad32. Called from the BTstack run loop on every pad change and every feedback tick. */
namespace scan_control {

    /* The connected pads, as the platform sees them. */
    struct Pads
    {
        int outputs{0};        // output slots in use (a Joy-Con pair is one)
        bool classic{false};   // a Bluetooth Classic pad is connected
        bool hogp{false};      // a BLE (HOGP) pad is connected
        bool awaiting{false};  // a lone Joy-Con waits for its other half
    };

    /* Diagnostics / log line for a change of the search. */
    using EventFn = void (*)(uint32_t now_ms, const char* text);

    /* Start + L3 on the controller: stop the search until a pad goes away. Any thread / core. */
    void request_stop();

    /* pad_leaving: a pad is going away (a slot opens again, a stop from the controller ends).
     * force: apply even if the state did not change (every pad change). start_when_empty: with no
     * pad, start the search here (the ESP32); the Pico W's pairing paths start it themselves. */
    void apply(const Pads& pads, int max_gamepads, bool pad_leaving, bool force, uint32_t now_ms,
               const dongle_settings::Settings& settings, bool start_when_empty, EventFn event);

    /* The reduced search is running (the LED blinks slowly). */
    bool reduced();

} // namespace scan_control

#endif // _OGXM_SCAN_CONTROL_H_
