#ifndef _OGXM_CUSTOM_DONGLE_SETTINGS_H_
#define _OGXM_CUSTOM_DONGLE_SETTINGS_H_

#include <cstddef>
#include <cstdint>

/*  Dongle-wide options (custom addition, not upstream), set from the web app and kept in flash.
 *  Defaults come from CMake:
 *    OGXM_DISCONNECT_PADS_ON_MODE_CHANGE   ON   turn every Bluetooth pad off before a mode change
 *    OGXM_JOYCON_PAIR_IMU_SIDE             RIGHT | LEFT          (merged pair motion source)
 *    OGXM_JOYCON_PAIR_ORIENTATION          VERTICAL | HORIZONTAL
 *    OGXM_JOYCON_SOLO_ORIENTATION          VERTICAL | HORIZONTAL
 *    OGXM_MAC_PER_CONTROLLER               OFF  report the connected pad's address, not the dongle's
 *    OGXM_PS4_LEGACY_MOTION_SCALE          OFF  PS4 mode: old Brook-style motion scale
 *    OGXM_SINGLE_CONTROLLER                OFF  accept one Bluetooth controller (no Joy-Con pair)
 *    OGXM_JOYCON_PAIR_RUMBLE               PER_SIDE | BOTH  (merged pair: strong motor on the left
 *                                          Joy-Con and weak on the right, as SDL / Steam / Linux
 *                                          do, or both motors on both Joy-Cons)
 *
 *  Settings is also the wire format (web app over USB and Bluetooth) and the flash format:
 *  16 bytes, a version byte then one byte per option (0 / 1), unused bytes zero. Version 1 was
 *  the first 8 bytes alone; it is still accepted (newer options take their defaults).
 *  Bytes 12-15 (version 2): output modes whose button combo is off, a little-endian bit mask
 *  indexed by DeviceDriverType (bit n = mode n); zero, as older records have, turns all combos on.
 *  The Web App mode combo (value 100, outside the mask) is always on.
 */
namespace dongle_settings {

    constexpr uint8_t kVersion = 2;
    constexpr size_t kV1Length = 8;

#pragma pack(push, 1)
    struct Settings {
        uint8_t version;
        uint8_t disconnect_pads_on_mode_change;
        uint8_t joycon_pair_imu_right;
        uint8_t joycon_pair_horizontal;
        uint8_t joycon_solo_horizontal;
        uint8_t mac_per_controller;
        uint8_t ps4_legacy_motion_scale;
        uint8_t single_controller;
        // Version 2
        uint8_t joycon_pair_rumble_per_side;
        uint8_t reserved[3];
        uint32_t combo_disabled_modes;
    };
#pragma pack(pop)
    static_assert(sizeof(Settings) == 16, "dongle_settings::Settings is a wire format");

    // Build-time defaults.
    Settings defaults();

    // Parse stored / received bytes, version 2 or 1. False (out untouched) for a short buffer or
    // an unknown version; option bytes are normalised to 0 / 1.
    bool decode(const uint8_t* data, size_t len, Settings& out);

    // Whether the button combo may switch to this output mode (DeviceDriverType value).
    bool mode_combo_enabled(const Settings& settings, uint8_t driver);

    // Same settings apart from the combo mask (a change of the mask alone needs no restart).
    bool same_except_combos(const Settings& a, const Settings& b);

    // Current settings (defaults until set() is called at boot with the stored ones).
    const Settings& get();
    void set(const Settings& settings);

} // namespace dongle_settings

#endif // _OGXM_CUSTOM_DONGLE_SETTINGS_H_
