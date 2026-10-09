// Motion sensors as input for the idle turn-off (Bluepad32/IdleMotion.h): offsets, noise, spikes,
// stuck sensors and the controller's own rumble are not use; a clear rotation or tilt is.
#include <cmath>
#include <cstdint>

#include "Bluepad32/IdleMotion.h"
#include "test.h"

namespace {

constexpr uint32_t kStepMs = 4;  // a DS4 over Bluetooth: 250 reports per second

struct Noise {
    uint32_t state = 12345;
    int32_t next(int32_t amplitude)  // -amplitude..amplitude
    {
        state = state * 1103515245u + 12345u;
        return static_cast<int32_t>((state >> 8) % static_cast<uint32_t>(2 * amplitude + 1)) - amplitude;
    }
};

// Feeds reports from from_ms to to_ms; true if any of them counted as motion.
template <typename Fn>
bool feed(idle_motion::Detector& d, uint32_t from_ms, uint32_t to_ms, Fn sample, bool rumbling = false)
{
    bool moved = false;
    for (uint32_t t = from_ms; t < to_ms; t += kStepMs) {
        int32_t gyro[3], accel[3];
        sample(t, gyro, accel);
        moved |= d.update(gyro, accel, t, rumbling);
    }
    return moved;
}

void at_rest(uint32_t, int32_t g[3], int32_t a[3], Noise& n, int32_t gyro_offset, int32_t gyro_noise)
{
    g[0] = gyro_offset + n.next(gyro_noise);
    g[1] = -gyro_offset / 2 + n.next(gyro_noise);
    g[2] = 7 + n.next(gyro_noise);
    a[0] = n.next(40);
    a[1] = 8192 + n.next(40);  // 1 g
    a[2] = n.next(40);
}

} // namespace

TEST(a_controller_at_rest_with_offset_and_noise_is_not_handled) {
    idle_motion::Detector d;
    Noise n;
    CHECK(!feed(d, 0, 20000, [&](uint32_t t, int32_t g[3], int32_t a[3]) { at_rest(t, g, a, n, 40, 20); }));
}

TEST(an_uncalibrated_gyro_with_a_large_offset_and_noise_is_not_handled) {
    idle_motion::Detector d;
    Noise n;
    CHECK(!feed(d, 0, 20000, [&](uint32_t t, int32_t g[3], int32_t a[3]) { at_rest(t, g, a, n, 3000, 120); }));
}

TEST(single_spikes_are_not_handled) {
    idle_motion::Detector d;
    Noise n;
    CHECK(!feed(d, 0, 20000, [&](uint32_t t, int32_t g[3], int32_t a[3]) {
        at_rest(t, g, a, n, 40, 20);
        if (t % 500 == 0 && t > 3000)
            g[0] += 8000;  // one report every half second
    }));
}

TEST(a_sensor_stuck_at_an_extreme_value_is_not_handled) {
    idle_motion::Detector d;
    CHECK(!feed(d, 0, 20000, [&](uint32_t, int32_t g[3], int32_t a[3]) {
        for (int i = 0; i < 3; ++i) {
            g[i] = 32767;
            a[i] = -32768;
        }
    }));
}

TEST(a_very_noisy_accelerometer_is_not_used) {
    idle_motion::Detector d;
    Noise n;
    CHECK(!feed(d, 0, 20000, [&](uint32_t t, int32_t g[3], int32_t a[3]) {
        at_rest(t, g, a, n, 40, 20);
        a[0] = n.next(8000);
        a[1] = 8192 + n.next(8000);
        a[2] = n.next(8000);
    }));
}

TEST(no_motion_sensors_never_count) {
    idle_motion::Detector d;
    CHECK(!feed(d, 0, 20000, [&](uint32_t, int32_t g[3], int32_t a[3]) {
        for (int i = 0; i < 3; ++i)
            g[i] = a[i] = 0;
    }));
}

TEST(the_controllers_own_rumble_is_not_handled) {
    idle_motion::Detector d;
    Noise n;
    auto rest = [&](uint32_t t, int32_t g[3], int32_t a[3]) { at_rest(t, g, a, n, 40, 20); };
    auto shaking = [&](uint32_t t, int32_t g[3], int32_t a[3]) {
        at_rest(t, g, a, n, 40, 20);
        for (int i = 0; i < 3; ++i) {
            g[i] += n.next(1500);
            a[i] += n.next(1500);
        }
    };
    CHECK(!feed(d, 0, 3000, rest));
    CHECK(!feed(d, 3000, 6000, shaking, true));   // rumbling
    CHECK(!feed(d, 6000, 6400, shaking, false));  // the motors winding down
    CHECK(!feed(d, 6400, 12000, rest));
}

TEST(a_clear_rotation_is_handled) {
    idle_motion::Detector d;
    Noise n;
    auto rest = [&](uint32_t t, int32_t g[3], int32_t a[3]) { at_rest(t, g, a, n, 40, 20); };
    CHECK(!feed(d, 0, 3000, rest));
    CHECK(feed(d, 3000, 3300, [&](uint32_t t, int32_t g[3], int32_t a[3]) {
        rest(t, g, a);
        g[2] += 1600;  // about 100 deg/s on a DS4
    }));
}

TEST(a_short_rotation_is_not_enough) {
    idle_motion::Detector d;
    Noise n;
    auto rest = [&](uint32_t t, int32_t g[3], int32_t a[3]) { at_rest(t, g, a, n, 40, 20); };
    CHECK(!feed(d, 0, 3000, rest));
    CHECK(!feed(d, 3000, 3100, [&](uint32_t t, int32_t g[3], int32_t a[3]) {
        rest(t, g, a);
        g[2] += 1600;  // 100 ms: shorter than kSustainMs
    }));
}

TEST(a_tilt_is_handled) {
    idle_motion::Detector d;
    Noise n;
    CHECK(!feed(d, 0, 3000, [&](uint32_t t, int32_t g[3], int32_t a[3]) { at_rest(t, g, a, n, 40, 20); }));
    // Picked up: gravity turns 25 degrees over half a second (the gyro left quiet on purpose).
    CHECK(feed(d, 3000, 3600, [&](uint32_t t, int32_t g[3], int32_t a[3]) {
        at_rest(t, g, a, n, 40, 20);
        const float angle = 25.0f * 3.14159265f / 180.0f * static_cast<float>(t - 3000 > 500 ? 500 : t - 3000) / 500.0f;
        a[0] = static_cast<int32_t>(8192.0f * std::sin(angle));
        a[1] = static_cast<int32_t>(8192.0f * std::cos(angle));
    }));
}

TEST(a_gyro_offset_that_jumps_stops_counting) {
    idle_motion::Detector d;
    Noise n;
    CHECK(!feed(d, 0, 3000, [&](uint32_t t, int32_t g[3], int32_t a[3]) { at_rest(t, g, a, n, 40, 20); }));
    auto jumped = [&](uint32_t t, int32_t g[3], int32_t a[3]) {
        at_rest(t, g, a, n, 40, 20);
        g[0] += 2000;  // the offset moved and stays there
    };
    feed(d, 3000, 13000, jumped);              // may count while it settles
    CHECK(!feed(d, 13000, 25000, jumped));     // then it is the new rest level
}

TEST_MAIN()
