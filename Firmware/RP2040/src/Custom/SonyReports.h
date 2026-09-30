#ifndef _OGXM_CUSTOM_SONY_REPORTS_H_
#define _OGXM_CUSTOM_SONY_REPORTS_H_

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "Custom/SonyImu.h"

/*  Report handling shared by the emulated DualShock 3 / 4 and DualSense (custom addition), kept
 *  free of TinyUSB so host tests cover the fixes it carries. Offsets are within the full report,
 *  report ID at [0], as Linux hid-sony / hid-playstation read it.
 */
namespace sony_reports {

    /* ---- Host -> device (output reports) ------------------------------------------------ */

    /* Copy an output report body (bytes after the report ID) into a struct that starts with the
     * ID field. The PS4 / STEAM drivers required the struct size (ID included) after the ID, so
     * every USB output report was dropped, and a longer one would have been copied over the ID
     * field (every setting one byte off). Fails when the body is shorter than min_body. */
    template <typename Out>
    bool copy_output_body(uint8_t report_id, const uint8_t* body, size_t len, size_t min_body, Out& out)
    {
        if (body == nullptr || len < min_body)
            return false;
        out = Out();
        uint8_t* dst = reinterpret_cast<uint8_t*>(&out);
        dst[0] = report_id;
        const size_t n = len < sizeof(Out) - 1 ? len : sizeof(Out) - 1;
        std::memcpy(dst + 1, body, n);
        return true;
    }

    /* DualShock 3 output report 0x01 body into a struct without the report ID. Linux hid-sony
     * sends it by SET_REPORT without the ID byte, so the body starts with a padding byte set to
     * 0x01; TinyUSB takes that for the report ID and strips it, so the body arrives one byte short
     * (34 bytes) and must be shifted back. Returns the bytes used from body. */
    constexpr size_t kDs3OutputBodySize = 35;   // hid-sony sixaxis_output_report minus the ID

    inline size_t ds3_copy_output_body(const uint8_t* body, size_t len, uint8_t* out, size_t out_size)
    {
        if (body == nullptr || out == nullptr || out_size == 0)
            return 0;
        size_t dst = 0;
        if (len == kDs3OutputBodySize - 1) {
            out[0] = 0x01;   // the stripped padding byte
            dst = 1;
        }
        const size_t n = len < out_size - dst ? len : out_size - dst;
        std::memcpy(out + dst, body, n);
        return n;
    }

    /* DS4 output report 0x05, byte 1: which parts the host set. */
    constexpr uint8_t kDs4ValidRumble = 0x01;
    constexpr uint8_t kDs4ValidLed = 0x02;
    /* DualSense output report 0x02 valid flags (hid-playstation DS_OUTPUT_VALID_FLAG*). */
    constexpr uint8_t kDs5Valid0CompatibleVibration = 0x01;
    constexpr uint8_t kDs5Valid0HapticsSelect = 0x02;
    constexpr uint8_t kDs5Valid1Lightbar = 0x04;
    constexpr uint8_t kDs5Valid2CompatibleVibration2 = 0x04;   // "improved rumble" (SDL, kernel v2)

    // A lightbar-only update carries zero motor bytes: only take rumble the host marked valid.
    inline bool ds4_rumble_valid(uint8_t flags) { return (flags & kDs4ValidRumble) != 0; }
    inline bool ds4_led_valid(uint8_t flags) { return (flags & kDs4ValidLed) != 0; }
    /* DualSense rumble, following SDL, Linux hid-playstation and inputtino (Sunshine):
     *  - motor bytes are valid with compatible vibration (flag0 bit 0) or its v2 form (flag2
     *    bit 2, which SDL uses on firmware it takes for 2.24+, including ours: version 0);
     *  - a report with every valid flag clear is SDL's "stop rumble" (it clears the rumble bits
     *    once the rumble ends), so it stops the motors;
     *  - anything else (e.g. a lightbar-only update) leaves the current rumble alone. */
    inline bool ds5_rumble_valid(uint8_t flag0, uint8_t flag2)
    {
        return (flag0 & kDs5Valid0CompatibleVibration) != 0 ||
               (flag2 & kDs5Valid2CompatibleVibration2) != 0;
    }
    inline bool ds5_rumble_stop(uint8_t flag0, uint8_t flag1, uint8_t flag2)
    {
        return flag0 == 0 && flag1 == 0 && flag2 == 0;
    }
    inline bool ds5_lightbar_valid(uint8_t flag1) { return (flag1 & kDs5Valid1Lightbar) != 0; }

    /* ---- GET_REPORT ----------------------------------------------------------------------- */

    /* TinyUSB already put the report ID in front when the host asked for a specific one; copy
     * the rest of a full report (ID at [0]). The drivers used to repeat the ID. */
    inline size_t copy_for_get_report(uint8_t requested_id, const uint8_t* report, size_t report_len,
                                      uint8_t* buffer, size_t reqlen)
    {
        const size_t skip = requested_id == 0 ? 0 : 1;
        if (report_len < skip)
            return 0;
        const size_t avail = report_len - skip;
        const size_t n = reqlen < avail ? reqlen : avail;
        std::memcpy(buffer, report + skip, n);
        return n;
    }

