#ifndef _OGXM_CUSTOM_MODE_INDICATOR_H_
#define _OGXM_CUSTOM_MODE_INDICATOR_H_

#include <cstddef>
#include <cstdint>

#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"

/*  Visual feedback of the active output mode (custom addition, not upstream).
 *
 *  - Onboard LED (Pico W / Pico 2 W, green only): after boot, blinks N times for the
 *    mode number, pauses, and repeats the code REPEATS times. Normal LED behaviour
 *    (blink while searching / solid when connected) resumes afterwards.
 *  - DualShock 4 / DualSense lightbar: colour per mode, applied shortly after connect.
 *  - External WS2812 LED (Pico 2 W): the same blink code and LED states, in the mode colour.
 */
namespace mode_indicator {

    // Mode number shown by the blink code (1..7).
    uint8_t blink_count(DeviceDriverType driver);

    // Lightbar colour for the mode.
    void lightbar_color(DeviceDriverType driver, uint8_t& r, uint8_t& g, uint8_t& b);

    // The mode's colour shown now (lightbar, status LED): the override (web app preview, or on
    // the hybrid's ESP32 the colour the RP2040 resolved), else the web app's colour for the mode,
    // else the default above.
    void shown_color(DeviceDriverType driver, uint8_t& r, uint8_t& g, uint8_t& b);

    // A colour scaled to a status LED's brightness (0..255). A lit channel never rounds down to
    // 0, so a colour keeps its hue at any brightness.
    void scale_color(uint8_t brightness, uint8_t& r, uint8_t& g, uint8_t& b);

    // shown_color() scaled to brightness.
    void led_color(DeviceDriverType driver, uint8_t brightness, uint8_t& r, uint8_t& g, uint8_t& b);

    /* The web app's colour per mode, the flash and wire format (USB GET / SET_LED_COLORS): a
     * version byte, then r, g, b for each DeviceDriverType 0..kColorSlots-1 (the web app mode
     * uses DInput's slot). 0, 0, 0 = the default colour. */
#pragma pack(push, 1)
    struct ModeColors {
        static constexpr uint8_t kVersion = 1;
        static constexpr size_t kColorSlots = 18;
        uint8_t version{kVersion};
        uint8_t rgb[kColorSlots][3]{};
    };
#pragma pack(pop)
    static_assert(sizeof(ModeColors) == 55, "mode_indicator::ModeColors is a wire format");

    // False (nothing changed) for a short buffer or an unknown version.
    bool decode_colors(const uint8_t* data, size_t len, ModeColors& out);
    const ModeColors& custom_colors();
    void set_custom_colors(const ModeColors& colors);

    // A colour shown in place of every mode's own one until turned off (active false).
    void set_override(bool active, uint8_t r, uint8_t g, uint8_t b);

    // Changes whenever a custom colour or the override changes, so the boards re-apply the
    // lightbar and the status LED colour.
    uint32_t color_generation();

    // Start the boot blink code for this mode.
    void begin(DeviceDriverType driver);

    // True while the blink code is still playing.
    bool active();

    // Advance one step: returns false when the code has finished; otherwise sets the LED
    // state to apply now and how long to keep it.
    bool next_step(bool& led_on, uint32_t& duration_ms);

} // namespace mode_indicator

#endif // _OGXM_CUSTOM_MODE_INDICATOR_H_
