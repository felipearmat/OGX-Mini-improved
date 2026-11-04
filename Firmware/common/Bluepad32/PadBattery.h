#ifndef _OGXM_PAD_BATTERY_H_
#define _OGXM_PAD_BATTERY_H_

#include <cstdint>

#include "Diagnostics/Diagnostics.h"

/* Custom: each Bluetooth controller's battery as its own reports give it, charging included,
 * which Bluepad32 leaves out: the Switch pads' status byte (byte 2 of reports 0x30 / 0x21: bits
 * 5-7 the level 0-4, bit 4 charging, as dekuNukem's notes and Linux hid-nintendo read it) and the
 * DS4 / DualSense status byte (Gamepad/SonyBattery.h). Filled from the raw reports
 * (Bluepad32/LinkDiag.cpp); read for the host (Bluepad32/BtPadIn.h): a merged Joy-Con pair gives
 * the lower of its two halves, so the host sees the one that runs out first. Header only: shared
 * by the Pico W firmware and the hybrid's ESP32. */
namespace pad_battery {

    struct State {
        bool known;
        uint8_t percent;  // 0-100
        diag::BatteryState state;
    };

    inline State* table(int idx)
    {
        static State s_states[8]{};
        return idx >= 0 && idx < 8 ? &s_states[idx] : nullptr;
    }

    inline void set(int idx, uint8_t percent, diag::BatteryState state)
    {
        if (State* s = table(idx)) *s = State{true, percent > 100 ? uint8_t{100} : percent, state};
    }

    inline void clear(int idx)
    {
        if (State* s = table(idx)) *s = State{};
    }

    /* A Switch pad's status byte: level 0 empty, 1 critical, 2 low, 3 medium, 4 full. */
    struct SwitchReading {
        uint8_t percent;
        diag::BatteryState state;
    };
    constexpr SwitchReading from_switch(uint8_t bat_con)
    {
        constexpr uint8_t kPercent[5] = {0, 10, 35, 65, 100};
        const uint8_t level = static_cast<uint8_t>(bat_con >> 5);
        const bool charging = (bat_con & 0x10) != 0;
        if (level > 4) return {0, diag::BatteryState::UNKNOWN};
        return {kPercent[level],
                charging ? (level == 4 ? diag::BatteryState::FULL : diag::BatteryState::CHARGING)
                         : diag::BatteryState::DISCHARGING};
    }

    constexpr bool charging(diag::BatteryState s)
    {
        return s == diag::BatteryState::CHARGING || s == diag::BatteryState::FULL;
    }

    /* Low enough to warn: at most 10 % (a Switch pad's "critical") and not on a charger. */
    constexpr bool critical(const State& s)
    {
        return s.known && s.percent <= 10 && !charging(s.state);
    }

    /* For the host: the pad's own state, or the lower one of a pair (its partner's index, -1 none).
     * Not known: nothing from the reports yet (other pads: Bluepad32's level only). */
    inline State for_pad(int idx, int partner_idx)
    {
        const State* own = table(idx);
        const State* other = table(partner_idx);
        State s = own ? *own : State{};
        if (other && other->known && (!s.known || other->percent < s.percent))
            s = *other;
        return s;
    }

    /* Bluepad32's battery scale (1-255, 0 = unknown) from a percent. */
    constexpr uint8_t to_255(uint8_t percent)
    {
        return static_cast<uint8_t>(percent == 0 ? 1 : (percent * 255u + 50u) / 100u);
    }

} // namespace pad_battery

#endif // _OGXM_PAD_BATTERY_H_
