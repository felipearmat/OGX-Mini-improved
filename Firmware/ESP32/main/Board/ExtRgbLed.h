#ifndef _EXT_RGB_LED_H_
#define _EXT_RGB_LED_H_

#include <cstdint>

/* External WS2812 status LED (Kconfig ENABLE_EXT_RGB / EXT_RGB_PIN), driven by the RMT peripheral.
 * Without the option both calls do nothing. */
namespace ext_rgb_led
{
    void init();
    void set(uint8_t r, uint8_t g, uint8_t b);
}

#endif // _EXT_RGB_LED_H_
