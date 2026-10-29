#ifndef _OGXM_BT_PAD_IN_H_
#define _OGXM_BT_PAD_IN_H_

#include <cstdint>
#include <cstring>

#include "Bluepad32/BtPad.h"
#include "Gamepad/Gamepad.h"
#include "Gamepad/MotionImu.h"

namespace bt_pad {

    /* Custom: the PadIn the output modes read, from one Bluepad32 report (Pico W path): buttons,
     * triggers, sticks and touchpad through the profile (map_input), motion in the output's frame,
     * battery. Also what the fixture tests replay recorded reports through (tests/fixtures). */
    inline Gamepad::PadIn pad_in_from(uni_hid_device_t* device, uni_gamepad_t* uni_gp, const Gamepad& gamepad,
                                      MotionImu::WiiPseudoGyroState& wii_gyro, uint32_t now_ms)
    {
        Gamepad::PadIn gp_in;
        const Input mapped = map_input(device, uni_gp, gamepad);
        gp_in.dpad = mapped.dpad;
        gp_in.buttons = mapped.buttons;
        gp_in.trigger_l = mapped.trigger_l;
        gp_in.trigger_r = mapped.trigger_r;
        gp_in.joystick_lx = mapped.joystick_lx;
        gp_in.joystick_ly = mapped.joystick_ly;
        gp_in.joystick_rx = mapped.joystick_rx;
        gp_in.joystick_ry = mapped.joystick_ry;
        std::memcpy(gp_in.touch_raw, mapped.touch_raw, sizeof(gp_in.touch_raw));
        gp_in.touchpad_click = mapped.touchpad_click ? 1 : 0;
        gp_in.touchpad_valid = mapped.touchpad_valid ? 1 : 0;

        const bool joycon = is_switch_joycon(device);
        const RawMotion motion = raw_motion(device, uni_gp);
        MotionImu::apply_bt_motion(gp_in, motion_source_of(device, uni_gp), motion.accel, motion.gyro,
                                   joycon && device->controller_type == CONTROLLER_TYPE_SwitchJoyConLeft,
                                   joycon && device->controller_type == CONTROLLER_TYPE_SwitchJoyConRight,
                                   joycon && pair_partner_idx(device) >= 0, wii_gyro, now_ms);

        gp_in.battery = device->controller.battery;  // every output mode (PS4 mode passes it on)
        return gp_in;
    }

} // namespace bt_pad

#endif // _OGXM_BT_PAD_IN_H_
