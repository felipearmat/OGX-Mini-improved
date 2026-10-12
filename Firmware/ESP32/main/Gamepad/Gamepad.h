#ifndef GAMEPAD_H
#define GAMEPAD_H

#include <cstdint>

#include "sdkconfig.h"
#include "Gamepad/GamepadBits.h"
#include "Gamepad/GamepadMapper.h"
#include "UserSettings/UserProfile.h"
#include "UserSettings/UserSettings.h"
#include "Board/ogxm_log.h"

/* Outputs (USB pads on the RP2040 side). One RP2040: one output, while Bluetooth keeps two slots
 * so a Joy-Con pair can merge into it. Several RP2040s (CONFIG_MULTI_SLAVE_MODE): one each. */
#if defined(CONFIG_MULTI_SLAVE_MODE) && CONFIG_MULTI_SLAVE_MODE
#define MAX_GAMEPADS CONFIG_BLUEPAD32_MAX_DEVICES
#else
#define MAX_GAMEPADS 1
#endif
static_assert(  MAX_GAMEPADS > 0 && 
                MAX_GAMEPADS <= 4, 
                "MAX_GAMEPADS must be between 1 and 4");

// Button / D-pad bits shared with the RP2040 firmware: Gamepad/GamepadBits.h. The user profile
// (remapping, stick and trigger settings) is applied by GamepadMapper, shared too.
struct Gamepad : GamepadBits {};

#endif // GAMEPAD_H
