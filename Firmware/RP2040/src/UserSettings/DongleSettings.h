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
 *    OGXM_SINGLE_CONTROLLER                OFF  no search for new controllers once one is connected
 *                                          (both search times 0; see below)
 *    OGXM_JOYCON_PAIR_RUMBLE               PER_SIDE | BOTH  (merged pair: strong motor on the left
 *                                          Joy-Con and weak on the right, as SDL / Steam / Linux
 *                                          do, or both motors on both Joy-Cons)
 *
 *  Settings is also the wire format (web app over USB and Bluetooth) and the flash format:
 *  16 bytes, a version byte then one byte per option (0 / 1), unused bytes zero.
 *  Version 3:
 *    bytes 1-6, 8   options (0 / 1); byte 7 unused (was the single controller option)
 *    bytes 9-11     search for new controllers while a slot is open with a pad connected
 *                   (Bluepad32/ScanPolicy.h): two 12-bit second counts, little-endian bit order —
 *                   full search = bits 0-11, reduced search = bits 12-23; 0-600 s, and 4095 for
 *                   the reduced search = no limit. Both 0 = no search once a pad is connected.
 *    bytes 12-15    output modes whose button combo is off, a little-endian bit mask indexed by
 *                   DeviceDriverType (bit n = mode n); zero turns all combos on. The Web App mode
 *                   combo (value 100, outside the mask) is always on.
 *  Versions 2 and 1 (the first 8 bytes) are still accepted: the search times take their
 *  defaults (60 s full, reduced with no limit), or 0 / 0 when their single controller byte (7)
 *  was on.
 */
namespace dongle_settings {

    constexpr uint8_t kVersion = 3;
    constexpr uint16_t kDefaultFullSearchS = 60;
    constexpr uint16_t kMaxSearchS = 600;           // 10 minutes
    constexpr uint16_t kSearchNoLimit = 0xFFF;      // reduced search only
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
        uint8_t unused_single_controller;  // versions 1-2 only (now the search times)
        // Version 2
        uint8_t joycon_pair_rumble_per_side;
        // Version 3
        uint8_t search_times[3];
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

    // Search times (seconds; reduced may be kSearchNoLimit). set clamps to 0-600.
    uint16_t full_search_s(const Settings& settings);
    uint16_t reduced_search_s(const Settings& settings);
    void set_search_times(Settings& settings, uint16_t full_s, uint16_t reduced_s);

    // Same settings apart from the ones applied live (combo mask, full search time): a change of
    // those alone needs no restart.
    bool same_except_live(const Settings& a, const Settings& b);

    // Current settings (defaults until set() is called at boot with the stored ones).
    const Settings& get();
    void set(const Settings& settings);

} // namespace dongle_settings

#endif // _OGXM_CUSTOM_DONGLE_SETTINGS_H_
