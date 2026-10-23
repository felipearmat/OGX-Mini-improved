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
 *    OGXM_FULL_SEARCH_S                    60   full search for new controllers (0-600 s)
 *    OGXM_REDUCED_SEARCH_S                 NO_LIMIT | 0-600   then the reduced search
 *    OGXM_COMBO_DISABLED_MODES             0    output modes whose button combo is off (bit mask)
 *    OGXM_IDLE_OFF_MINUTES                 15   turn a Bluetooth controller off after this many
 *                                          minutes without input (0 = never, 1-255)
 *    OGXM_HIGH_TX_POWER                    OFF  higher Bluetooth transmit power, where the radio
 *                                          allows it (the RP2040 + ESP32 hybrid: +9 dBm)
 *
 *    OGXM_STATUS_LED_PICO                  ON   the Pico board's own status LED (a board's RGB LED
 *                                          counts as its own)
 *    OGXM_STATUS_LED_PICO_EXT              ON   an external RGB status LED on the Pico (GP6)
 *    OGXM_STATUS_LED_ESP32                 ON   the hybrid ESP32's status LED
 *    OGXM_STATUS_LED_ESP32_EXT             ON   an external RGB status LED on the ESP32 (GPIO 4)
 *    OGXM_EXT_RGB_BRIGHTNESS               48   the RGB LEDs' brightness (1-255)
 *    OGXM_FLASH_LOG                        OFF  keep the log in flash too (ON with OGXM_DEV_DEFAULTS)
 *    OGXM_DIAGNOSTICS_LINK                 OFF  detailed controller diagnostics: the Bluetooth link queries
 *                                          of Bluepad32/LinkDiag (ON with OGXM_DEV_DEFAULTS)
 *
 *  Settings is also the wire format (web app over USB and Bluetooth) and the flash format:
 *  24 bytes, a version byte then one byte per option (0 / 1), unused bytes zero.
 *  Version 8 = version 7 plus, applied without a restart:
 *    byte 7         detailed controller diagnostics (0 / 1): the link queries of Bluepad32/LinkDiag (RSSI,
 *                   channels in use, failed contacts, transmit power, the controller's Bluetooth
 *                   chip). Byte 7 was unused since version 3 (zero in what this firmware wrote), so
 *                   it is read from version 6 on: the hybrid's I2C link carries it in its version 6
 *                   record, and a stored version 6 / 7 record reads as off.
 *  Version 7 = version 6 plus, applied without a restart:
 *    byte 23        keep the log in flash too (Board/FlashLog.h), so it outlives unplugging
 *  The hybrid's I2C link carries the version 6 record (encode_v6): the ESP32 has no use for byte
 *  23, and its packet keeps its size.
 *  Version 6 = version 5 plus, all applied without a restart:
 *    byte 18        the Pico's status LED on (0 / 1)
 *    byte 19        the external RGB LED on the Pico on (0 / 1)
 *    byte 20        the hybrid ESP32's status LED on (0 / 1)
 *    byte 21        the external RGB LED on the hybrid ESP32 on (0 / 1)
 *    byte 22        the RGB LEDs' brightness (1-255; 0 reads as the default)
 *  Version 5 = version 4 plus:
 *    byte 17        higher Bluetooth transmit power (0 / 1), applied without a restart
 *  Version 4 = version 3 plus:
 *    byte 16        minutes without input before a Bluetooth controller is turned off
 *                   (0 = never)
 *  Version 3 (16 bytes):
 *    bytes 1-6, 8   options (0 / 1); byte 7 unused (was the single controller option)
 *    bytes 9-11     search for new controllers while a slot is open with a pad connected
 *                   (Bluepad32/ScanPolicy.h): two 12-bit second counts, little-endian bit order —
 *                   full search = bits 0-11, reduced search = bits 12-23; 0-600 s, and 4095 for
 *                   the reduced search = no limit. Both 0 = no search once a pad is connected.
 *    bytes 12-15    output modes whose button combo is off, a little-endian bit mask indexed by
 *                   DeviceDriverType (bit n = mode n); zero turns all combos on. The Web App mode
 *                   combo (value 100, outside the mask) is always on.
 *  Versions 5, 4, 3, 2 and 1 are still accepted; the options they lack take their build defaults.
 *  For versions 2 and 1 (the first 8 bytes) the search times take their defaults, or 0 / 0
 *  when their single controller byte (7) was on.
 */
namespace dongle_settings {

    constexpr uint8_t kVersion = 8;
    constexpr uint16_t kMaxSearchS = 600;           // 10 minutes
    constexpr uint16_t kSearchNoLimit = 0xFFF;      // reduced search only
    constexpr size_t kV1Length = 8;
    constexpr size_t kV3Length = 16;
    constexpr size_t kV4Length = 17;
    constexpr size_t kV5Length = 18;
    constexpr size_t kV6Length = 23;

#pragma pack(push, 1)
    struct Settings {
        uint8_t version;
        uint8_t disconnect_pads_on_mode_change;
        uint8_t joycon_pair_imu_right;
        uint8_t joycon_pair_horizontal;
        uint8_t joycon_solo_horizontal;
        uint8_t mac_per_controller;
        uint8_t ps4_legacy_motion_scale;
        uint8_t link_diagnostics;  // version 8 (versions 1-2: the single controller option, now the search times)
        // Version 2
        uint8_t joycon_pair_rumble_per_side;
        // Version 3
        uint8_t search_times[3];
        uint32_t combo_disabled_modes;
        // Version 4
        uint8_t idle_off_minutes;
        // Version 5
        uint8_t high_tx_power;
        // Version 6
        uint8_t status_led_pico;
        uint8_t status_led_pico_ext;
        uint8_t status_led_esp32;
        uint8_t status_led_esp32_ext;
        uint8_t status_led_brightness;
        // Version 7
        uint8_t keep_log_in_flash;
    };
#pragma pack(pop)
    static_assert(sizeof(Settings) == 24, "dongle_settings::Settings is a wire format");

    // Build-time defaults.
    Settings defaults();

    // The version 6 record (kV6Length bytes, version byte 6): what the hybrid's I2C link carries.
    void encode_v6(const Settings& settings, uint8_t* out);

    // Parse stored / received bytes, version 8, 7, 6, 5, 4, 3, 2 or 1. False (out untouched) for a short buffer or
    // an unknown version; option bytes are normalised to 0 / 1.
    bool decode(const uint8_t* data, size_t len, Settings& out);

    // Whether the button combo may switch to this output mode (DeviceDriverType value). Always
    // true for D-Input/Debug (the way back to the web app), whatever the mask says.
    bool mode_combo_enabled(const Settings& settings, uint8_t driver);

    // Search times (seconds; reduced may be kSearchNoLimit). set clamps to 0-600.
    uint16_t full_search_s(const Settings& settings);
    uint16_t reduced_search_s(const Settings& settings);
    void set_search_times(Settings& settings, uint16_t full_s, uint16_t reduced_s);

    // Same settings apart from the ones applied live (combo mask, search times, idle time,
    // transmit power, status LEDs, the log in flash, controller diagnostics): a change of those alone
    // needs no restart.
    bool same_except_live(const Settings& a, const Settings& b);

    // Current settings (defaults until set() is called at boot with the stored ones).
    const Settings& get();
    void set(const Settings& settings);

} // namespace dongle_settings

#endif // _OGXM_CUSTOM_DONGLE_SETTINGS_H_
