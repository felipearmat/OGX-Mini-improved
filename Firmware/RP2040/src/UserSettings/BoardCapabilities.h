#ifndef _OGXM_BOARD_CAPABILITIES_H_
#define _OGXM_BOARD_CAPABILITIES_H_

#include <cstdint>

#include "Board/Config.h"
#include "UserSettings/Capabilities.h"

/* This RP2040 / RP2350 build's capabilities (UserSettings/Capabilities.h), for the USB and the
 * Bluetooth web app alike. */
namespace capabilities {

    inline uint32_t board()
    {
#if defined(CONFIG_OGXM_BOARD_ESP32_BLUEPAD32_I2C)
        uint32_t caps = kHybrid;
#elif defined(CONFIG_EN_BLUETOOTH)
        uint32_t caps = kPicoW | STATUS_LED_PICO;  // the Pico W's LED, on its radio chip
#else
        uint32_t caps = 0;
#endif
#if defined(LED_INDICATOR_PIN)
        caps |= STATUS_LED_PICO;
#endif
#if defined(CONFIG_EN_RGB) && defined(OGXM_EXT_RGB_PIN)
        caps |= STATUS_LED_PICO_EXT;  // an external WS2812B on GP6
#elif defined(CONFIG_EN_RGB)
        caps |= STATUS_LED_PICO;      // the board's own RGB LED is its status LED
#endif
#if defined(CONFIG_EN_USB_HOST)
        caps |= WIRED_INPUT;
#endif
        caps |= FLASH_LOG;  // every RP2040 / RP2350 board (Board/FlashLog.h)
        return caps;
    }

} // namespace capabilities

#endif // _OGXM_BOARD_CAPABILITIES_H_
