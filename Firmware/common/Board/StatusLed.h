#ifndef _OGXM_STATUS_LED_H_
#define _OGXM_STATUS_LED_H_

#include <cstdint>

/*  The adapter's status LED, the same on every board that has one (custom addition, not
 *  upstream): the output mode's blink code after boot (Board/ModeIndicator.h), then blinking
 *  while the adapter searches for new controllers (fast = full search, slow = reduced search,
 *  Bluepad32/ScanPolicy.h) and solid when it does not (every slot in use, search times used up or
 *  stopped from the controller, a wired controller in use).
 *
 *  Only the on / off state and its timing live here; each board writes it to its own LEDs (Pico
 *  W onboard LED, external WS2812 on a Pico 2 W or on the hybrid's Pico 2 or ESP32, ESP32 GPIO
 *  LED) in the output mode's colour (mode_indicator::led_color).
 */
namespace status_led {

    enum class Search : uint8_t { NONE, FULL, REDUCED };

    static constexpr uint32_t kFastBlinkMs = 250;   // full search for new controllers
    static constexpr uint32_t kSlowBlinkMs = 1000;  // reduced search

    class Blinker
    {
    public:
        /* Call again after `next_ms`. Sets `on` to the state to show now; returns true when the
         * LED must be written (it changed, or the blink code is playing), so boards where a write
         * costs a bus transfer (the Pico W's LED is on the CYW43) only write on a change. */
        bool step(Search search, bool& on, uint32_t& next_ms);

        // The next step writes the LED even if its state did not change (its colour, brightness
        // or which LEDs are on changed).
        void invalidate() { shown_ = -1; }

    private:
        bool phase_{false};
        int shown_{-1};  // last state returned, -1 = unknown
    };

} // namespace status_led

#endif // _OGXM_STATUS_LED_H_
