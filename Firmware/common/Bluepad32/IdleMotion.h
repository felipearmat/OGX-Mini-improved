#ifndef _OGXM_IDLE_MOTION_H_
#define _OGXM_IDLE_MOTION_H_

#include <cmath>
#include <cstdint>

/*  Custom: whether a controller's motion sensors show it being handled, for the idle turn-off of
 *  Bluetooth controllers (Bluepad32.cpp). Readings are compared with what the same sensor shows
 *  while still, never with zero or with a fixed scale, so these do not count as use:
 *    - a sensor with a fixed offset (an uncalibrated gyro reading a few deg/s at rest, an
 *      accelerometer with a wrong scale): the gyro rest level is learnt and followed slowly, and
 *      the accelerometer is used only for the direction of gravity;
 *    - noise (a DS4 gyro without any stabilisation): a reading counts only well above the noise
 *      level learnt for that sensor, and only when it lasts (kSustainMs), so single spikes and
 *      a sensor stuck at an extreme value do not count either; an accelerometer whose noise is
 *      large next to gravity is not used;
 *    - the controller's own rumble, which shakes both sensors: ignored while the host asks for
 *      rumble and for kRumbleQuietMs after.
 *  What counts: the controller's tilt changing by more than ~10 degrees from where it rested, or a
 *  rotation clearly above the noise for kSustainMs.
 *  Units are whatever the controller reports (Bluepad32 raw counts); a pad without motion sensors
 *  reports zeros and never counts. */
namespace idle_motion {

    constexpr uint32_t kLearnMs = 2000;        // after connecting: learn the rest level and noise
    constexpr uint32_t kSustainMs = 150;       // a rotation must last this long
    constexpr uint32_t kRumbleQuietMs = 500;   // after the rumble stops
    constexpr float kTiltCos = 0.9848f;        // cos(10 degrees)
    constexpr float kGyroNoiseFactor = 4.0f;   // rotation = deviation above 4x the learnt noise...
    constexpr float kGyroFloor = 64.0f;        // ...and above this (a few deg/s on DS4 / Switch pads)
    constexpr float kAccelNoisy = 0.5f;        // accelerometer noise above half of gravity: unused

    class Detector {
    public:
        void reset() { *this = Detector{}; }

        // One input report. True when it shows the controller being handled.
        bool update(const int32_t gyro[3], const int32_t accel[3], uint32_t now_ms, bool rumbling)
        {
            if (!started_) {
                started_ = true;
                start_ms_ = now_ms;
                for (int i = 0; i < 3; ++i) {
                    gyro_rest_[i] = static_cast<float>(gyro[i]);
                    accel_avg_[i] = static_cast<float>(accel[i]);
                }
                return false;
            }

            float accel_dev = 0.0f;
            for (int i = 0; i < 3; ++i) {
                const float a = static_cast<float>(accel[i]);
                accel_dev += std::fabs(a - accel_avg_[i]);
                accel_avg_[i] += (a - accel_avg_[i]) / 8.0f;
            }
            accel_noise_ += (accel_dev - accel_noise_) / 32.0f;
            const float gravity = norm(accel_avg_);

            float gyro_dev = 0.0f;
            for (int i = 0; i < 3; ++i)
                gyro_dev += std::fabs(static_cast<float>(gyro[i]) - gyro_rest_[i]);

            if (rumbling)
                quiet_until_ms_ = now_ms + kRumbleQuietMs;
            const bool quiet = static_cast<int32_t>(quiet_until_ms_ - now_ms) > 0;
            const bool learning = now_ms - start_ms_ < kLearnMs;
            if (quiet || learning) {
                if (learning) {
                    gyro_noise_ += (gyro_dev - gyro_noise_) / 16.0f;
                    for (int i = 0; i < 3; ++i)
                        gyro_rest_[i] += (static_cast<float>(gyro[i]) - gyro_rest_[i]) / 16.0f;
                }
                rotating_ = false;
                set_tilt_reference(gravity);  // wherever the rumble or the start left it
                return false;
            }

            // The rest level follows slowly even while turning (not while rumbling), so a sensor
            // whose offset jumps to a new value stops counting as rotation after a few seconds.
            for (int i = 0; i < 3; ++i)
                gyro_rest_[i] += (static_cast<float>(gyro[i]) - gyro_rest_[i]) / 256.0f;

            bool tilted = false;
            if (tilt_ref_valid_ && gravity > 0.0f && accel_noise_ < kAccelNoisy * gravity) {
                const float ref = norm(tilt_ref_);
                const float dot = accel_avg_[0] * tilt_ref_[0] + accel_avg_[1] * tilt_ref_[1] + accel_avg_[2] * tilt_ref_[2];
                tilted = ref > 0.0f && dot / (gravity * ref) < kTiltCos;
            }

            const float limit = std::fmax(kGyroNoiseFactor * gyro_noise_, kGyroFloor);
            if (gyro_dev > limit) {
                if (!rotating_) {
                    rotating_ = true;
                    rotating_since_ms_ = now_ms;
                }
            } else {
                rotating_ = false;
                gyro_noise_ += (gyro_dev - gyro_noise_) / 32.0f;
            }
            const bool rotated = rotating_ && now_ms - rotating_since_ms_ >= kSustainMs;

            if (!tilt_ref_valid_ || tilted || rotated)
                set_tilt_reference(gravity);
            return tilted || rotated;
        }

    private:
        static float norm(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

        void set_tilt_reference(float gravity)
        {
            if (gravity <= 0.0f)
                return;
            for (int i = 0; i < 3; ++i)
                tilt_ref_[i] = accel_avg_[i];
            tilt_ref_valid_ = true;
        }

        bool started_ = false;
        uint32_t start_ms_ = 0;
        float gyro_rest_[3]{};
        float gyro_noise_ = 0.0f;
        float accel_avg_[3]{};
        float accel_noise_ = 0.0f;
        float tilt_ref_[3]{};
        bool tilt_ref_valid_ = false;
        bool rotating_ = false;
        uint32_t rotating_since_ms_ = 0;
        uint32_t quiet_until_ms_ = 0;
    };

} // namespace idle_motion

#endif // _OGXM_IDLE_MOTION_H_
