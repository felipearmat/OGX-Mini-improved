// Legacy 23-byte pad layout sent to the web app (USB CDC and BLE).
// Regression covered: the web app rejects anything but 23 bytes, and Gamepad::PadIn grew to
// 59 bytes with IMU/touchpad fields, so live input stopped showing.
#include <cstddef>

#include "Gamepad/I2CWirePad.h"
#include "test.h"

TEST(wire_layout_is_the_legacy_23_bytes) {
    CHECK_EQ(sizeof(I2CWirePadIn), 23u);
    CHECK_EQ(offsetof(I2CWirePadIn, dpad), 0u);
    CHECK_EQ(offsetof(I2CWirePadIn, buttons), 1u);
    CHECK_EQ(offsetof(I2CWirePadIn, trigger_l), 3u);
    CHECK_EQ(offsetof(I2CWirePadIn, trigger_r), 4u);
    CHECK_EQ(offsetof(I2CWirePadIn, joystick_lx), 5u);
    CHECK_EQ(offsetof(I2CWirePadIn, joystick_ry), 11u);
    CHECK_EQ(offsetof(I2CWirePadIn, analog), 13u);
    CHECK(sizeof(Gamepad::PadIn) > sizeof(I2CWirePadIn));  // why the conversion is needed
}

TEST(conversion_keeps_every_legacy_field) {
    Gamepad::PadIn in;
    in.dpad = Gamepad::DPAD_UP;
    in.buttons = Gamepad::BUTTON_START | Gamepad::BUTTON_A;
    in.trigger_l = 11;
    in.trigger_r = 22;
    in.joystick_lx = -1234;
    in.joystick_ly = 2345;
    in.joystick_rx = -32768;
    in.joystick_ry = 32767;
    for (int i = 0; i < 10; ++i)
        in.analog[i] = static_cast<uint8_t>(i * 3);
    in.accel[0] = 999;  // not part of the legacy layout

    const I2CWirePadIn w = i2c_wire_pad_from(in);
    CHECK_EQ(w.dpad, Gamepad::DPAD_UP);
    CHECK_EQ(w.buttons, static_cast<uint16_t>(Gamepad::BUTTON_START | Gamepad::BUTTON_A));
    CHECK_EQ(w.trigger_l, 11);
    CHECK_EQ(w.trigger_r, 22);
    CHECK_EQ(w.joystick_lx, -1234);
    CHECK_EQ(w.joystick_ly, 2345);
    CHECK_EQ(w.joystick_rx, -32768);
    CHECK_EQ(w.joystick_ry, 32767);
    for (int i = 0; i < 10; ++i)
        CHECK_EQ(w.analog[i], i * 3);
}

TEST_MAIN()
