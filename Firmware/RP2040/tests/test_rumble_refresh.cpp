// Idle neutral-rumble refresh for Switch pads (heals a lost "stop" without cutting rumble).
#include <vector>

#include "Custom/RumbleRefresh.h"
#include "test.h"

using switch_rumble::IdleRefresh;
using switch_rumble::kFeedbackPeriodMs;

namespace {

// Runs the feedback loop (one tick per period) and returns the times a refresh fired.
std::vector<uint32_t> run(IdleRefresh& r, uint32_t from_ms, uint32_t to_ms, bool rumble) {
    std::vector<uint32_t> fired;
    for (uint32_t t = from_ms; t < to_ms; t += kFeedbackPeriodMs)
        if (r.tick(t, rumble))
            fired.push_back(t);
    return fired;
}

}  // namespace

TEST(duration_outlives_the_feedback_period) {
    // Otherwise the parser's stop fires before every re-send and long rumble stutters.
    CHECK(switch_rumble::kRumbleDurationMs > kFeedbackPeriodMs);
    CHECK(switch_rumble::kStopRecheckMs > switch_rumble::kRumbleDurationMs);
}

TEST(long_rumble_is_never_interrupted) {
    IdleRefresh r;
    run(r, 1000, 5000, false);  // idle before
    const auto fired = run(r, 5000, 8000, true);  // 3 s cutscene rumble
    CHECK(fired.empty());
    const auto fired_long = run(r, 8000, 68000, true);  // and a whole minute of it
    CHECK(fired_long.empty());
}

TEST(refresh_after_rumble_ends_then_every_second) {
    IdleRefresh r;
    run(r, 1000, 3000, false);
    run(r, 3000, 5000, true);  // last rumble tick at 4750
    const auto fired = run(r, 5000, 8000, false);
    CHECK(!fired.empty());
    // First refresh only after the parser's own stop (last tick + recheck delay).
    CHECK(fired[0] >= 4750 + switch_rumble::kStopRecheckMs);
    CHECK(fired[0] < 4750 + switch_rumble::kStopRecheckMs + kFeedbackPeriodMs);
    for (size_t i = 1; i < fired.size(); ++i)
        CHECK(fired[i] - fired[i - 1] >= switch_rumble::kIdleRefreshMs);
}

TEST(idle_pad_is_refreshed_every_second) {
    IdleRefresh r;
    const auto fired = run(r, 1000, 11000, false);
    CHECK(fired.size() >= 9 && fired.size() <= 11);
}

TEST(short_pulses_do_not_trigger_refresh_mid_rumble) {
    IdleRefresh r;
    run(r, 1000, 3000, false);
    // Rumble on/off every other tick: a refresh may only come after each pulse has stopped.
    uint32_t last_on = 0;
    for (uint32_t t = 3000; t < 9000; t += kFeedbackPeriodMs) {
        const bool on = ((t / kFeedbackPeriodMs) % 4) < 2;
        if (on) last_on = t;
        if (r.tick(t, on))
            CHECK(!on && t - last_on >= switch_rumble::kStopRecheckMs);
    }
}

TEST_MAIN()
