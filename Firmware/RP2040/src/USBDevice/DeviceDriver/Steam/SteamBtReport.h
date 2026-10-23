#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "Descriptors/PS5.h"
#include "Gamepad/Gamepad.h"
#include "USBDevice/DeviceDriver/Steam/SteamPassthrough.h"

#include "Gamepad/Ps5Core.h"

namespace SteamBtReport
{

/* The passthrough report from a DualSense's own input (Gamepad/Ps5Core.h): built by the Pico W
 * path from Bluepad32, or received from the hybrid's ESP32 (SET_PAD_RAW). */
inline void store_ps5_core(const Ps5Core& core)
{
	PS5::InReport rep{};
	rep.report_id = 0x01;
	rep.joystick_lx = core.joystick_lx;
	rep.joystick_ly = core.joystick_ly;
	rep.joystick_rx = core.joystick_rx;
	rep.joystick_ry = core.joystick_ry;
	rep.trigger_l = core.trigger_l;
	rep.trigger_r = core.trigger_r;
	rep.buttons[0] = core.buttons[0];
	rep.buttons[1] = core.buttons[1];
	rep.buttons[2] = core.buttons[2];

	uint8_t buf[SteamPassthrough::USB_REPORT_SIZE]{};
	std::memcpy(buf, &rep, sizeof(rep));
	SteamPassthrough::store(buf, sizeof(buf));
}

static_assert(ps5_bits::SQUARE == PS5::Buttons0::SQUARE && ps5_bits::CROSS == PS5::Buttons0::CROSS &&
              ps5_bits::DPAD_CENTER == PS5::Buttons0::DPAD_CENTER && ps5_bits::L1 == PS5::Buttons1::L1 &&
              ps5_bits::L2 == PS5::Buttons1::L2 && ps5_bits::R2 == PS5::Buttons1::R2 &&
              ps5_bits::OPTIONS == PS5::Buttons1::OPTIONS && ps5_bits::R3 == PS5::Buttons1::R3 &&
              ps5_bits::PS_HOME == PS5::Buttons2::PS && ps5_bits::MUTE == PS5::Buttons2::MUTE,
              "Gamepad/Ps5Core.h bits follow Descriptors/PS5.h");

inline uint8_t pad_stick_byte(int16_t axis)
{
	int32_t scaled = (static_cast<int32_t>(axis) + 32768) * 255 / 65535;
	if (scaled < 0) {
		return 0;
	}
	if (scaled > 255) {
		return 255;
	}
	return static_cast<uint8_t>(scaled);
}

inline void fill_report_from_pad(PS5::InReport& rep, const Gamepad::PadIn& gp_in, const Gamepad& gamepad)
{
	rep.report_id = 0x01;
	rep.joystick_lx = pad_stick_byte(gp_in.joystick_lx);
	rep.joystick_ly = pad_stick_byte(gp_in.joystick_ly);
	rep.joystick_rx = pad_stick_byte(gp_in.joystick_rx);
	rep.joystick_ry = pad_stick_byte(gp_in.joystick_ry);
	rep.trigger_l = gp_in.trigger_l;
	rep.trigger_r = gp_in.trigger_r;

	uint8_t b0 = PS5::Buttons0::DPAD_CENTER;
	if (gp_in.dpad & gamepad.MAP_DPAD_UP) {
		if (gp_in.dpad & gamepad.MAP_DPAD_LEFT) {
			b0 = PS5::Buttons0::DPAD_LEFT_UP;
		} else if (gp_in.dpad & gamepad.MAP_DPAD_RIGHT) {
			b0 = PS5::Buttons0::DPAD_UP_RIGHT;
		} else {
			b0 = PS5::Buttons0::DPAD_UP;
		}
	} else if (gp_in.dpad & gamepad.MAP_DPAD_DOWN) {
		if (gp_in.dpad & gamepad.MAP_DPAD_LEFT) {
			b0 = PS5::Buttons0::DPAD_DOWN_LEFT;
		} else if (gp_in.dpad & gamepad.MAP_DPAD_RIGHT) {
			b0 = PS5::Buttons0::DPAD_RIGHT_DOWN;
		} else {
			b0 = PS5::Buttons0::DPAD_DOWN;
		}
	} else if (gp_in.dpad & gamepad.MAP_DPAD_LEFT) {
		b0 = PS5::Buttons0::DPAD_LEFT;
	} else if (gp_in.dpad & gamepad.MAP_DPAD_RIGHT) {
		b0 = PS5::Buttons0::DPAD_RIGHT;
	}

	if (gp_in.buttons & gamepad.MAP_BUTTON_X) {
		b0 |= PS5::Buttons0::SQUARE;
	}
	if (gp_in.buttons & gamepad.MAP_BUTTON_A) {
		b0 |= PS5::Buttons0::CROSS;
	}
	if (gp_in.buttons & gamepad.MAP_BUTTON_B) {
		b0 |= PS5::Buttons0::CIRCLE;
	}
	if (gp_in.buttons & gamepad.MAP_BUTTON_Y) {
		b0 |= PS5::Buttons0::TRIANGLE;
	}
	rep.buttons[0] = b0;

	uint8_t b1 = 0;
	if (gp_in.buttons & gamepad.MAP_BUTTON_LB) {
		b1 |= PS5::Buttons1::L1;
	}
	if (gp_in.buttons & gamepad.MAP_BUTTON_RB) {
		b1 |= PS5::Buttons1::R1;
	}
	/* Custom fix: the L2 / R2 bits with the triggers, as a DualSense (and the PS4 / PS3 outputs) */
	if (gp_in.trigger_l) {
		b1 |= PS5::Buttons1::L2;
	}
	if (gp_in.trigger_r) {
		b1 |= PS5::Buttons1::R2;
	}
	if (gp_in.buttons & gamepad.MAP_BUTTON_BACK) {
		b1 |= PS5::Buttons1::SHARE;
	}
	if (gp_in.buttons & gamepad.MAP_BUTTON_START) {
		b1 |= PS5::Buttons1::OPTIONS;
	}
	if (gp_in.buttons & gamepad.MAP_BUTTON_L3) {
		b1 |= PS5::Buttons1::L3;
	}
	if (gp_in.buttons & gamepad.MAP_BUTTON_R3) {
		b1 |= PS5::Buttons1::R3;
	}
	rep.buttons[1] = b1;

	uint8_t b2 = 0;
	if (gp_in.buttons & gamepad.MAP_BUTTON_SYS) {
		b2 |= PS5::Buttons2::PS;
	}
	if (gp_in.buttons & gamepad.MAP_BUTTON_MISC) {
		b2 |= PS5::Buttons2::MUTE;
	}
	rep.buttons[2] = b2;
}

inline void update_from_gamepad(Gamepad& gamepad)
{
	PS5::InReport rep{};
	fill_report_from_pad(rep, gamepad.get_pad_in(), gamepad);
	uint8_t buf[SteamPassthrough::USB_REPORT_SIZE]{};
	std::memcpy(buf, &rep, sizeof(rep));
	SteamPassthrough::store(buf, sizeof(buf));
}

} // namespace SteamBtReport