    /* ---- Device -> host (input reports) --------------------------------------------------- */

    /* Two touch points of 4 bytes (DS4 and DualSense share the format). Bit 7 of a point's
     * first byte set = not touching; the all-zero report showed two fingers resting at (0, 0) to
     * hosts that read the first touch report directly (SDL / Steam). */
    inline void put_touch_points(uint8_t* dst, const uint8_t touch_raw[8], bool valid)
    {
        if (valid) {
            std::memcpy(dst, touch_raw, 8);
        } else {
            std::memset(dst, 0, 8);
            dst[0] = 0x80;
            dst[4] = 0x80;
        }
    }

    /* Battery 1..255 (Bluepad32 scale, 0 = unknown) -> 0..10. */
    inline uint8_t battery_level_0_10(uint8_t battery)
    {
        return static_cast<uint8_t>((battery * 10u + 127u) / 255u);
    }

    /* DS4 status byte: level in the low nibble, bit 4 = cable. Unknown: cabled and full (11). */
    inline uint8_t ds4_status(uint8_t battery)
    {
        return battery == 0 ? 0x1B : battery_level_0_10(battery);
    }

    /* DualSense status byte: level in the low nibble, charging state in the high one (2 = full).
     * Unknown: full. */
    inline uint8_t ds5_status(uint8_t battery)
    {
        return battery == 0 ? 0x2A : battery_level_0_10(battery);
    }

    /* Motion sensor clocks: DS4 in units of 16/3 us (16 bits), DualSense in 1/3 us (32 bits).
     * Hosts derive the sample interval from them; the DS4 one used to be a per-call counter. */
    inline uint16_t ds4_sensor_timestamp(uint64_t time_us)
    {
        return static_cast<uint16_t>(time_us * 3u / 16u);
    }
    inline uint32_t ds5_sensor_timestamp(uint64_t time_us)
    {
        return static_cast<uint32_t>(time_us * 3u);
    }

    /* Motion (input units: 1024 per deg/s, 8192 per g) into int16 LE fields. */
    inline void put_motion(uint8_t* report, int gyro_offset, int accel_offset,
                           const int32_t gyro[3], const int32_t accel[3], const sony_imu::MotionScale& scale)
    {
        for (int i = 0; i < 3; ++i) {
            sony_imu::put_le16(report, gyro_offset + i * 2, sony_imu::scale(gyro[i], scale.gyro_div));
            sony_imu::put_le16(report, accel_offset + i * 2, sony_imu::scale(accel[i], scale.accel_div));
        }
    }

    /* DS4 USB input report 0x01 offsets. */
    namespace ds4 {
        constexpr int kButtons2 = 7;
        constexpr int kSensorTimestamp = 10;
        constexpr int kGyro = 13;
        constexpr int kAccel = 19;
        constexpr int kStatus = 30;
        constexpr int kTouchCount = 33;
        constexpr int kTouchTimestamp = 34;
        constexpr int kTouchPoints = 35;
    }
    /* DualSense USB input report 0x01 offsets (PS5::InReport lacks the 4 reserved bytes after the
     * buttons, so it only fits up to them). */
    namespace ds5 {
        constexpr int kSeq = 7;
        constexpr int kButtons2 = 10;
        constexpr int kGyro = 16;
        constexpr int kAccel = 22;
        constexpr int kSensorTimestamp = 28;
        constexpr int kTouchPoints = 33;
        constexpr int kStatus = 53;
    }
    constexpr uint8_t kTouchpadClick = 0x02;   // buttons[2] bit on both pads

    /* What a synthesized DualSense report (STEAM mode, any pad but a real DualSense) used to
     * leave at zero: sequence, motion (already in the DS4 playing frame; skipped when absent),
     * sensor clock, touch points and click, battery status. */
    struct Ds5SynthInput {
        uint8_t seq;
        bool has_motion;
        int32_t gyro[3];
        int32_t accel[3];
        uint64_t time_us;
        const uint8_t* touch_raw;   // 8 bytes, or null
        bool touch_valid;
        bool touch_click;
        uint8_t battery;
    };

    inline void ds5_fill_synth(uint8_t* report, const Ds5SynthInput& in)
    {
        report[ds5::kSeq] = in.seq;
        if (in.has_motion)
            put_motion(report, ds5::kGyro, ds5::kAccel, in.gyro, in.accel, sony_imu::kRealUnits);
        const uint32_t ts = ds5_sensor_timestamp(in.time_us);
        std::memcpy(&report[ds5::kSensorTimestamp], &ts, sizeof(ts));
        const bool touch = in.touch_valid && in.touch_raw != nullptr;
        put_touch_points(&report[ds5::kTouchPoints], in.touch_raw, touch);
        if (touch && in.touch_click)
            report[ds5::kButtons2] |= kTouchpadClick;
        report[ds5::kStatus] = ds5_status(in.battery);
    }

} // namespace sony_reports

#endif // _OGXM_CUSTOM_SONY_REPORTS_H_
