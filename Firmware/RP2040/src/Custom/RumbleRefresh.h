#ifndef _OGXM_CUSTOM_RUMBLE_REFRESH_H_
#define _OGXM_CUSTOM_RUMBLE_REFRESH_H_

#include <cstdint>

/*  Rumble timing for Switch pads (custom addition, not upstream).
 *
 *  Switch pads keep vibrating with the last rumble data they received. The feedback loop
 *  re-sends "rumble for N ms" every kFeedbackPeriodMs while the host requests rumble, and the
 *  parser sends a single "stop" when N ms pass without a refresh.
 *
 *  - kRumbleDurationMs is longer than the feedback period, so a long rumble (cutscenes) is not
 *    stopped and restarted on every cycle (it used to be: duration == period, the stop always
 *    fired first, doubling the outgoing traffic).
 *  - IdleRefresh decides when to re-send a neutral packet while no rumble is requested:
 *    once shortly after the rumble's own stop, then every second. A dropped or ignored "stop"
 *    therefore heals within a second instead of leaving the motor on.
 */
namespace switch_rumble {

    constexpr uint32_t kFeedbackPeriodMs = 250;
    constexpr uint32_t kRumbleDurationMs = kFeedbackPeriodMs + 100;
    constexpr uint32_t kIdleRefreshMs = 1000;
    constexpr uint32_t kStopRecheckMs = kRumbleDurationMs + 150;  // after the parser's own stop

    class IdleRefresh {
    public:
        // Call on every feedback tick. True when a neutral rumble packet should be sent now.
        bool tick(uint32_t now_ms, bool rumble_requested);

    private:
        uint32_t active_ms_ = 0;    // last tick with rumble requested (0 = never)
        uint32_t refresh_ms_ = 0;   // last neutral refresh
        bool stop_rechecked_ = true;
    };

} // namespace switch_rumble

#endif // _OGXM_CUSTOM_RUMBLE_REFRESH_H_
