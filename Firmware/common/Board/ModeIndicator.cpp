#include "Board/ModeIndicator.h"

namespace mode_indicator {

/* One colour per mode the combos switch between (the user's decision, 2026-11-26: the mode is told
 * by its colour, no blink code), full or mixed with white (never dimmed: a dim LED loses its hue).
 * The eight modes on Start + D-pad and Start + RB + D-pad keep the 2026-10-15 colours; the others
 * were picked to stay apart from them on an RGB LED (CIEDE2000 of 17.5 or more with the LED's
 * linear light, against 10.2 for the old Mouse + Keyboard / Dreamcast pair). Wii, GameCube and N64
 * are fixed builds, never switched to, so they may share. */
void lightbar_color(DeviceDriverType driver, uint8_t& r, uint8_t& g, uint8_t& b)
{
    switch (driver)
    {
        // Nintendo
        case DeviceDriverType::SWITCH:    r = 0xFF; g = 0x00; b = 0x00; break; // red
        case DeviceDriverType::WIIU:      r = 0xFF; g = 0x40; b = 0x80; break; // pink (GameCube adapter)
        case DeviceDriverType::WII:       r = 0xFF; g = 0x60; b = 0x00; break; // red-orange
        case DeviceDriverType::GAMECUBE:
        case DeviceDriverType::N64:       r = 0xFF; g = 0xA0; b = 0x80; break; // salmon
        // Xbox
        case DeviceDriverType::XINPUT:    r = 0x00; g = 0xFF; b = 0x00; break; // green
        case DeviceDriverType::XBOXOG:    r = 0xA0; g = 0xFF; b = 0xA0; break; // light green
        case DeviceDriverType::XBOXOG_SB: r = 0x00; g = 0x80; b = 0xFF; break; // azure (Steel Battalion)
        case DeviceDriverType::XBOXOG_XR: r = 0x00; g = 0x40; b = 0xFF; break; // royal blue (DVD remote)
        // PlayStation (STEAM is a DualSense)
        case DeviceDriverType::PS1PS2:    r = 0x80; g = 0x40; b = 0xFF; break; // violet
        case DeviceDriverType::PSCLASSIC: r = 0xD0; g = 0xA0; b = 0xFF; break; // lilac
        case DeviceDriverType::PS3:       r = 0x00; g = 0xFF; b = 0xFF; break; // light blue
        case DeviceDriverType::PS4:       r = 0x00; g = 0x00; b = 0xFF; break; // blue
        case DeviceDriverType::STEAM:     r = 0xA0; g = 0x00; b = 0xFF; break; // purple
        // Others
        case DeviceDriverType::KBM:       r = 0xFF; g = 0xE0; b = 0x00; break; // yellow
        case DeviceDriverType::DREAMCAST: r = 0xFF; g = 0x80; b = 0x40; break; // coral
        default:                          r = 0xFF; g = 0xFF; b = 0xFF; break; // white (D-Input/Debug)
    }
}

static uint8_t scale(uint8_t value, uint8_t brightness)
{
    if (value == 0 || brightness == 0)
    {
        return 0;
    }
    const uint16_t scaled = static_cast<uint16_t>(static_cast<unsigned>(value) * brightness / 255u);
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
        return static_cast<int>(DeviceDriverType::DINPUT_DEBUG);
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

} // namespace mode_indicator
