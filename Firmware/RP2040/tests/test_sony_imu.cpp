// Emulated DS4 / DualSense motion units vs. the calibration report hosts apply (USBDevice/DeviceDriver/Sony/SonyImu).
#include "USBDevice/DeviceDriver/Sony/SonyImu.h"
#include "test.h"

namespace {

int16_t le16(const uint8_t* r, int o) { return static_cast<int16_t>(r[o] | (r[o + 1] << 8)); }

// What Linux hid-playstation / SDL compute from the calibration report.
double host_deg_per_s(const uint8_t* cal, int axis, int16_t raw) {
    const int bias = le16(cal, 1 + axis * 2);
    const int plus = le16(cal, 7 + axis * 4), minus = le16(cal, 9 + axis * 4);
    const int speed_2x = le16(cal, 19) + le16(cal, 21);
    const int denom = (plus > bias ? plus - bias : bias - plus) + (minus > bias ? minus - bias : bias - minus);
    return static_cast<double>(raw) * speed_2x / denom;
}

double host_g(const uint8_t* cal, int axis, int16_t raw) {
    const int plus = le16(cal, 23 + axis * 4), minus = le16(cal, 25 + axis * 4);
    const int range_2g = plus - minus;
    const int bias = plus - range_2g / 2;
    return (raw - bias) * 2.0 / range_2g;
}

bool near(double a, double b) { return a - b < 0.01 && b - a < 0.01; }

}  // namespace

TEST(calibration_matches_report_units) {
    uint8_t cal[37] = {0x02};
    sony_imu::fill_calibration(cal);
    for (int axis = 0; axis < 3; ++axis) {
        // 90 deg/s in (1024 per deg/s) reads as 90 deg/s on the host.
        CHECK(near(host_deg_per_s(cal, axis, sony_imu::scale(90 * 1024, sony_imu::kGyroDiv)), 90.0));
        CHECK(near(host_deg_per_s(cal, axis, sony_imu::scale(-45 * 1024, sony_imu::kGyroDiv)), -45.0));
        // 1 g in (8192 per g) reads as 1 g.
        CHECK(near(host_g(cal, axis, sony_imu::scale(8192, sony_imu::kAccelDiv)), 1.0));
        CHECK(near(host_g(cal, axis, sony_imu::scale(-8192, sony_imu::kAccelDiv)), -1.0));
    }
}

TEST(legacy_ps4_calibration_matches_its_units) {
    const sony_imu::MotionScale& legacy = sony_imu::kLegacyPs4;
    uint8_t cal[37] = {0x02};
    sony_imu::fill_calibration(cal, legacy);
    for (int axis = 0; axis < 3; ++axis) {
        CHECK(near(host_deg_per_s(cal, axis, sony_imu::scale(90 * 1024, legacy.gyro_div)), 90.0));
        CHECK(near(host_g(cal, axis, sony_imu::scale(8192, legacy.accel_div)), 1.0));
    }
    CHECK_EQ(sony_imu::scale(90 * 1024, legacy.gyro_div), 90 * 128);  // Brook-style values
}

TEST(real_units_without_calibration) {
    // Steam treats the values as a real pad's: 16 per deg/s, 8192 per g.
    CHECK_EQ(sony_imu::scale(90 * 1024, sony_imu::kGyroDiv), 90 * 16);
    CHECK_EQ(sony_imu::scale(8192, sony_imu::kAccelDiv), 8192);
}

TEST(scale_rounds_and_clamps) {
    CHECK_EQ(sony_imu::scale(32, 64), 1);     // rounds half away from zero
    CHECK_EQ(sony_imu::scale(-32, 64), -1);
    CHECK_EQ(sony_imu::scale(31, 64), 0);
    CHECK_EQ(sony_imu::scale(40000, 1), 32767);
    CHECK_EQ(sony_imu::scale(-40000, 1), -32768);
}

TEST_MAIN()
