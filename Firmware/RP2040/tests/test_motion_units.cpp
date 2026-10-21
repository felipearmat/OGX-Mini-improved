// Motion units and axes into the Sony outputs (Gamepad/MotionImu.h, then
// USBDevice/DeviceDriver/Sony/SonyImu.h): every source ends up at the same deg/s and g. Switch
// sources used to be only rotated, so a Joy-Con's gyro reached STEAM / PS4 / PS3 mode about 14
// times too strong and gravity at half; and for PS4 / STEAM they landed in the Sixaxis-style
// frame (gravity on Z), so Steam read a Joy-Con lying flat as one held upright.
#include "Gamepad/MotionImu.h"
#include "USBDevice/DeviceDriver/Sony/SonyImu.h"
#include "test.h"

namespace {

// A Switch source as Bluepad32 gives it: gyro raw * 1000, raw = deg/s * 13371 / 936.
int32_t switch_gyro(double deg_s) { return static_cast<int32_t>(deg_s * 13371.0 / 936.0 * 1000.0); }

// What the DualSense / DS4 report carries (real units: 16 per deg/s, 8192 per g).
int16_t report_gyro(int32_t ds4_units) { return sony_imu::scale(ds4_units, sony_imu::kRealUnits.gyro_div); }

}  // namespace

TEST(a_joycon_turning_90_deg_s_reads_90_deg_s_in_the_sony_outputs) {
    int32_t accel[3] = {0, 0, 4096};             // Switch raw: 1 g on Z (face up)
    int32_t gyro[3] = {0, 0, switch_gyro(90.0)}; // turning about Z (on the table)
    MotionImu::remap_to_ds4_playing_frame(Gamepad::PadIn::MOTION_SRC_SWITCH_PRO, accel, gyro);
    CHECK_EQ(accel[2], -8192);                    // PS3's Sixaxis-style frame: 1 g on Z
    const int16_t yaw = report_gyro(gyro[2]);
    CHECK(yaw <= -1438 && yaw >= -1442);          // 90 deg/s * 16, not ~20000
}

TEST(a_joycon_lying_flat_reads_as_a_dualsense_lying_flat) {
    // DS4 / DualSense report frame: X right, Y out of the face, Z towards the player.
    int32_t accel[3] = {0, 0, 4096};             // face up
    int32_t gyro[3] = {0, 0, switch_gyro(90.0)}; // turning on the table
    MotionImu::remap_to_ds4_report_frame(Gamepad::PadIn::MOTION_SRC_SWITCH_PRO, accel, gyro);
    CHECK_EQ(accel[0], 0);
    CHECK_EQ(accel[1], 8192);                     // +1 g on Y, as a real pad face up
    CHECK_EQ(accel[2], 0);
    const int16_t yaw = report_gyro(gyro[1]);     // the turn is gyro Y
    CHECK(yaw >= 1438 && yaw <= 1442);
    CHECK_EQ(gyro[0], 0);
    CHECK_EQ(gyro[2], 0);
    // Pro X (front) is the DualSense's -Z (away from the player), Pro Y (left) its -X.
    int32_t a2[3] = {4096, 2048, 0}, g2[3] = {0, 0, 0};
    MotionImu::remap_to_ds4_report_frame(Gamepad::PadIn::MOTION_SRC_SWITCH_USB, a2, g2);
    CHECK_EQ(a2[0], -4096);
    CHECK_EQ(a2[2], -8192);
}

TEST(the_wired_switch_source_is_converted_too) {
    int32_t accel[3] = {4096, 0, 0};
    int32_t gyro[3] = {switch_gyro(-45.0), 0, 0};
    MotionImu::remap_to_ds4_playing_frame(Gamepad::PadIn::MOTION_SRC_SWITCH_USB, accel, gyro);
    CHECK_EQ(accel[1], -8192);
    const int16_t pitch = report_gyro(gyro[1]);
    CHECK(pitch >= 718 && pitch <= 722);          // 45 deg/s * 16
}

TEST(ds4_units_pass_unchanged) {
    int32_t accel[3] = {100, -8192, 7};
    int32_t gyro[3] = {1024 * 30, -1024, 5};
    MotionImu::remap_to_ds4_report_frame(Gamepad::PadIn::MOTION_SRC_DS4, accel, gyro);
    CHECK_EQ(accel[1], -8192);
    CHECK_EQ(gyro[0], 1024 * 30);
    CHECK_EQ(report_gyro(gyro[0]), 480);          // 30 deg/s * 16
}

TEST_MAIN()
