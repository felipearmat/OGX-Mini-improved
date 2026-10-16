#include "Board/ModeIndicator.h"

namespace mode_indicator {

static constexpr uint32_t ON_MS     = 200;
static constexpr uint32_t OFF_MS    = 300;
static constexpr uint32_t PAUSE_MS  = 1500;
static constexpr uint8_t  REPEATS   = 2;

// Only touched from the Bluetooth run loop (BTstack), so no locking is needed.
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

/* Twelve colours and white, picked for an RGB LED: hues 30 degrees apart, some of them mixed with
 * white (never dimmed: a dim LED loses its hue), chosen so the closest pair stays clearly apart on
 * the LED (CIEDE2000 >= 19.5 with the LED's linear brightness). Grouped by console family, so the
 * colour tells the family and the blink count the mode: Nintendo from magenta to orange, Xbox
 * greens, PlayStation from cyan to lilac. */
void lightbar_color(DeviceDriverType driver, uint8_t& r, uint8_t& g, uint8_t& b)
{
    switch (driver)
    {
        // Nintendo
        case DeviceDriverType::GAMECUBE:
        case DeviceDriverType::N64:       r = 0xFF; g = 0x00; b = 0xFF; break; // magenta
        case DeviceDriverType::WIIU:      r = 0xFF; g = 0x80; b = 0x80; break; // light red (GameCube adapter)
        case DeviceDriverType::SWITCH:    r = 0xFF; g = 0x00; b = 0x00; break; // red
        case DeviceDriverType::WII:       r = 0xFF; g = 0x80; b = 0x00; break; // orange
        // Xbox
        case DeviceDriverType::XINPUT:    r = 0x00; g = 0xFF; b = 0x00; break; // green
        case DeviceDriverType::XBOXOG:
        case DeviceDriverType::XBOXOG_SB:
        case DeviceDriverType::XBOXOG_XR: r = 0xC0; g = 0xFF; b = 0x80; break; // light lime
        // PlayStation (STEAM is a DualSense)
        case DeviceDriverType::PS3:       r = 0x00; g = 0xFF; b = 0xFF; break; // cyan
        case DeviceDriverType::PS1PS2:
        case DeviceDriverType::PSCLASSIC: r = 0x40; g = 0xA0; b = 0xFF; break; // light azure
        case DeviceDriverType::PS4:       r = 0x00; g = 0x00; b = 0xFF; break; // blue
        case DeviceDriverType::STEAM:     r = 0xC0; g = 0x80; b = 0xFF; break; // lilac
        // Others
        case DeviceDriverType::KBM:       r = 0xFF; g = 0xFF; b = 0x00; break; // yellow
        case DeviceDriverType::DREAMCAST: r = 0x40; g = 0x40; b = 0xFF; break; // light blue-violet
        default:                          r = 0xFF; g = 0xFF; b = 0xFF; break; // white (DInput / Debug)
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

/* Written by the web app (USB loop), read by the Bluetooth core: a colour read while it changes
 * shows for one LED step at most, and the generation makes the boards apply the final one. */
static ModeColors s_custom{};
static volatile bool s_override_active = false;
static volatile uint8_t s_override[3] = {0, 0, 0};
static volatile uint32_t s_generation = 0;

static int color_slot(DeviceDriverType driver)
{
    const int v = static_cast<int>(driver);
    if (driver == DeviceDriverType::WEBAPP)
    {
        return static_cast<int>(DeviceDriverType::DINPUT);
    }
    return v < static_cast<int>(ModeColors::kColorSlots) ? v : -1;
}

void shown_color(DeviceDriverType driver, uint8_t& r, uint8_t& g, uint8_t& b)
{
    if (s_override_active)
    {
        r = s_override[0];
        g = s_override[1];
        b = s_override[2];
        return;
    }
    const int slot = color_slot(driver);
    if (slot >= 0)
    {
        const uint8_t* c = s_custom.rgb[slot];
        if (c[0] | c[1] | c[2])
        {
            r = c[0];
            g = c[1];
            b = c[2];
            return;
        }
    }
    lightbar_color(driver, r, g, b);
}

void scale_color(uint8_t brightness, uint8_t& r, uint8_t& g, uint8_t& b)
{
    r = scale(r, brightness);
    g = scale(g, brightness);
    b = scale(b, brightness);
}

void led_color(DeviceDriverType driver, uint8_t brightness, uint8_t& r, uint8_t& g, uint8_t& b)
{
    shown_color(driver, r, g, b);
    scale_color(brightness, r, g, b);
}

bool decode_colors(const uint8_t* data, size_t len, ModeColors& out)
{
    if (data == nullptr || len < sizeof(ModeColors) || data[0] != ModeColors::kVersion)
    {
        return false;
    }
    for (size_t i = 0; i < sizeof(ModeColors); ++i)
    {
        reinterpret_cast<uint8_t*>(&out)[i] = data[i];
    }
    return true;
}

const ModeColors& custom_colors()
{
    return s_custom;
}

void set_custom_colors(const ModeColors& colors)
{
    s_custom = colors;
    s_custom.version = ModeColors::kVersion;
    s_generation = s_generation + 1;
}

void set_override(bool active, uint8_t r, uint8_t g, uint8_t b)
{
    if (active == s_override_active && (!active || (r == s_override[0] && g == s_override[1] && b == s_override[2])))
    {
        return;
    }
    s_override[0] = r;
    s_override[1] = g;
    s_override[2] = b;
    s_override_active = active;
    s_generation = s_generation + 1;
}

uint32_t color_generation()
{
    return s_generation;
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
