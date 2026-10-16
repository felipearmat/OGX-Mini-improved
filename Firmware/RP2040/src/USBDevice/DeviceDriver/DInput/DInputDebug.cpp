#include "USBDevice/DeviceDriver/DInput/DInputDebug.h"

#include "UserSettings/UserSettings.h"

void DInputDebugDevice::initialize()
{
    DInputDevice::initialize();
    debug_.initialize();
    class_drivers_[0] = class_driver_;
    class_drivers_[1] = *debug_.get_class_driver();
    unmapped_.fill(-1);
}

void DInputDebugDevice::process(const uint8_t idx, Gamepad& gamepad)
{
    apply_mapping(idx, gamepad);
    debug_.process(idx, gamepad);  // the web app's packets and live input
    DInputDevice::process(idx, gamepad);
}

const uint16_t* DInputDebugDevice::get_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    return debug_.get_descriptor_string_cb(index, langid);  // "OGX-Mini", serial number, "OGX-Mini Debug"
}

const usbd_class_driver_t* DInputDebugDevice::get_class_drivers(uint8_t& count)
{
    count = static_cast<uint8_t>(class_drivers_.size());
    return class_drivers_.data();
}

void DInputDebugDevice::line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    debug_.line_state_cb(itf, dtr, rts);
}

/* The default mapping while the web app is open (it shows the controller's own buttons), the
 * profile's otherwise. */
void DInputDebugDevice::apply_mapping(uint8_t idx, Gamepad& gamepad)
{
#if defined(CONFIG_OGXM_BOARD_ESP32_BLUEPAD32_I2C)
    /* The hybrid's ESP32 maps the controllers itself (it is told when the web app is open). */
    (void)idx;
    (void)gamepad;
#else
    const int8_t want = WebAppDevice::connected() ? 1 : 0;
    if (unmapped_[idx] == want)
        return;
    unmapped_[idx] = want;
    UserSettings& user_settings = UserSettings::get_instance();
    gamepad.set_profile(user_settings.get_profile_by_index(idx),
                        want ? DeviceDriverType::WEBAPP : DeviceDriverType::DINPUT_DEBUG);
#endif
}
