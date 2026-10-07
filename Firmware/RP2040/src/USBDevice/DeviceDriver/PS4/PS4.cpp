#include <cstddef>
#include <cstring>
#include <algorithm>

#include "pico/time.h"
#include "Board/ReportedMac.h"
#include "UserSettings/DongleSettings.h"
#include "USBDevice/DeviceDriver/Sony/SonyImu.h"
#include "USBDevice/DeviceDriver/Sony/SonyReports.h"
#include "Gamepad/MotionImu.h"
#include "USBDevice/DeviceDriver/PS4/PS4.h"
#include "Descriptors/PS4Usb.h"

namespace {

constexpr uint8_t kReportIdIn = 0x01;

uint8_t joystick_to_u8(int16_t value, int16_t deadzone)
{
	constexpr uint8_t center = PS4::JOYSTICK_MID;
	if (value > -deadzone && value < deadzone) {
		return center;
	}
	int32_t in = static_cast<int32_t>(value) + 32768;
	int32_t scaled = (in * 255 + 32768) / 65535;
	if (scaled < 0) {
		scaled = 0;
	}
	if (scaled > 255) {
		scaled = 255;
	}
	return static_cast<uint8_t>(scaled);
}

/* int16 LE gyro @ 13–18, accel @ 19–24 in report id 1. Filled from DS4/DS5 BT, Switch Pro, or
 * wired DS4/DualSense USB host paths.
 * Custom (OGX-Mini-improved): real DS4 units (USBDevice/DeviceDriver/Sony/SonyImu). The old Brook-style scaling
 * (gyro/8, accel/64) only read right to hosts applying our calibration report; Steam treated it
 * as real DS4 units: gyro 8x too fast, gravity 64x too weak. */
/* Dongle option: the legacy scale for auth adapters (Brook) that may expect it. */
const sony_imu::MotionScale& motion_scale()
{
	return dongle_settings::get().ps4_legacy_motion_scale ? sony_imu::kLegacyPs4 : sony_imu::kRealUnits;
}

void apply_pad_imu_to_ps4_report(std::array<uint8_t, 64>& rep, const Gamepad::PadIn& gp_in)
{
	if (!gp_in.has_motion()) {
		return;
	}

	int32_t accel[3] = {gp_in.accel[0], gp_in.accel[1], gp_in.accel[2]};
	int32_t gyro[3] = {gp_in.gyro[0], gp_in.gyro[1], gp_in.gyro[2]};
	MotionImu::remap_to_ds4_playing_frame(gp_in.motion_source, accel, gyro);

	sony_reports::put_motion(rep.data(), sony_reports::ds4::kGyro, sony_reports::ds4::kAccel, gyro, accel,
	                         motion_scale());
}

/* Custom (OGX-Mini-improved): feature reports a DS4 host reads at startup (Linux hid-playstation,
 * SDL / Steam). They used to be all zeros. Offsets below are within the full report, report ID
 * at [0] (TinyUSB prepends it). */
constexpr uint8_t kFeatureCalibration = 0x02;
constexpr uint8_t kFeaturePairingInfo = 0x12;
constexpr uint8_t kFeatureFirmwareInfo = 0xA3;

using sony_imu::put_le16;

void fill_calibration(uint8_t* report)
{
	sony_imu::fill_calibration(report, motion_scale());
}

/* Pairing info: device MAC (LSB first) at [1..6]: the dongle's, or the pad's with the
 * "MAC address per controller" dongle option (Board/ReportedMac). */
void fill_pairing_info(uint8_t* report)
{
	reported_mac::get_lsb_first(&report[1]);
}

/* Firmware info: build date/time strings, hardware version at [35], firmware version at [41]. */
void fill_firmware_info(uint8_t* report)
{
	std::memcpy(&report[1], "Sep 21 2018", 11);
	std::memcpy(&report[17], "04:50:51", 8);
	put_le16(report, 35, static_cast<int16_t>(0xB408));
	put_le16(report, 41, static_cast<int16_t>(0xA00A));
}

} // namespace

void PS4Device::initialize()
{
	class_driver_ = {
		.name = TUD_DRV_NAME("PS4"),
		.init = hidd_init,
		.deinit = hidd_deinit,
		.reset = hidd_reset,
		.open = hidd_open,
		.control_xfer_cb = hidd_control_xfer_cb,
		.xfer_cb = hidd_xfer_cb,
		.sof = NULL
	};
	std::memset(report_in_.data(), 0, report_in_.size());
	report_in_[0] = kReportIdIn;
	report_in_[1] = report_in_[2] = report_in_[3] = report_in_[4] = PS4::JOYSTICK_MID;
	report_out_.report_id = 0x05;
	reported_mac::init();
}

