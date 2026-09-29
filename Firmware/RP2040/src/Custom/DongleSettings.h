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
 *
 *  Settings is also the wire format (web app over USB and Bluetooth) and the flash format:
 *  8 bytes, a version byte then one byte per option (0 / 1).
 */
namespace dongle_settings {

    constexpr uint8_t kVersion = 1;

#pragma pack(push, 1)
    struct Settings {
        uint8_t version;
        uint8_t disconnect_pads_on_mode_change;
        uint8_t joycon_pair_imu_right;
        uint8_t joycon_pair_horizontal;
        uint8_t joycon_solo_horizontal;
        uint8_t mac_per_controller;
        uint8_t ps4_legacy_motion_scale;
        uint8_t reserved;
    };
#pragma pack(pop)
    static_assert(sizeof(Settings) == 8, "dongle_settings::Settings is a wire format");

    // Build-time defaults.
    Settings defaults();

    // Parse stored / received bytes. False (out untouched) for a short buffer or another
    // version; option bytes are normalised to 0 / 1.
    bool decode(const uint8_t* data, size_t len, Settings& out);

    // Current settings (defaults until set() is called at boot with the stored ones).
    const Settings& get();
    void set(const Settings& settings);

} // namespace dongle_settings

#endif // _OGXM_CUSTOM_DONGLE_SETTINGS_H_
