#include "Board/Config.h"
#if defined(CONFIG_EN_RGB)

#include <hardware/gpio.h>

#include "Board/Pico_WS2812/WS2812.hpp"
#include "Board/board_api_private/board_api_private.h"

namespace board_api_rgb {

#ifndef RGB_PXL_COUNT
#define RGB_PXL_COUNT 1
#endif

WS2812& get_ws2812() {
#if defined(RGB_PXL_PIO)
    /* Custom: board picks the PIO block; claim a free state machine on it. */
    static WS2812 ws2812 = WS2812(RGB_PXL_PIN, RGB_PXL_COUNT, RGB_PXL_PIO,
                                  pio_claim_unused_sm(RGB_PXL_PIO, true), WS2812::FORMAT_GRB);
#else
    static WS2812 ws2812 = WS2812(RGB_PXL_PIN, RGB_PXL_COUNT, pio1, 0, WS2812::FORMAT_GRB);
#endif
    return ws2812;
}

void init() {
#if defined(RGB_PWR_PIN)
    gpio_init(RGB_PWR_PIN);
    gpio_set_dir(RGB_PWR_PIN, GPIO_OUT);
    gpio_put(RGB_PWR_PIN, 1);
#endif

    /* Custom: the pixel buffer starts uninitialised; clear the LEDs set_led() never writes. */
    get_ws2812().fill(WS2812::RGB(0, 0, 0));

#if defined(OGXM_EXT_RGB_PIN)
    /* Custom: off until the output mode colour is known (board_api::set_led_color). */
    set_led(0, 0, 0);
#else
    set_led(0xFF, 0, 0);
#endif
}

/* The status colour goes on the first LED; the others (RGB_PXL_COUNT > 1) stay off for now. */
void set_led(uint8_t r, uint8_t g, uint8_t b) {
    get_ws2812().setPixelColor(0, WS2812::RGB(r, g, b));
    get_ws2812().show();
}

} // namespace board_api_rgb

#endif // defined(CONFIG_EN_RGB)