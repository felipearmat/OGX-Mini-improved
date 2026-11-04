#ifndef _OGXM_INPUT_WATCH_H_
#define _OGXM_INPUT_WATCH_H_

#include <cstdint>

/* Custom: a Switch pad that finished its setup but sends no input. 2026-11-04, a Joy-Con (R) on a
 * flat battery: twice it answered every setup subcommand, then sent 3-4 reports a second instead
 * of about 66 and its link dropped 21 s later. Watched from "ready": fewer than kMinReports in
 * kGraceMs asks it again for the standard report mode; still quiet kGraceMs later, the link is
 * dropped so it connects again (Bluepad32.cpp). Header only, shared by the Pico W and the hybrid's
 * ESP32. */
namespace input_watch {

    constexpr uint32_t kGraceMs = 3000;
    constexpr uint32_t kMinReports = 60;  // a Joy-Con sends about 200 in that time

    enum class Action : uint8_t { NONE, ASK_AGAIN, DROP };

    struct Slot {
        bool watching;
        uint8_t step;     // 0 waiting, 1 asked again
        uint32_t since_ms;
        uint32_t reports;
    };

    inline Slot* table(int idx)
    {
        static Slot s_slots[8]{};
        return idx >= 0 && idx < 8 ? &s_slots[idx] : nullptr;
    }

    inline void on_ready(int idx, uint32_t now_ms)
    {
        if (Slot* s = table(idx)) *s = Slot{true, 0, now_ms, 0};
    }

    inline void on_report(int idx)
    {
        if (Slot* s = table(idx)) ++s->reports;
    }

    inline void clear(int idx)
    {
        if (Slot* s = table(idx)) *s = Slot{};
    }

    /* What to do now for a watched pad; it stops watching once the pad reports enough. */
    inline Action check(int idx, uint32_t now_ms, uint32_t* reports = nullptr)
    {
        Slot* s = table(idx);
        if (!s || !s->watching || now_ms - s->since_ms < kGraceMs)
            return Action::NONE;
        if (reports) *reports = s->reports;
        if (s->reports >= kMinReports) {
            s->watching = false;  // streaming: done
            return Action::NONE;
        }
        if (s->step == 0) {
            s->step = 1;
            s->since_ms = now_ms;
            s->reports = 0;
            return Action::ASK_AGAIN;
        }
        s->watching = false;
        return Action::DROP;
    }

} // namespace input_watch

#endif // _OGXM_INPUT_WATCH_H_
