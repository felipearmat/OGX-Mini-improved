#include <algorithm>
#include <cstring>

#include "Bluepad32/BleStore.h"
#include "BTManager/BTManager.h"
#include "Gamepad/Gamepad.h"
#include "UserSettings/UserSettings.h"

/* The ESP32's storage for the shared BLE web app (Bluepad32/BleStore.h): the RP2040 keeps the
 * settings (I2C/Esp32Bridge.h), so the USB and Bluetooth web apps edit the same ones. Before it
 * has sent them (or with an older RP2040), profiles stay in this ESP32's NVS. */
namespace ble_store {

DeviceDriverType current_driver() { return UserSettings::get_instance().get_current_driver(); }
uint8_t max_gamepads() { return MAX_GAMEPADS; }
uint8_t active_profile(uint8_t index) { return UserSettings::get_instance().get_active_profile_id(index); }
UserProfile profile_by_index(uint8_t index) { return UserSettings::get_instance().get_profile_by_index(index); }
UserProfile profile_by_id(uint8_t profile_id) { return UserSettings::get_instance().get_profile_by_id(profile_id); }

bool save_profile(uint8_t index, const UserProfile& profile, DeviceDriverType driver)
{
    UserProfile copy = profile;
    if (driver != DeviceDriverType::NONE)
        UserSettings::get_instance().store_profile_and_driver_type(driver, index, copy);
    else
        UserSettings::get_instance().store_profile(index, copy);
    BTManager::get_instance().on_settings_saved();  // applied now, not at the next boot
    return true;
}

const dongle_settings::Settings& dongle_settings() { return ::dongle_settings::get(); }

void save_dongle_settings(const ::dongle_settings::Settings& settings)
{
    BTManager::get_instance().send_settings_blob(Esp32Bridge::PacketBlob::DONGLE_SETTINGS,
                                                 reinterpret_cast<const uint8_t*>(&settings), sizeof(settings));
}

size_t kbm_settings(uint8_t* out, size_t len)
{
    return BTManager::get_instance().kbm_settings(out, len);
}

bool save_kbm_settings(const uint8_t* data, size_t len)
{
    if (len == 0)
        return false;
    BTManager::get_instance().send_settings_blob(Esp32Bridge::PacketBlob::KBM_SETTINGS, data, len);  // checked there
    return true;
}

size_t live_pad(uint8_t* out, size_t len)
{
    /* The web app's 23-byte layout is PacketIn from dpad to analog. */
    const I2CDriver::PacketIn packet_in = BTManager::get_instance().get_packet_in(0);
    const size_t n = std::min<size_t>(len, 23);
    std::memcpy(out, &packet_in.dpad, n);
    return n;
}

} // namespace ble_store
