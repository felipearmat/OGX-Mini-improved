#include <cstring>

#include "Gamepad/GamepadBits.h"
#include "UserSettings/UserProfile.h"

UserProfile::UserProfile()
{
    id = 1;

    dpad_up = GamepadBits::DPAD_UP;
    dpad_down = GamepadBits::DPAD_DOWN;
    dpad_left = GamepadBits::DPAD_LEFT;
    dpad_right = GamepadBits::DPAD_RIGHT;

    button_a = GamepadBits::BUTTON_A;
    button_b = GamepadBits::BUTTON_B;
    button_x = GamepadBits::BUTTON_X;
    button_y = GamepadBits::BUTTON_Y;
    button_l3 = GamepadBits::BUTTON_L3;
    button_r3 = GamepadBits::BUTTON_R3;
    button_back = GamepadBits::BUTTON_BACK;
    button_start = GamepadBits::BUTTON_START;
    button_lb = GamepadBits::BUTTON_LB;
    button_rb = GamepadBits::BUTTON_RB;
    button_sys = GamepadBits::BUTTON_SYS;
    button_misc = GamepadBits::BUTTON_MISC;

    analog_enabled = 1;

    analog_off_up = GamepadBits::ANALOG_OFF_UP;
    analog_off_down = GamepadBits::ANALOG_OFF_DOWN;
    analog_off_left = GamepadBits::ANALOG_OFF_LEFT;
    analog_off_right = GamepadBits::ANALOG_OFF_RIGHT;
    analog_off_a = GamepadBits::ANALOG_OFF_A;
    analog_off_b = GamepadBits::ANALOG_OFF_B;
    analog_off_x = GamepadBits::ANALOG_OFF_X;
    analog_off_y = GamepadBits::ANALOG_OFF_Y;
    analog_off_lb = GamepadBits::ANALOG_OFF_LB;
    analog_off_rb = GamepadBits::ANALOG_OFF_RB;
}