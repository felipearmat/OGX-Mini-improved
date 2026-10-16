#ifndef _DINPUT_DEBUG_DEVICE_H_
#define _DINPUT_DEBUG_DEVICE_H_

#include <array>
#include <cstdint>

#include "USBDevice/DeviceDriver/DInput/DInput.h"
#include "USBDevice/DeviceDriver/WebApp/WebApp.h"

/* Custom: DInput / Debug mode — the DInput gamepad (one per MAX_GAMEPADS) and the web app's
 * serial port in one USB device (Descriptors/DInput.h). The web app's protocol
 * (USBDevice/DeviceDriver/WebApp) runs next to the gamepad: settings, profiles, diagnostics,
 * flashing the hybrid's ESP32. While the web app is open the gamepad sends the controller's own
 * buttons (the default mapping), so the mapping rows light up for the button pressed; the
 * profile's mapping applies again once it closes. */
class DInputDebugDevice : public DInputDevice
{
public:
    void initialize() override;
    void process(const uint8_t idx, Gamepad& gamepad) override;
    const uint16_t* get_descriptor_string_cb(uint8_t index, uint16_t langid) override;
    const usbd_class_driver_t* get_class_drivers(uint8_t& count) override;
    void line_state_cb(uint8_t itf, bool dtr, bool rts) override;

private:
    WebAppDevice debug_;
    std::array<usbd_class_driver_t, 2> class_drivers_{};  // HID, then the serial port
    std::array<int8_t, MAX_GAMEPADS> unmapped_{};          // mapping applied: 1 = default, 0 = profile, -1 = none yet

    void apply_mapping(uint8_t idx, Gamepad& gamepad);
};

#endif // _DINPUT_DEBUG_DEVICE_H_
