#ifndef _OGXM_STATUS_LED_H_
#define _OGXM_STATUS_LED_H_

#include <cstdint>

/*  The adapter's status LED, the same on every board that has one (custom addition, not
 *  upstream): the output mode's blink code after boot (Board/ModeIndicator.h), then blinking
 *  while the adapter searches for new controllers (fast = full search, slow = reduced search,
 *  Bluepad32/ScanPolicy.h) and solid when it does not (every slot in use, search times used up or
 *  stopped from the controller, a wired controller in use).
 *
 *  A controller's battery at "critical" and not on a charger (Bluepad32/PadBattery.h) adds an
 *  alert: five quick blinks, then about 4 s as above, again and again; boards with an RGB LED
 *  show the quick blinks in a dim red (`alert`).
 *
 *  Only the on / off state and its timing live here; each board writes it to its own LEDs (Pico
 *  W onboard LED, external WS2812 on a Pico 2 W or on the hybrid's Pico 2 or ESP32, ESP32 GPIO
 *  LED) in the output mode's colour (mode_indicator::led_color).
 */
namespace status_led {

    enum class Search : uint8_t { NONE, FULL, REDUCED };

    static constexpr uint32_t kFastBlinkMs = 250;   // full search for new controllers
    static constexpr uint32_t kSlowBlinkMs = 1000;  // reduced search
    static constexpr uint32_t kAlertOnMs = 70;      // battery alert: five quick blinks...
    static constexpr uint32_t kAlertOffMs = 110;
    static constexpr uint8_t kAlertBlinks = 5;
    static constexpr uint32_t kAlertEveryMs = 4000; // ...then this long as usual
    static constexpr uint8_t kAlertLevel = 24;      // the alert's red at no more than this brightness

    /* The alert's red on an RGB LED for the LEDs' brightness setting (0 read as 1): full red that
     * the brightness scales down to at most kAlertLevel, so it stays dim whatever the setting. */
    constexpr uint8_t alert_red(uint8_t brightness)
    {
        const uint8_t level = brightness ? brightness : 1;
        return static_cast<uint8_t>(level <= kAlertLevel ? 255 : 255u * kAlertLevel / level);
    }

    /* When the alert starts or ends, so each board writes its colour once (alert_red, or back to
     * the mode's colour). */
    class AlertColour
    {
    public:
        bool changed(bool alert)
        {
            if (alert == alerting_) return false;
            alerting_ = alert;
            return true;
        }

    private:
        bool alerting_ = false;
    };

    class Blinker
    {
    public:
        /* Call again after `next_ms`. Sets `on` to the state to show now; returns true when the
         * LED must be written (it changed, or the blink code is playing), so boards where a write
         * costs a bus transfer (the Pico W's LED is on the CYW43) only write on a change. */
        bool step(Search search, bool& on, uint32_t& next_ms) { bool alert; return step(search, false, on, next_ms, alert); }
        /* The same with the battery alert: `critical` a controller's battery is critical; `alert`
         * set while the quick blinks play (an RGB LED shows them in dim red). */
        bool step(Search search, bool critical, bool& on, uint32_t& next_ms, bool& alert);

        // The next step writes the LED even if its state did not change (its colour, brightness
        // or which LEDs are on changed).
        void invalidate() { shown_ = -1; }

    private:
        bool phase_{false};
        int shown_{-1};  // last state returned, -1 = unknown
        uint8_t alert_left_{0};       // quick blink phases still to play (on and off each count)
        uint32_t since_alert_ms_{kAlertEveryMs};  // time shown as usual since the last alert
    };

    /* One status LED tick, shared by the Pico W (Bluepad32.cpp), the hybrid's RP2040
     * (ESP32_Bluepad32_I2C.cpp) and its ESP32 (BTManager.cpp): the mode colour written again when
     * it changes (a colour saved or previewed in the web app, a mode change), the blink restarted
     * when the LED options change, then the blink code / search / battery alert (Blinker), the
     * alert in dim red and back to the mode colour. Each board passes what it reads (its search
     * state, its LED options) and how it writes its LED. Returns when to call again (ms). */
    class Indicator
    {
    public:
        /* `rgb`: the mode colour now (mode_indicator::shown_color); `colour_generation`:
         * mode_indicator::color_generation(); `options`: the LED options that board shows, packed
         * by the caller (a change restarts the blink). `on_colour` is called when the mode colour
         * was written because it changed (the Pico W then updates the lightbars, the hybrid logs it). */
        template <typename SetColour, typename SetLed, typename OnColour>
        uint32_t tick(Search search, bool critical, const uint8_t rgb[3], uint32_t colour_generation,
                      uint32_t options, uint8_t brightness, SetColour set_colour, SetLed set_led,
                      OnColour on_colour)
        {
            if (generation_ != colour_generation) {
                generation_ = colour_generation;
                set_colour(rgb[0], rgb[1], rgb[2]);
                on_colour();
                blinker_.invalidate();
            }
            if (options_ != options) {
                options_ = options;
                blinker_.invalidate();
            }
            bool on = false, alert = false;
            uint32_t next_ms = 0;
            if (blinker_.step(search, critical, on, next_ms, alert)) {
                if (alert_.changed(alert)) {
                    if (alert)
                        set_colour(alert_red(brightness), 0, 0);
                    else
                        set_colour(rgb[0], rgb[1], rgb[2]);
                }
                set_led(on);
            }
            return next_ms;
        }

        static uint32_t pack_options(uint8_t a, uint8_t b, uint8_t brightness)
        {
            return static_cast<uint32_t>(a) | static_cast<uint32_t>(b) << 8 | static_cast<uint32_t>(brightness) << 16;
        }

    private:
        Blinker blinker_;
        AlertColour alert_;
        uint32_t generation_{UINT32_MAX};
        uint32_t options_{UINT32_MAX};
    };

} // namespace status_led

#endif // _OGXM_STATUS_LED_H_
