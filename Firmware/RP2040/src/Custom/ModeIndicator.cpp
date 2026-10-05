#include "Custom/ModeIndicator.h"

namespace mode_indicator {

static constexpr uint32_t ON_MS     = 200;
static constexpr uint32_t OFF_MS    = 300;
static constexpr uint32_t PAUSE_MS  = 1500;
static constexpr uint8_t  REPEATS   = 2;

// Only touched from the BTstack run loop (Core1), so no locking is needed.
static uint8_t s_count = 0;       // blinks per repetition
static uint8_t s_step = 0;        // step within the current repetition
static uint8_t s_repeat = 0;      // completed repetitions
static bool s_active = false;

uint8_t blink_count(DeviceDriverType driver)
{
    switch (driver)
    {
        case DeviceDriverType::XINPUT: return 1;
        case DeviceDriverType::SWITCH: return 2;
        case DeviceDriverType::DINPUT: return 3;
        case DeviceDriverType::PS4:    return 4;
        case DeviceDriverType::STEAM:  return 5;
        case DeviceDriverType::XBOXOG:
        case DeviceDriverType::XBOXOG_SB:
        case DeviceDriverType::XBOXOG_XR: return 6;
        case DeviceDriverType::KBM:    return 8;
        default: return 7;
    }
}

void lightbar_color(DeviceDriverType driver, uint8_t& r, uint8_t& g, uint8_t& b)
{
    switch (driver)
    {
        case DeviceDriverType::XINPUT: r = 0x00; g = 0xFF; b = 0x00; break; // green
        case DeviceDriverType::SWITCH: r = 0xFF; g = 0x00; b = 0x00; break; // red
        case DeviceDriverType::DINPUT: r = 0x00; g = 0x00; b = 0xFF; break; // blue
        case DeviceDriverType::PS4:    r = 0x00; g = 0xFF; b = 0xFF; break; // light blue (hue 180)
        case DeviceDriverType::PS3:    r = 0x00; g = 0x00; b = 0x50; break; // dark blue
        case DeviceDriverType::STEAM:  r = 0x80; g = 0x00; b = 0xFF; break; // purple
        case DeviceDriverType::KBM:    r = 0xFF; g = 0x50; b = 0x00; break; // orange
        case DeviceDriverType::XBOXOG:
        case DeviceDriverType::XBOXOG_SB:
        case DeviceDriverType::XBOXOG_XR: r = 0xFF; g = 0xB0; b = 0x00; break; // yellow
        default:                       r = 0xFF; g = 0xFF; b = 0xFF; break; // white
    }
}

static uint8_t scale(uint8_t value, uint8_t brightness)
{
    if (value == 0 || brightness == 0)
    {
        return 0;
    }
    const uint16_t scaled = static_cast<uint16_t>(value) * brightness / 255;
    return scaled == 0 ? 1 : static_cast<uint8_t>(scaled);
}

void led_color(DeviceDriverType driver, uint8_t brightness, uint8_t& r, uint8_t& g, uint8_t& b)
{
    lightbar_color(driver, r, g, b);
    r = scale(r, brightness);
    g = scale(g, brightness);
    b = scale(b, brightness);
}

void begin(DeviceDriverType driver)
{
    s_count = blink_count(driver);
    s_step = 0;
    s_repeat = 0;
    s_active = true;
}

bool active()
{
    return s_active;
}

bool next_step(bool& led_on, uint32_t& duration_ms)
{
    if (!s_active)
    {
        return false;
    }
    // Steps per repetition: (on, off) * count, then one pause (LED off).
    const uint8_t steps = static_cast<uint8_t>(s_count * 2 + 1);
    if (s_step < s_count * 2)
    {
        led_on = (s_step % 2) == 0;
        duration_ms = led_on ? ON_MS : OFF_MS;
    }
    else
    {
        led_on = false;
        duration_ms = PAUSE_MS;
    }

    if (++s_step >= steps)
    {
        s_step = 0;
        if (++s_repeat >= REPEATS)
        {
            s_active = false;
        }
    }
    return true;
}

} // namespace mode_indicator
