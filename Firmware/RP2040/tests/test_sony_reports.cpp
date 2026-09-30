// Emulated DualShock 3 / 4 and DualSense report handling (Custom/SonyReports).
// Regressions covered, each found on hardware with Linux hosts:
//  - PS3: hid-sony's output report arrived one byte short (TinyUSB strips the 0x01 padding byte
//    as a report ID); rumble fields were read one byte off, rumble never worked.
//  - PS4: output reports were dropped (length check counted the report ID twice): no rumble,
//    no lightbar.
//  - STEAM (DualSense): output body copied over the ID field: rumble from the flags byte, no
//    lightbar.
//  - PS4 / STEAM: a lightbar-only update carries zero motor bytes and stopped a running rumble.
//  - GET_REPORT repeated the report ID TinyUSB already puts in front.
//  - Input: idle touch points read as two fingers at (0, 0); battery / status left at zero;
//    DS4 sensor clock was a per-call counter; synthesized DualSense left motion, touch, battery
//    and clock at zero.
#include <cstddef>

#include "Custom/SonyReports.h"
#include "Descriptors/PS3.h"
#include "Descriptors/PS4.h"
#include "Descriptors/PS5.h"
#include "test.h"

namespace sr = sony_reports;

namespace {

int16_t le16(const uint8_t* r, int o) { return static_cast<int16_t>(r[o] | (r[o + 1] << 8)); }

}  // namespace

// ---- PS3 -------------------------------------------------------------------------------------

TEST(ds3_linux_report_one_byte_short_is_realigned) {
    // Body captured from the dongle with Linux hid-sony, left motor at full force (34 bytes: the
    // leading 0x01 padding byte was stripped by TinyUSB).
    uint8_t body[34] = {0xFF, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x02, 0xFF, 0x27, 0x10};
    PS3::OutReport out{};
    sr::ds3_copy_output_body(body, sizeof(body), reinterpret_cast<uint8_t*>(&out), sizeof(out));
    CHECK_EQ(out.rumble.right_duration, 0xFF);
    CHECK_EQ(out.rumble.right_motor_on, 0);
    CHECK_EQ(out.rumble.left_duration, 0xFF);
    CHECK_EQ(out.rumble.left_motor_force, 0xFF);

    // Small motor on, large motor at 0xC0 (the "both" test).
    uint8_t both[34] = {0xFF, 0x01, 0xFF, 0xC0};
    sr::ds3_copy_output_body(both, sizeof(both), reinterpret_cast<uint8_t*>(&out), sizeof(out));
    CHECK_EQ(out.rumble.right_motor_on, 1);
    CHECK_EQ(out.rumble.left_motor_force, 0xC0);
}

TEST(ds3_full_body_is_copied_as_is) {
    // A host that sends the padding byte (35-byte body, e.g. via the interrupt endpoint).
    uint8_t body[35] = {0x00, 0xFF, 0x01, 0xFF, 0x80};
    PS3::OutReport out{};
    sr::ds3_copy_output_body(body, sizeof(body), reinterpret_cast<uint8_t*>(&out), sizeof(out));
    CHECK_EQ(out.rumble.right_motor_on, 1);
    CHECK_EQ(out.rumble.left_motor_force, 0x80);
}

// ---- PS4 -------------------------------------------------------------------------------------

TEST(ds4_output_body_of_31_bytes_is_accepted_and_aligned) {
    // Full report as Linux / SDL / our test script send it: ID 0x05, flags, 2 bytes, motors, RGB.
    uint8_t report[32] = {0x05, 0x07, 0x04, 0x00, 0x40 /* right */, 0xC0 /* left */, 0x12, 0x34, 0x56};
    PS4::OutReport out{};
    CHECK(sr::copy_output_body(0x05, report + 1, sizeof(report) - 1,
                               offsetof(PS4::OutReport, lightbar_blue), out));
    CHECK_EQ(out.report_id, 0x05);
    CHECK_EQ(out.motor_right, 0x40);
    CHECK_EQ(out.motor_left, 0xC0);
    CHECK_EQ(out.lightbar_red, 0x12);
    CHECK_EQ(out.lightbar_green, 0x34);
    CHECK_EQ(out.lightbar_blue, 0x56);
    CHECK(sr::ds4_rumble_valid(reinterpret_cast<const uint8_t*>(&out)[1]));
    CHECK(sr::ds4_led_valid(reinterpret_cast<const uint8_t*>(&out)[1]));
}

