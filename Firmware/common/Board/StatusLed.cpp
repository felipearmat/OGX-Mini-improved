#include "Board/StatusLed.h"
#include "Board/ModeIndicator.h"

namespace status_led {

bool Blinker::step(Search search, bool& on, uint32_t& next_ms)
{
    if (mode_indicator::next_step(on, next_ms))
    {
        shown_ = -1;
        return true;
    }

    phase_ = !phase_;
    on = (search == Search::NONE) ? true : phase_;
    next_ms = (search == Search::REDUCED) ? kSlowBlinkMs : kFastBlinkMs;

    const int state = on ? 1 : 0;
    if (state == shown_)
    {
        return false;
    }
    shown_ = state;
    return true;
}

} // namespace status_led
