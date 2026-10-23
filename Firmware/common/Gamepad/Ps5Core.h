#ifndef _OGXM_PS5_CORE_H_
#define _OGXM_PS5_CORE_H_

#include <cstdint>

/* The DualSense input a STEAM mode passthrough sends as it came from the pad (no user profile):
 * sticks, triggers and the three button bytes of the PS5 input report. Built from Bluepad32
 * (Bluepad32/BtPad.h ps5_core_from), crosses the hybrid's I2C link (SET_PAD_RAW), written into the
 * report by USBDevice/DeviceDriver/Steam/SteamBtReport.h. Bit values: Descriptors/PS5.h. */
#pragma pack(push, 1)
struct Ps5Core
{
    uint8_t joystick_lx{0x80};
    uint8_t joystick_ly{0x80};
    uint8_t joystick_rx{0x80};
    uint8_t joystick_ry{0x80};
    uint8_t trigger_l{0};
    uint8_t trigger_r{0};
    uint8_t buttons[3]{0x08, 0, 0};  // D-pad centred
};
#pragma pack(pop)
static_assert(sizeof(Ps5Core) == 9, "Ps5Core crosses the I2C link");

namespace ps5_bits {
    // buttons[0]: D-pad (low nibble) and face buttons
    constexpr uint8_t DPAD_UP = 0x00, DPAD_UP_RIGHT = 0x01, DPAD_RIGHT = 0x02, DPAD_RIGHT_DOWN = 0x03,
                      DPAD_DOWN = 0x04, DPAD_DOWN_LEFT = 0x05, DPAD_LEFT = 0x06, DPAD_LEFT_UP = 0x07,
                      DPAD_CENTER = 0x08;
    constexpr uint8_t SQUARE = 0x10, CROSS = 0x20, CIRCLE = 0x40, TRIANGLE = 0x80;
    // buttons[1]
    constexpr uint8_t L1 = 0x01, R1 = 0x02, L2 = 0x04, R2 = 0x08, SHARE = 0x10, OPTIONS = 0x20, L3 = 0x40, R3 = 0x80;
    // buttons[2]
    constexpr uint8_t PS_HOME = 0x01, MUTE = 0x04;  // not "PS": a register macro on the ESP32 (Xtensa)
}

#endif // _OGXM_PS5_CORE_H_