TEST(ds4_output_too_short_is_rejected) {
    uint8_t body[5] = {0x03, 0, 0, 0x10, 0x10};
    PS4::OutReport out{};
    CHECK(!sr::copy_output_body(0x05, body, sizeof(body), offsetof(PS4::OutReport, lightbar_blue), out));
}

TEST(ds4_lightbar_only_update_is_not_rumble) {
    CHECK(!sr::ds4_rumble_valid(0x02));
    CHECK(sr::ds4_led_valid(0x02));
    CHECK(sr::ds4_rumble_valid(0x01));
    CHECK(!sr::ds4_led_valid(0x01));
}

// ---- DualSense -------------------------------------------------------------------------------

TEST(ds5_output_body_is_aligned) {
    // USB report 0x02 (63 bytes): valid flags, motors, ..., lightbar at the end of the common part.
    uint8_t report[63]{};
    report[0] = 0x02;
    report[1] = 0x01;         // valid_flag0: compatible vibration
    report[2] = 0x04;         // valid_flag1: lightbar
    report[3] = 0x33;         // motor right
    report[4] = 0x99;         // motor left
    report[45] = 128;         // lightbar R G B (purple)
    report[46] = 0;
    report[47] = 255;
    PS5::OutReport out{};
    CHECK(sr::copy_output_body(0x02, report + 1, sizeof(report) - 1,
                               offsetof(PS5::OutReport, lightbar_blue), out));
    CHECK_EQ(out.motor_right, 0x33);
    CHECK_EQ(out.motor_left, 0x99);
    CHECK_EQ(out.lightbar_red, 128);
    CHECK_EQ(out.lightbar_green, 0);
    CHECK_EQ(out.lightbar_blue, 255);
    CHECK(sr::ds5_rumble_valid(out.control_flag[0], out.led_control_flag));
    CHECK(sr::ds5_lightbar_valid(out.control_flag[1]));
}

TEST(ds5_rumble_flags_as_hosts_send_them) {
    // SDL, rumbling on firmware it takes for 2.24+ (ours reports 0): haptics select in flag0,
    // "improved rumble" in flag2 (it used to be ignored: no rumble from Steam).
    CHECK(sr::ds5_rumble_valid(0x02, 0x04));
    // SDL on older firmware, and Linux hid-playstation v1: compatible vibration in flag0.
    CHECK(sr::ds5_rumble_valid(0x03, 0x00));
    // SDL stopping: every flag clear with zero motors. It used to be ignored (rumble stuck on).
    CHECK(!sr::ds5_rumble_valid(0x00, 0x00));
    CHECK(sr::ds5_rumble_stop(0x00, 0x00, 0x00));
    // Lightbar-only update (Linux): neither rumble nor stop, the running rumble stays.
    CHECK(!sr::ds5_rumble_valid(0x00, 0x00));
    CHECK(!sr::ds5_rumble_stop(0x00, sr::kDs5Valid1Lightbar, 0x00));
}

// ---- GET_REPORT ------------------------------------------------------------------------------

