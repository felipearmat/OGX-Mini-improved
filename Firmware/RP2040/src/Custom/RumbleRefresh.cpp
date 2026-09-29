#include "Custom/RumbleRefresh.h"

namespace switch_rumble {

bool IdleRefresh::tick(uint32_t now_ms, bool rumble_requested)
{
    if (rumble_requested)
    {
        active_ms_ = now_ms;
        stop_rechecked_ = false;
        return false;
    }
    const bool recheck_due = !stop_rechecked_ && (now_ms - active_ms_) >= kStopRecheckMs;
    const bool periodic_due = (now_ms - refresh_ms_) >= kIdleRefreshMs;
    if (!recheck_due && !periodic_due)
        return false;
    // Hold the periodic refresh until the rumble's own stop has had time to go out.
    if (!stop_rechecked_ && !recheck_due)
        return false;
    stop_rechecked_ = true;
    refresh_ms_ = now_ms;
    return true;
}

} // namespace switch_rumble
