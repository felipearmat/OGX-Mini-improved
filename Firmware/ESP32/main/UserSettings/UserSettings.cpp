#include <cstring>
#include <array>
#include <string>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_err.h>
#include <esp_log.h>

#include "Gamepad/Gamepad.h"
#include "UserSettings/NVSHelper.h"
#include "UserSettings/ModeCombos.h"

/* The hybrid's mode list: Firmware/common/UserSettings/ModeCombos.h; the combos themselves are
 * checked by UserSettings/ComboCheck.h (BTManager), both shared with the RP2040 firmware. */
#if MAX_GAMEPADS > 1
static constexpr DeviceDriverType VALID_DRIVER_TYPES[] =
{
    DeviceDriverType::DINPUT, 
    DeviceDriverType::SWITCH, 
};
#else
static constexpr const auto& VALID_DRIVER_TYPES = mode_combos::HYBRID_MODES;
#endif

const std::string UserSettings::INIT_FLAG_KEY()
{
    return std::string("init_flag");
}

const std::string UserSettings::PROFILE_KEY(const uint8_t profile_id)
{
    return std::string("profile_") + std::to_string(profile_id);
}

const std::string UserSettings::ACTIVE_PROFILE_KEY(const uint8_t index)
{
    return std::string("active_id_") + std::to_string(index);
}

const std::string UserSettings::DRIVER_TYPE_KEY()
{
    return std::string("driver_type");
}

const std::string UserSettings::FIRMWARE_VER_KEY()
{
    return std::string("firmware_ver");
}

void UserSettings::initialize_flash()
{
    ESP_LOGD("UserSettings", "Checking for UserSettings init flag");

    uint8_t init_flag = 0;

    ESP_ERROR_CHECK(nvs_helper_.read(INIT_FLAG_KEY(), &init_flag, sizeof(init_flag)));

    if (init_flag == INIT_FLAG)
    {
        ESP_LOGD("UserSettings", "UserSettings already initialized");
        return;
    }

    ESP_ERROR_CHECK(nvs_helper_.erase_all());
    OGXM_LOG("Initializing UserSettings\n");

    current_driver_ = DEFAULT_DRIVER();
    uint8_t driver_type = static_cast<uint8_t>(current_driver_);
    ESP_ERROR_CHECK(nvs_helper_.write(DRIVER_TYPE_KEY(), &driver_type, sizeof(driver_type)));

    uint8_t active_id = 1;
    for (uint8_t i = 0; i < MAX_GAMEPADS; i++)
    {
        ESP_ERROR_CHECK(nvs_helper_.write(ACTIVE_PROFILE_KEY(i), &active_id, sizeof(active_id)));
    }

    UserProfile profile = UserProfile();
    for (uint8_t i = 0; i < MAX_PROFILES; i++)
    {
        profile.id = i + 1;
        ESP_ERROR_CHECK(nvs_helper_.write(PROFILE_KEY(profile.id), &profile, sizeof(UserProfile)));
    }

    init_flag = INIT_FLAG;
    ESP_ERROR_CHECK(nvs_helper_.write(INIT_FLAG_KEY(), &init_flag, sizeof(init_flag)));
}

DeviceDriverType UserSettings::get_current_driver()
{
    if (current_driver_ != DeviceDriverType::NONE)
    {
        return current_driver_;
    }

    uint8_t stored_type = 0;
    nvs_helper_.read(DRIVER_TYPE_KEY(), &stored_type, sizeof(stored_type));

    if (is_valid_driver(static_cast<DeviceDriverType>(stored_type)))
    {
        current_driver_ = normalized(static_cast<DeviceDriverType>(stored_type));
        return current_driver_;
    }

    current_driver_ = DEFAULT_DRIVER();
    return current_driver_;
}