TEST(get_report_skips_the_id_tinyusb_adds) {
    const uint8_t report[5] = {0x12, 0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t buffer[8]{};
    CHECK_EQ(sr::copy_for_get_report(0x12, report, sizeof(report), buffer, sizeof(buffer)), 4u);
    CHECK_EQ(buffer[0], 0xAA);
    CHECK_EQ(buffer[3], 0xDD);
    // No report ID requested: the whole report.
    CHECK_EQ(sr::copy_for_get_report(0, report, sizeof(report), buffer, sizeof(buffer)), 5u);
    CHECK_EQ(buffer[0], 0x12);
    // Host asks for less.
    CHECK_EQ(sr::copy_for_get_report(0x12, report, sizeof(report), buffer, 2), 2u);
}

// ---- Input reports ---------------------------------------------------------------------------

TEST(idle_touch_points_are_not_touching) {
    uint8_t dst[8];
    std::memset(dst, 0, sizeof(dst));
    sr::put_touch_points(dst, nullptr, false);
    CHECK(dst[0] & 0x80);
    CHECK(dst[4] & 0x80);
}

TEST(touch_points_pass_through) {
    // Captured DS4 touch: id 5 at (777, 692), second point idle.
    const uint8_t raw[8] = {0x05, 0x09, 0x43, 0x2B, 0x80, 0, 0, 0};
    uint8_t dst[8]{};
    sr::put_touch_points(dst, raw, true);
    CHECK_EQ(dst[0] & 0x7F, 5);
    CHECK_EQ(dst[1] | ((dst[2] & 0x0F) << 8), 777);
    CHECK_EQ((dst[2] >> 4) | (dst[3] << 4), 692);
    CHECK(dst[4] & 0x80);
}

TEST(battery_status_bytes) {
    CHECK_EQ(sr::ds4_status(0), 0x1B);     // unknown: cabled, full
    CHECK_EQ(sr::ds5_status(0), 0x2A);     // unknown: full
    // Bluepad32 reports a DS4 at level n as n * 25 + 1.
    for (int n = 0; n <= 10; ++n) {
        const uint8_t b = static_cast<uint8_t>(n * 25 + 1);
        CHECK_EQ(sr::ds4_status(b), n);
        CHECK_EQ(sr::ds5_status(b) & 0x0F, n);
    }
}

TEST(sensor_clocks_follow_real_time) {
    // DS4: 16/3 us per count, so 1 ms = 187.5 counts (hosts saw ~5 us per sample before).
    const uint16_t t0 = sr::ds4_sensor_timestamp(1000000);
    const uint16_t t1 = sr::ds4_sensor_timestamp(1004000);
    CHECK_EQ(static_cast<uint16_t>(t1 - t0), 750);
    // DualSense: 1/3 us per count.
    CHECK_EQ(sr::ds5_sensor_timestamp(4000) - sr::ds5_sensor_timestamp(0), 12000u);
}

TEST(ds4_motion_lands_at_the_report_offsets) {
    uint8_t rep[64]{};
    const int32_t gyro[3] = {90 * 1024, -45 * 1024, 0};
    const int32_t accel[3] = {0, 8192, -8192};
    sr::put_motion(rep, sr::ds4::kGyro, sr::ds4::kAccel, gyro, accel, sony_imu::kRealUnits);
    CHECK_EQ(le16(rep, 13), 90 * 16);
    CHECK_EQ(le16(rep, 15), -45 * 16);
    CHECK_EQ(le16(rep, 21), 8192);
    CHECK_EQ(le16(rep, 23), -8192);
}

TEST(ds5_synthesized_report_fields) {
    uint8_t rep[64]{};
    const uint8_t touch[8] = {0x02, 0x10, 0x20, 0x30, 0x80, 0, 0, 0};
    sr::Ds5SynthInput in{};
    in.seq = 42;
    in.has_motion = true;
    in.gyro[0] = 1024;         // 1 deg/s
    in.accel[1] = 8192;        // 1 g
    in.time_us = 1000;
    in.touch_raw = touch;
    in.touch_valid = true;
    in.touch_click = true;
    in.battery = 5 * 25 + 1;
    sr::ds5_fill_synth(rep, in);
    CHECK_EQ(rep[7], 42);                              // sequence
    CHECK_EQ(le16(rep, 16), 16);                       // gyro x
    CHECK_EQ(le16(rep, 24), 8192);                     // accel y
    CHECK_EQ(rep[28] | (rep[29] << 8), 3000);          // clock, 1/3 us
    CHECK_EQ(rep[33], 0x02);                           // touch point 0
    CHECK(rep[37] & 0x80);                             // point 1 idle
    CHECK(rep[10] & sr::kTouchpadClick);               // click bit
    CHECK_EQ(rep[53], 5);                              // battery, discharging
}

TEST(ds5_synthesized_without_touch_or_motion) {
    uint8_t rep[64]{};
    sr::Ds5SynthInput in{};
    sr::ds5_fill_synth(rep, in);
    CHECK(rep[33] & 0x80);
    CHECK(rep[37] & 0x80);
    CHECK_EQ(le16(rep, 16), 0);
    CHECK_EQ(rep[53], 0x2A);
    CHECK(!(rep[10] & sr::kTouchpadClick));
}

TEST_MAIN()