void PS4Device::process(const uint8_t idx, Gamepad& gamepad)
{
	(void)idx;
	{
		static bool start_wake_sent = false;
		static bool start_held = false;
		static absolute_time_t start_hold_begin = { 0 };
		Gamepad::PadIn gp_wake = gamepad.get_pad_in();
		bool start_pressed = (gp_wake.buttons & Gamepad::BUTTON_START) != 0;
		if (start_pressed) {
			if (!start_held) {
				start_held = true;
				start_hold_begin = get_absolute_time();
			} else {
				uint64_t hold_ms = to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(start_hold_begin);
				if (hold_ms >= 3000 && tud_suspended() && !start_wake_sent) {
					tud_remote_wakeup();
					start_wake_sent = true;
				}
			}
		} else {
			start_held = false;
			start_wake_sent = false;
		}
		if (tud_suspended() && (gp_wake.buttons & Gamepad::BUTTON_SYS)) {
			tud_remote_wakeup();
		}
	}

	Gamepad::PadIn gp_in = gamepad.get_pad_in();
	std::memset(report_in_.data(), 0, report_in_.size());
	report_in_[0] = kReportIdIn;

	constexpr int16_t DEADZONE = 512;
	report_in_[1] = joystick_to_u8(gp_in.joystick_lx, DEADZONE);
	report_in_[2] = joystick_to_u8(gp_in.joystick_ly, DEADZONE);
	report_in_[3] = joystick_to_u8(gp_in.joystick_rx, DEADZONE);
	report_in_[4] = joystick_to_u8(gp_in.joystick_ry, DEADZONE);

	uint8_t b0 = PS4::Buttons0::DPAD_CENTER;
	switch (gp_in.dpad)
	{
		case Gamepad::DPAD_UP:
			b0 = PS4::Buttons0::DPAD_UP;
			break;
		case Gamepad::DPAD_DOWN:
			b0 = PS4::Buttons0::DPAD_DOWN;
			break;
		case Gamepad::DPAD_LEFT:
			b0 = PS4::Buttons0::DPAD_LEFT;
			break;
		case Gamepad::DPAD_RIGHT:
			b0 = PS4::Buttons0::DPAD_RIGHT;
			break;
		case Gamepad::DPAD_UP_RIGHT:
			b0 = PS4::Buttons0::DPAD_UP_RIGHT;
			break;
		case Gamepad::DPAD_DOWN_RIGHT:
			b0 = PS4::Buttons0::DPAD_RIGHT_DOWN;
			break;
		case Gamepad::DPAD_DOWN_LEFT:
			b0 = PS4::Buttons0::DPAD_DOWN_LEFT;
			break;
		case Gamepad::DPAD_UP_LEFT:
			b0 = PS4::Buttons0::DPAD_LEFT_UP;
			break;
		default:
			break;
	}

	uint8_t b1 = 0;
	uint8_t b2 = 0;

	if (gp_in.buttons & Gamepad::BUTTON_X) {
		b0 |= PS4::Buttons0::SQUARE;
	}
	if (gp_in.buttons & Gamepad::BUTTON_A) {
		b0 |= PS4::Buttons0::CROSS;
	}
	if (gp_in.buttons & Gamepad::BUTTON_B) {
		b0 |= PS4::Buttons0::CIRCLE;
	}
	if (gp_in.buttons & Gamepad::BUTTON_Y) {
		b0 |= PS4::Buttons0::TRIANGLE;
	}
	if (gp_in.buttons & Gamepad::BUTTON_LB) {
		b1 |= PS4::Buttons1::L1;
	}
	if (gp_in.buttons & Gamepad::BUTTON_RB) {
		b1 |= PS4::Buttons1::R1;
	}
	if (gp_in.buttons & Gamepad::BUTTON_L3) {
		b1 |= PS4::Buttons1::L3;
	}
	if (gp_in.buttons & Gamepad::BUTTON_R3) {
		b1 |= PS4::Buttons1::R3;
	}
	if (gp_in.buttons & Gamepad::BUTTON_BACK) {
		b1 |= PS4::Buttons1::SHARE;
	}
	if (gp_in.buttons & Gamepad::BUTTON_START) {
		b1 |= PS4::Buttons1::OPTIONS;
	}
	if (gp_in.buttons & Gamepad::BUTTON_SYS) {
		sys_button_latch_frames_ = 8;
	}
	if (sys_button_latch_frames_ > 0) {
		b2 |= PS4::Buttons2::PS;
		sys_button_latch_frames_--;
	}
	if (gp_in.buttons & Gamepad::BUTTON_MISC) {
		b2 |= PS4::Buttons2::TP;
	}

	if (gp_in.trigger_l) {
		b1 |= PS4::Buttons1::L2;
	}
	if (gp_in.trigger_r) {
		b1 |= PS4::Buttons1::R2;
	}

	report_in_[5] = b0;
	report_in_[6] = b1;
	touch_seq_ = static_cast<uint8_t>((touch_seq_ + 1u) & 0x3Fu);
	b2 = static_cast<uint8_t>(b2 | static_cast<uint8_t>(static_cast<uint8_t>(touch_seq_ << 2)));
	report_in_[7] = b2;

	report_in_[8] = gp_in.trigger_l;
	report_in_[9] = gp_in.trigger_r;

	frame_seq_++;
	/* Custom fix: bytes 10-11 are the motion sensor clock, in units of 16/3 us on a real DS4
	 * (Linux hid-playstation and SDL derive the sample interval from it). They used to get a
	 * per-call counter, so hosts saw samples ~5 us apart. */
	const uint16_t sensor_ts = sony_reports::ds4_sensor_timestamp(time_us_64());
	std::memcpy(&report_in_[sony_reports::ds4::kSensorTimestamp], &sensor_ts, sizeof(sensor_ts));
	report_in_[12] = 0;

	apply_pad_imu_to_ps4_report(report_in_, gp_in);

	/* Custom: battery and touchpad (USBDevice/DeviceDriver/Sony/SonyReports: status byte, idle touch points).
	 * DS4 and DualSense touch points share this format, so theirs are passed through. */
	namespace ds4 = sony_reports::ds4;
	report_in_[ds4::kStatus] = sony_reports::ds4_status(gp_in.battery);
	report_in_[ds4::kTouchCount] = 1;
	report_in_[ds4::kTouchTimestamp] = static_cast<uint8_t>(frame_seq_);
	sony_reports::put_touch_points(&report_in_[ds4::kTouchPoints], gp_in.touch_raw, gp_in.touchpad_valid != 0);
	if (gp_in.touchpad_valid && gp_in.touchpad_click) {
		report_in_[ds4::kButtons2] |= sony_reports::kTouchpadClick;
	}

	if (tud_hid_ready()) {
		tud_hid_report(0, report_in_.data(), static_cast<uint16_t>(report_in_.size()));
	}

	if (new_report_out_) {
		/* Custom: only take what the host marked valid. A lightbar-only update carries zero
		 * motor bytes, which would stop a running rumble. */
		const uint8_t flags = reinterpret_cast<const uint8_t*>(&report_out_)[1];
		if (sony_reports::ds4_rumble_valid(flags)) {
			Gamepad::PadOut gp_out;
			gp_out.rumble_l = report_out_.motor_left;
			gp_out.rumble_r = report_out_.motor_right;
			gamepad.set_pad_out(gp_out);
		}
		/* Custom: pass the lightbar colour on to the pad (DS4 / DualSense over Bluetooth). */
		if (sony_reports::ds4_led_valid(flags)) {
			gamepad.set_host_lightbar(report_out_.lightbar_red, report_out_.lightbar_green,
			                          report_out_.lightbar_blue);
		}
		new_report_out_ = false;
	}
}

