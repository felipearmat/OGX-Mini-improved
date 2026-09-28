#ifndef _OGXM_CUSTOM_MODE_INDICATOR_H_
#define _OGXM_CUSTOM_MODE_INDICATOR_H_

#include <cstdint>

#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"

/*  Visual feedback of the active output mode (custom addition, not upstream).
 *
 *  - Onboard LED (Pico W / Pico 2 W, green only): after boot, blinks N times for the
 *    mode number, pauses, and repeats the code REPEATS times. Normal LED behaviour
 *    (blink while searching / solid when connected) resumes afterwards.
 *  - DualShock 4 / DualSense lightbar: colour per mode, applied shortly after connect.
 */
namespace mode_indicator {

    // Mode number shown by the blink code (1..7).
    uint8_t blink_count(DeviceDriverType driver);

    // Lightbar colour for the mode.
    void lightbar_color(DeviceDriverType driver, uint8_t& r, uint8_t& g, uint8_t& b);

    // Start the boot blink code for this mode.
    void begin(DeviceDriverType driver);

    // True while the blink code is still playing.
    bool active();

    // Advance one step: returns false when the code has finished; otherwise sets the LED
    // state to apply now and how long to keep it.
    bool next_step(bool& led_on, uint32_t& duration_ms);

} // namespace mode_indicator

#endif // _OGXM_CUSTOM_MODE_INDICATOR_H_
