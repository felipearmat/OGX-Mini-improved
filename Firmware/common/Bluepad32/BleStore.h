#ifndef _OGXM_BLE_STORE_H_
#define _OGXM_BLE_STORE_H_

#include <cstddef>
#include <cstdint>

#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"
#include "UserSettings/DongleSettings.h"
#include "UserSettings/UserProfile.h"

/* Where the BLE web app's settings live (Bluepad32/BLEServer). Each firmware implements it: the
 * Pico W stores them in its flash; the hybrid's ESP32 sends them to the RP2040, which keeps them.
 * Called from the BTstack run loop. */
namespace ble_store {

    constexpr uint8_t kMaxProfiles = 8;

    DeviceDriverType current_driver();
    uint8_t max_gamepads();
    uint32_t capabilities();  // UserSettings/Capabilities.h
    uint8_t active_profile(uint8_t index);
    UserProfile profile_by_index(uint8_t index);
    UserProfile profile_by_id(uint8_t profile_id);
    /* driver NONE: the profile only. The board may restart once it is stored. */
    bool save_profile(uint8_t index, const UserProfile& profile, DeviceDriverType driver);

    const dongle_settings::Settings& dongle_settings();
    void save_dongle_settings(const dongle_settings::Settings& settings);  // the board restarts

    /* Mouse + keyboard mode mapping as its wire bytes (48), applied without a restart. */
    size_t kbm_settings(uint8_t* out, size_t len);
    bool save_kbm_settings(const uint8_t* data, size_t len);  // false: not a valid mapping

    /* Pad 0's live input in the web app's legacy 23-byte layout; returns the length. */
    size_t live_pad(uint8_t* out, size_t len);

} // namespace ble_store

#endif // _OGXM_BLE_STORE_H_
