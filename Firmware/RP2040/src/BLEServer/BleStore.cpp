#include "BLEServer/BleStore.h"

#include <algorithm>
#include <cstring>

#include "Bluepad32/BleStore.h"
#include "UserSettings/Capabilities.h"
#include "Gamepad/I2CWirePad.h"
#include "TaskQueue/TaskQueue.h"
#include "USBDevice/DeviceDriver/KBM/KbmSettings.h"
#include "UserSettings/UserSettings.h"

/* Writes go to flash from Core0 (they restart the board, except the KBM mapping). */
namespace ble_store {

static Gamepad* s_pad = nullptr;

void init(Gamepad* pad)
{
    s_pad = pad;
    UserSettings::get_instance().initialize_flash();
}

DeviceDriverType current_driver() { return UserSettings::get_instance().get_current_driver(); }
uint8_t max_gamepads() { return MAX_GAMEPADS; }
uint32_t capabilities()
{
#if defined(CONFIG_EN_USB_HOST)
    return ::capabilities::kPicoW | ::capabilities::WIRED_INPUT;
#else
    return ::capabilities::kPicoW;
#endif
}
uint8_t active_profile(uint8_t index) { return UserSettings::get_instance().get_active_profile_id(index); }
UserProfile profile_by_index(uint8_t index) { return UserSettings::get_instance().get_profile_by_index(index); }
UserProfile profile_by_id(uint8_t profile_id) { return UserSettings::get_instance().get_profile_by_id(profile_id); }

bool save_profile(uint8_t index, const UserProfile& profile, DeviceDriverType driver)
{
    if (driver != DeviceDriverType::NONE) {
        return TaskQueue::Core0::queue_delayed_task(TaskQueue::Core0::get_new_task_id(), 1000, false,
            [driver, profile, index] { UserSettings::get_instance().store_profile_and_driver_type(driver, index, profile); });
    }
    return TaskQueue::Core0::queue_delayed_task(TaskQueue::Core0::get_new_task_id(), 1000, false,
        [index, profile] { UserSettings::get_instance().store_profile(index, profile); });
}

const dongle_settings::Settings& dongle_settings() { return ::dongle_settings::get(); }

void save_dongle_settings(const ::dongle_settings::Settings& settings)
{
    TaskQueue::Core0::queue_delayed_task(TaskQueue::Core0::get_new_task_id(), 1000, false,
        [settings] { UserSettings::get_instance().store_dongle_settings(settings); });
}

size_t kbm_settings(uint8_t* out, size_t len)
{
    const kbm_settings::Settings& settings = ::kbm_settings::get();
    const size_t n = std::min(len, sizeof(settings));
    std::memcpy(out, &settings, n);
    return n;
}

bool save_kbm_settings(const uint8_t* data, size_t len)
{
    ::kbm_settings::Settings settings{};
    if (!::kbm_settings::decode(data, len, settings))
        return false;
    /* Stored and applied from Core0; no reboot. */
    TaskQueue::Core0::queue_delayed_task(TaskQueue::Core0::get_new_task_id(), 1, false,
        [settings] { UserSettings::get_instance().store_kbm_settings(settings); });
    return true;
}

size_t live_pad(uint8_t* out, size_t len)
{
    if (!s_pad)
        return 0;
    const I2CWirePadIn wire = i2c_wire_pad_from(s_pad->get_pad_in());
    const size_t n = std::min(len, sizeof(wire));
    std::memcpy(out, &wire, n);
    return n;
}

} // namespace ble_store
