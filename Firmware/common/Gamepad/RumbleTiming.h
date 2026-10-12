#ifndef _OGXM_RUMBLE_TIMING_H_
#define _OGXM_RUMBLE_TIMING_H_

#include <cstdint>

/*  Rumble timing for Bluetooth pads, shared by the Pico W / Pico 2 W Bluetooth code
 *  (RP2040/src/Bluepad32) and the ESP32 companion of the hybrid (ESP32/main/BTManager).
 *
 *  Switch pads.
 *
 *  Switch pads keep vibrating with the last rumble data they received. The feedback loop
 *  re-sends "rumble for N ms" every kFeedbackPeriodMs while the host requests rumble, and the
 *  parser sends a single "stop" when N ms pass without a refresh. kRumbleDurationMs is longer
 *  than the feedback period, so a long rumble (cutscenes) is not stopped and restarted on every
 *  cycle (it used to be: duration == period, the stop always fired first, doubling the outgoing
 *  traffic).
 *
 *  That single "stop" used to be lost when Bluepad32's 32-slot output queue was full, leaving the
 *  motor on; the byte-stream output queue (patch bluepad32_output_ring_buffer.diff, upstream
 *  ricardoquesada/bluepad32 b6531db) holds about 270 rumble packets.
 */
namespace switch_rumble {

    constexpr uint32_t kFeedbackPeriodMs = 250;
    constexpr uint32_t kRumbleDurationMs = kFeedbackPeriodMs + 100;
    static_assert(kRumbleDurationMs > kFeedbackPeriodMs, "a long rumble must outlive one feedback period");

} // namespace switch_rumble

/*  One-second rumble when a pad becomes ready, so the user knows it is connected. Deferred a
 *  little, more for a DualShock 4: early output reports can drop its freshly opened link. For the
 *  same reason, rumble requested by the host is held back for kDs4HostRumbleGraceMs after a
 *  DualShock 4 connects. */
namespace connect_rumble {

    constexpr uint16_t kDurationMs = 1000;
    constexpr uint8_t kWeak = 160;
    constexpr uint8_t kStrong = 160;
    constexpr uint16_t kDelayDs4Ms = 1200;
    constexpr uint16_t kDelayDefaultMs = 300;
    constexpr uint32_t kDs4HostRumbleGraceMs = 6000;

} // namespace connect_rumble

#endif // _OGXM_RUMBLE_TIMING_H_
