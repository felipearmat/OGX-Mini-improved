#ifndef _OGXM_FIXTURE_REPLAY_H_
#define _OGXM_FIXTURE_REPLAY_H_

#include <cstdint>
#include <string>
#include <vector>

#include "Gamepad/Gamepad.h"
#include "Gamepad/Ps5Core.h"

/* The Bluetooth half of the fixture replay (replay_bt.cpp: Bluepad32 and BTstack headers), kept
 * apart from the USB drivers' half (test_fixtures.cpp: TinyUSB headers): the two define the same
 * HID types, as in the firmware, where they never meet in one file either. */
struct FixtureRow {
    uint32_t t;
    int slot;
    std::string label;  // "" = between steps
    std::vector<uint8_t> r;
};

struct ReplayFrame {
    uint32_t t;
    const std::string* label;
    Gamepad::PadIn in;
    Ps5Core core;  // STEAM mode's passthrough report for pads other than a DualSense
};

/* Each report of slot 0 through Bluepad32's parser for `controller_type`, then the firmware's
 * PadIn path with `gamepad`'s profile (Bluepad32/BtPadIn.h). False if the pad has no parser here. */
bool bt_replay(const std::vector<FixtureRow>& rows, int controller_type, uint16_t vid, uint16_t pid,
               const Gamepad& gamepad, std::vector<ReplayFrame>& out);

/* The pad (Bluepad32's controller type) has a touchpad: DS4, DualSense. */
bool bt_has_touchpad(int controller_type);

#endif // _OGXM_FIXTURE_REPLAY_H_
