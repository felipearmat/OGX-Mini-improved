#ifndef _OGXM_CUSTOM_SONY_IMU_H_
#define _OGXM_CUSTOM_SONY_IMU_H_

#include <cstdint>

/*  Motion for the emulated DualShock 4 (PS4 mode) and DualSense (STEAM mode), custom addition.
 *
 *  Input motion (Bluepad32 / wired hosts) is 1024 per deg/s and 8192 per g. Reports carry real
 *  controller units, 16 per deg/s and 8192 per g, and the calibration feature report describes
 *  an ideal controller with those units. Hosts that apply the calibration (Linux
 *  hid-playstation, SDL) and hosts that assume real units (Steam) then read the same values:
 *    deg/s = raw * (speed_plus + speed_minus) / (|plus - bias| + |minus - bias|)
 *    g     = (raw - bias) * 2 / (acc_plus - acc_minus)
 */
namespace sony_imu {

    constexpr int32_t kGyroDiv = 64;    // 1024 -> 16 per deg/s
    constexpr int32_t kAccelDiv = 1;    // 8192 per g either way

    constexpr int16_t kCalGyroPlus = 8640;   // (540 + 540) / (2 * 8640) = 1/16 deg/s per count
    constexpr int16_t kCalGyroSpeed = 540;   // a real pad reports about 540 and +-8700
    constexpr int16_t kCalAccelPlus = 8192;  // 2 g over 16384 counts

    // Rounded division clamped to int16 (truncation zeroed small Bluetooth samples).
    inline int16_t scale(int32_t v, int32_t div)
    {
        const int64_t d = div > 0 ? div : 1;
        const int64_t q = v >= 0 ? (v + d / 2) / d : (v - d / 2) / d;
        return static_cast<int16_t>(q > 32767 ? 32767 : (q < -32768 ? -32768 : q));
    }

    inline void put_le16(uint8_t* report, int offset, int16_t v)
    {
        report[offset] = static_cast<uint8_t>(v & 0xFF);
        report[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    }

    // Calibration feature body, report ID at [0] (DS4 USB 0x02 and DualSense 0x05 share it):
    // biases at [1..6], gyro pitch+/pitch-/yaw+/yaw-/roll+/roll- at [7..18], speed at [19..22],
    // accel x+/x-/y+/y-/z+/z- at [23..34].
    inline void fill_calibration(uint8_t* report)
    {
        for (int axis = 0; axis < 3; ++axis) {
            put_le16(report, 1 + axis * 2, 0);
            put_le16(report, 7 + axis * 4, kCalGyroPlus);
            put_le16(report, 9 + axis * 4, -kCalGyroPlus);
            put_le16(report, 23 + axis * 4, kCalAccelPlus);
            put_le16(report, 25 + axis * 4, -kCalAccelPlus);
        }
        put_le16(report, 19, kCalGyroSpeed);
        put_le16(report, 21, kCalGyroSpeed);
    }

} // namespace sony_imu

#endif // _OGXM_CUSTOM_SONY_IMU_H_