uint16_t PS4Device::get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
	(void)itf;
	if (report_type == HID_REPORT_TYPE_INPUT) {
		if (report_id == 0 || report_id == kReportIdIn) {
			return static_cast<uint16_t>(sony_reports::copy_for_get_report(
				report_id, report_in_.data(), report_in_.size(), buffer, reqlen));
		}
	} else if (report_type == HID_REPORT_TYPE_FEATURE) {
		/* Build the full report (ID at [0]) and hand TinyUSB everything after the ID. */
		std::array<uint8_t, 64> report{};
		report[0] = report_id;
		switch (report_id) {
			case kFeatureCalibration:  fill_calibration(report.data()); break;
			case kFeaturePairingInfo:  fill_pairing_info(report.data()); break;
			case kFeatureFirmwareInfo: fill_firmware_info(report.data()); break;
			default: break;
		}
		return static_cast<uint16_t>(sony_reports::copy_for_get_report(
			report_id, report.data(), report.size(), buffer, reqlen));
	}
	return 0;
}

void PS4Device::set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
	(void)itf;
	if (report_type != HID_REPORT_TYPE_OUTPUT) {
		return;
	}
	uint8_t rid = report_id;
	const uint8_t *buf = buffer;
	uint16_t len = bufsize;
	if (rid == 0 && len > 0) {
		rid = buffer[0];
		len = static_cast<uint16_t>(len - 1u);
		buf = &buffer[1];
	}
	/* Custom fix: buf holds the report after its ID (31 bytes from Linux and SDL); every report
	 * used to be dropped (see sony_reports::copy_output_body). Needs flags .. lightbar. */
	constexpr size_t kMinBody = offsetof(PS4::OutReport, lightbar_blue);
	if (rid == 0x05 && sony_reports::copy_output_body(rid, buf, len, kMinBody, report_out_)) {
		new_report_out_ = true;
	}
}

bool PS4Device::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
	(void)rhport;
	(void)stage;
	(void)request;
	return false;
}

const uint16_t* PS4Device::get_descriptor_string_cb(uint8_t index, uint16_t langid)
{
	const char *value = reinterpret_cast<const char*>(PS4Usb::STRING_DESCRIPTORS[index]);
	return get_string_descriptor(value, index);
}

const uint8_t* PS4Device::get_descriptor_device_cb()
{
	return PS4Usb::DEVICE_DESCRIPTORS;
}

const uint8_t* PS4Device::get_hid_descriptor_report_cb(uint8_t itf)
{
	(void)itf;
	return PS4Usb::REPORT_DESCRIPTORS;
}

const uint8_t* PS4Device::get_descriptor_configuration_cb(uint8_t index)
{
	(void)index;
	return PS4Usb::CONFIGURATION_DESCRIPTORS;
}

const uint8_t* PS4Device::get_descriptor_device_qualifier_cb()
{
	return nullptr;
}
