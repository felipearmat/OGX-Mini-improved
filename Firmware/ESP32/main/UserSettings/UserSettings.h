#ifndef _USER_SETTINGS_H_
#define _USER_SETTINGS_H_

#include <cstdint>
#include <array>
#include <atomic>
#include <functional>

#include "sdkconfig.h"
#include "I2CDriver/I2CDriver.h"
#include "UserSettings/UserProfile.h"
#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"
#include "UserSettings/NVSHelper.h"
#include "UserSettings/ComboCheck.h"

class UserSettings
{
public:
    static constexpr uint8_t MAX_PROFILES = 8;
    static constexpr uint32_t GP_CHECK_DELAY_MS = mode_combos::kCheckDelayMs;  // UserSettings/ComboCheck.h

    static UserSettings& get_instance()
    {
        static UserSettings instance;
        return instance;
    }

    void initialize_flash();

    DeviceDriverType get_current_driver();
    bool is_valid_driver(DeviceDriverType mode);
    
    UserProfile get_profile_by_index(const uint8_t index);
    UserProfile get_profile_by_id(const uint8_t profile_id);
    uint8_t get_active_profile_id(const uint8_t index);

    void store_driver_type(DeviceDriverType new_driver_type);
    void store_profile(uint8_t index, UserProfile& profile);
    void store_profile_and_driver_type(DeviceDriverType new_driver_type, uint8_t index, UserProfile& profile);

    /* The RP2040 keeps the settings (I2C/Esp32Bridge.h): once it has sent them, profiles and the
     * output mode come from there and saves go there (send_to_rp2040); until then (or with an
     * RP2040 that predates it) they stay in this ESP32's NVS. BTstack run loop only. */
    using RemoteStore = std::function<void(const UserProfile& profile, uint8_t index, DeviceDriverType driver)>;
    void set_remote_store(RemoteStore send_to_rp2040);
    void set_remote(const std::array<UserProfile, MAX_PROFILES>& profiles, uint8_t active_profile);
    void set_remote_driver(DeviceDriverType driver);
    bool remote() const { return remote_; }

private:
    UserSettings() = default;
    ~UserSettings() = default;
    UserSettings(const UserSettings&) = delete;
    UserSettings& operator=(const UserSettings&) = delete;

    static constexpr uint8_t INIT_FLAG = 0x12;

    NVSHelper& nvs_helper_{NVSHelper::get_instance()};
    DeviceDriverType current_driver_{DeviceDriverType::NONE};

    bool remote_{false};
    std::array<UserProfile, MAX_PROFILES> remote_profiles_{};
    uint8_t remote_active_{1};
    RemoteStore send_to_rp2040_;

    DeviceDriverType DEFAULT_DRIVER();
    const std::string INIT_FLAG_KEY();
    const std::string PROFILE_KEY(const uint8_t profile_id);
    const std::string ACTIVE_PROFILE_KEY(const uint8_t index);
    const std::string DRIVER_TYPE_KEY();
    const std::string FIRMWARE_VER_KEY();
};

#endif // _USER_SETTINGS_H_