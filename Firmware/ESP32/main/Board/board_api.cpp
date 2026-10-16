#include <driver/gpio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "sdkconfig.h"
#include "Board/board_api.h"
#include "Board/ExtRgbLed.h"
#include "Board/ModeIndicator.h"
#include "UserSettings/DongleSettings.h"

namespace board_api {

SemaphoreHandle_t leds_mutex_ = nullptr;
uint8_t status_rgb_[3]{};  // the external RGB status LED's colour when on, before its brightness
// SemaphoreHandle_t reset_mutex_ = nullptr;

void init_board()
{
    if (leds_mutex_ == nullptr)
    {
        leds_mutex_ = xSemaphoreCreateMutex();
    }
    // if (reset_mutex_ == nullptr)
    // {
    //     reset_mutex_ = xSemaphoreCreateMutex();
    // }

    if (xSemaphoreTake(leds_mutex_, portMAX_DELAY))
    {
        for (const auto& LED_PIN : LED_PINS)
        {
            gpio_reset_pin(LED_PIN);
            gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
            gpio_set_level(LED_PIN, 0);
        }
        ext_rgb_led::init();
        xSemaphoreGive(leds_mutex_);
    }
    // if (xSemaphoreTake(reset_mutex_, portMAX_DELAY))
    // {
    //     gpio_reset_pin(RESET_PIN);
    //     gpio_set_direction(RESET_PIN, GPIO_MODE_INPUT);
    //     gpio_set_level(RESET_PIN, 1);
    //     xSemaphoreGive(reset_mutex_);
    // }
}

//Set LED by index
void set_led(uint8_t index, bool state)
{
    if constexpr (NUM_LEDS < 1)
    {
        return;
    }
    if (index >= NUM_LEDS)
    {
        return;
    }
    if (xSemaphoreTake(leds_mutex_, portMAX_DELAY))
    {
        gpio_set_level(LED_PINS[index], state ? 1 : 0);
        xSemaphoreGive(leds_mutex_);
    }
}

//Set first LED (and the external RGB status LED, in its colour)
void set_led(bool state)
{
    /* Each LED can be turned off in the web app (dongle options from the RP2040, applied live). */
    const dongle_settings::Settings& options = dongle_settings::get();
    uint8_t r = status_rgb_[0], g = status_rgb_[1], b = status_rgb_[2];
    mode_indicator::scale_color(options.status_led_brightness, r, g, b);
    const bool rgb_on = state && options.status_led_esp32_ext;
    if (xSemaphoreTake(leds_mutex_, portMAX_DELAY))
    {
        if constexpr (NUM_LEDS > 0)
        {
            gpio_set_level(LED_PINS[0], (state && options.status_led_esp32) ? 1 : 0);
        }
        ext_rgb_led::set(rgb_on ? r : 0, rgb_on ? g : 0, rgb_on ? b : 0);
        xSemaphoreGive(leds_mutex_);
    }
}

void set_led_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (xSemaphoreTake(leds_mutex_, portMAX_DELAY))
    {
        status_rgb_[0] = r;
        status_rgb_[1] = g;
        status_rgb_[2] = b;
        xSemaphoreGive(leds_mutex_);
    }
}

} // namespace board_api