void UserSettings::store_driver_type(DeviceDriverType new_driver_type)
{
    if (!is_valid_driver(new_driver_type))
    {
        return;
    }
    new_driver_type = normalized(new_driver_type);

    current_driver_ = new_driver_type;  // sent to the RP2040 on the next driver update tick
    uint8_t new_driver = static_cast<uint8_t>(new_driver_type);
    nvs_helper_.write(DRIVER_TYPE_KEY(), &new_driver, sizeof(new_driver));
}

void UserSettings::set_remote_store(RemoteStore send_to_rp2040)
{
    send_to_rp2040_ = std::move(send_to_rp2040);
}

void UserSettings::set_remote(const std::array<UserProfile, MAX_PROFILES>& profiles, uint8_t active_profile)
{
    remote_profiles_ = profiles;
    remote_active_ = (active_profile >= 1 && active_profile <= MAX_PROFILES) ? active_profile : 1;
    remote_ = true;
}

void UserSettings::set_remote_driver(DeviceDriverType driver)
{
    if (is_valid_driver(driver))
    {
        current_driver_ = normalized(driver);
    }
}

void UserSettings::store_profile(const uint8_t index, UserProfile& profile)
{
    if (remote_ && send_to_rp2040_)
    {
        if (index < MAX_GAMEPADS && profile.id >= 1 && profile.id <= MAX_PROFILES)
        {
            send_to_rp2040_(profile, index, DeviceDriverType::NONE);
        }
        return;
    }
    if (index > MAX_GAMEPADS || profile.id < 1 || profile.id > MAX_PROFILES)
    {
        return;
    }

    OGXM_LOG("Storing profile %d for gamepad %d\n", profile.id, index);

    if (nvs_helper_.write(PROFILE_KEY(profile.id), &profile, sizeof(UserProfile)) == ESP_OK)
    {
        OGXM_LOG("Profile %d stored successfully\n", profile.id);

        nvs_helper_.write(ACTIVE_PROFILE_KEY(index), &profile.id, sizeof(profile.id));
    }
}

void UserSettings::store_profile_and_driver_type(DeviceDriverType new_driver_type, const uint8_t index, UserProfile& profile)
{
    if (remote_ && send_to_rp2040_)
    {
        if (is_valid_driver(new_driver_type) && index < MAX_GAMEPADS && profile.id >= 1 && profile.id <= MAX_PROFILES)
        {
            send_to_rp2040_(profile, index, new_driver_type);
        }
        return;
    }
    store_driver_type(new_driver_type);
    store_profile(index, profile);
}

uint8_t UserSettings::get_active_profile_id(const uint8_t index)
{
    if (remote_)
    {
        return remote_active_;
    }
    uint8_t read_profile_id = 0;

    if (nvs_helper_.read(ACTIVE_PROFILE_KEY(index), &read_profile_id, sizeof(read_profile_id)) != ESP_OK ||
        read_profile_id < 1 || 
        read_profile_id > MAX_PROFILES)
    {
        return 0x01;
    }
    return read_profile_id;
}

UserProfile UserSettings::get_profile_by_index(const uint8_t index)
{
    return get_profile_by_id(get_active_profile_id(index));
}

UserProfile UserSettings::get_profile_by_id(const uint8_t profile_id)
{
    if (remote_)
    {
        return (profile_id >= 1 && profile_id <= MAX_PROFILES) ? remote_profiles_[profile_id - 1] : UserProfile();
    }
    UserProfile profile;

    if (profile_id < 1 || 
        profile_id > MAX_PROFILES ||
        nvs_helper_.read(PROFILE_KEY(profile_id), &profile, sizeof(UserProfile)) != ESP_OK)
    {
        return UserProfile();
    }

    return profile;
}

DeviceDriverType UserSettings::DEFAULT_DRIVER()
{
    return VALID_DRIVER_TYPES[0];
}

bool UserSettings::is_valid_driver(DeviceDriverType mode)
{
    mode = normalized(mode);  // the Web App mode is DInput / Debug
    for (const auto& driver : VALID_DRIVER_TYPES)
    {
        if (mode == driver)
        {
            return true;
        }
    }
    return false;
}