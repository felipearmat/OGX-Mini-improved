#ifndef _OGXM_COMBO_HOLD_H_
#define _OGXM_COMBO_HOLD_H_

#include <cstdint>

/* A button combo held for a time, measured in ms (report rates differ per pad). Fed on every
 * report of the pad; fires once per hold. */
class ComboHold
{
public:
    /* Starts a grace window: the combo is ignored until grace_until_ms (e.g. right after the pad
     * connects, while buttons pressed during pairing may still be down). */
    void reset(uint32_t grace_until_ms = 0)
    {
        holding_ = false;
        grace_until_ms_ = grace_until_ms;
    }

    /* True once the combo has been held for hold_ms; the hold then starts over. */
    bool update(bool pressed, uint32_t now_ms, uint32_t hold_ms)
    {
        if (!pressed || static_cast<int32_t>(now_ms - grace_until_ms_) < 0)
        {
            holding_ = false;
            return false;
        }
        if (!holding_)
        {
            holding_ = true;
            since_ms_ = now_ms;
        }
        if (now_ms - since_ms_ < hold_ms)
        {
            return false;
        }
        holding_ = false;
        return true;
    }

private:
    bool holding_{false};
    uint32_t since_ms_{0};
    uint32_t grace_until_ms_{0};
};

#endif // _OGXM_COMBO_HOLD_H_
