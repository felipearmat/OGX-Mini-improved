#include "Board/StatusLed.h"
#include "Board/ModeIndicator.h"

namespace status_led {

bool Blinker::step(Search search, bool critical, bool& on, uint32_t& next_ms, bool& alert)
{
    alert = false;
    if (mode_indicator::next_step(on, next_ms))
    {
        shown_ = -1;
        return true;
    }

    if (!critical) {
        alert_left_ = 0;
        since_alert_ms_ = kAlertEveryMs;  // the next critical battery alerts at once
    } else if (alert_left_ == 0 && since_alert_ms_ >= kAlertEveryMs) {
        alert_left_ = 2 * kAlertBlinks;
        since_alert_ms_ = 0;
    }
    if (alert_left_ > 0) {
        on = (alert_left_ % 2) == 0;  // starts with "on"
        --alert_left_;
        next_ms = on ? kAlertOnMs : kAlertOffMs;
        alert = true;
        shown_ = -1;  // the usual state is written again after the blinks
        return true;
    }

    phase_ = !phase_;
    on = (search == Search::NONE) ? true : phase_;
    next_ms = (search == Search::REDUCED) ? kSlowBlinkMs : kFastBlinkMs;
    since_alert_ms_ += next_ms;

    const int state = on ? 1 : 0;
    if (state == shown_)
    {
        return false;
    }
    shown_ = state;
    return true;
}

} // namespace status_led
