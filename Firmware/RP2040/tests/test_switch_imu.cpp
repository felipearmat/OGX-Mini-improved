// Motion conversion for the emulated Switch Pro Controller (Phase 2a).
#include "Custom/SwitchImu.h"
#include "test.h"

namespace {

Gamepad::PadIn pad(uint8_t source, int32_t ax, int32_t ay, int32_t az, int32_t gx, int32_t gy, int32_t gz) {
    Gamepad::PadIn in;
    in.motion_source = source;
    in.accel[0] = ax; in.accel[1] = ay; in.accel[2] = az;
    in.gyro[0] = gx;  in.gyro[1] = gy;  in.gyro[2] = gz;
    return in;
}

int16_t le16(const uint8_t* p) { return static_cast<int16_t>(p[0] | (p[1] << 8)); }

}  // namespace

TEST(no_motion_source_gives_no_sample) {
    switch_imu::Sample s;
    Gamepad::PadIn in = pad(Gamepad::PadIn::MOTION_SRC_NONE, 100, 100, 100, 5, 5, 5);
    CHECK(!switch_imu::to_switch_sample(in, s));
    CHECK_EQ(s.accel[0], 0);
    CHECK_EQ(s.gyro[2], 0);
}

TEST(switch_sources_pass_through_in_raw_counts) {
    // Bluepad32 Switch parser: accel in raw counts, gyro in raw counts * 1000.
    switch_imu::Sample s;
    CHECK(switch_imu::to_switch_sample(
        pad(Gamepad::PadIn::MOTION_SRC_SWITCH_PRO, 4096, -100, 7, 1428000, -2000, 499), s));
    CHECK_EQ(s.accel[0], 4096);
    CHECK_EQ(s.accel[1], -100);
    CHECK_EQ(s.accel[2], 7);
    CHECK_EQ(s.gyro[0], 1428);
    CHECK_EQ(s.gyro[1], -2);
    CHECK_EQ(s.gyro[2], 0);
}

TEST(ds4_is_scaled_to_switch_units_and_axes) {
    // 1 g on DS4 Y (8192) and 100 deg/s on DS4 X (102400).
    switch_imu::Sample s;
    CHECK(switch_imu::to_switch_sample(pad(Gamepad::PadIn::MOTION_SRC_DS4, 0, 8192, 0, 102400, 0, 0), s));
    // DS4 (x, y, z) -> Pro (-y, -x, -z)
    CHECK_EQ(s.accel[0], -4096);  // 1 g = 4096 counts
    CHECK_EQ(s.accel[1], 0);
    CHECK_EQ(s.gyro[1], -1429);   // 100 dps * 13371 / 936 = 1428.5
    CHECK_EQ(s.gyro[0], 0);
}

TEST(values_saturate_instead_of_wrapping) {
    switch_imu::Sample s;
    CHECK(switch_imu::to_switch_sample(
        pad(Gamepad::PadIn::MOTION_SRC_SWITCH_PRO, 100000, -100000, 0, 0, 0, 0), s));
    CHECK_EQ(s.accel[0], 32767);
    CHECK_EQ(s.accel[1], -32768);
}

TEST(factory_calibration_has_zero_offsets_and_matching_scales) {
    const uint8_t* c = switch_imu::kFactoryCalibration;
    for (int i = 0; i < 3; ++i) {
        CHECK_EQ(le16(c + 0 + 2 * i), 0);                              // accel offsets
        CHECK_EQ(le16(c + 6 + 2 * i), 16384);                          // accel scale: 4 g / 16384 counts
        CHECK_EQ(le16(c + 12 + 2 * i), 0);                             // gyro offsets
        CHECK_EQ(le16(c + 18 + 2 * i), switch_imu::kGyroScale);        // gyro scale
    }
    CHECK_EQ(switch_imu::kAccelCountsPerG * 4, 16384);
}

TEST_MAIN()
