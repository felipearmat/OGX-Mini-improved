#ifndef _OGXM_COMBO_CHECK_H_
#define _OGXM_COMBO_CHECK_H_

#include <cstdint>

#include "Gamepad/GamepadBits.h"
#include "UserSettings/DongleSettings.h"
#include "UserSettings/ModeCombos.h"

/* Button combos held about 3 s: output-mode changes (UserSettings/ModeCombos.h) and Start + L3,
 * which stops the search for new controllers. Shared by the RP2040 firmware and the ESP32 of the
 * hybrid, which checks the combos itself. Fed every kCheckDelayMs with the pad's mapped input. */
namespace mode_combos {

    constexpr uint32_t kCheckDelayMs = 600;
    constexpr uint8_t kCheckCount = 3000 / kCheckDelayMs;

    /* Start + L3: no mode combo uses L3, so it never takes over one. */
    constexpr uint32_t STOP_SEARCH_COMBO = combo(GamepadBits::BUTTON_START | GamepadBits::BUTTON_L3);

    /* D-pad direction of a stick held near the edge along one axis (Y negative = up), for a lone
     * Joy-Con, which has no D-pad. Diagonals and partial pushes give none, so a combo cannot fire
     * by accident. */
    inline uint8_t stick_edge_as_dpad(int16_t x, int16_t y)
    {
        constexpr int32_t EDGE = 26000;  // ~80% of full deflection
        const int32_t ax = x < 0 ? -static_cast<int32_t>(x) : x;
        const int32_t ay = y < 0 ? -static_cast<int32_t>(y) : y;
        if (ay >= EDGE && ax < EDGE / 2)
            return y < 0 ? GamepadBits::DPAD_UP : GamepadBits::DPAD_DOWN;
        if (ax >= EDGE && ay < EDGE / 2)
            return x < 0 ? GamepadBits::DPAD_LEFT : GamepadBits::DPAD_RIGHT;
        return 0;
    }

    /* Required buttons / D-pad pressed (extra face buttons or triggers do not block the combo). */
    inline bool combo_matches(uint32_t current, uint32_t expected)
    {
        const uint16_t req_buttons = static_cast<uint16_t>(expected >> 16);
        const uint16_t cur_buttons = static_cast<uint16_t>(current >> 16);
        const uint8_t req_dpad = static_cast<uint8_t>(expected & 0xFF);
        const uint8_t cur_dpad = static_cast<uint8_t>(current & 0xFF);
        return ((cur_buttons & req_buttons) == req_buttons) && (cur_dpad == req_dpad);
    }

    /* The matching combo that requires the most buttons (e.g. STEAM over XInput when LB is held). */
    inline uint32_t find_matching_combo(uint32_t current)
    {
        uint32_t best = 0;
        uint8_t best_button_count = 0;
        for (const auto& entry : MODE_COMBOS) {
            if (!combo_matches(current, entry.combo))
                continue;
            const uint16_t req = static_cast<uint16_t>(entry.combo >> 16);
            uint8_t count = 0;
            for (uint16_t bit = req; bit != 0; bit &= static_cast<uint16_t>(bit - 1))
                ++count;
            if (count >= best_button_count) {
                best_button_count = count;
                best = entry.combo;
            }
        }
        return best;
    }

    class ComboCheck
    {
    public:
        struct Result
        {
            bool stop_search{false};                        // Start + L3 held: once per hold
            DeviceDriverType new_driver{DeviceDriverType::NONE};  // a mode combo held
        };

        /* stick_as_dpad: a lone Joy-Con (its stick at the edge stands in for the D-pad).
         * stop_search_allowed: false where Bluetooth is not the controller input (Wii mode).
         * is_valid: whether this firmware offers the mode. */
        Result update(uint16_t buttons, uint8_t dpad, bool stick_as_dpad, int16_t lx, int16_t ly,
                      DeviceDriverType current, bool stop_search_allowed,
                      bool (*is_valid)(DeviceDriverType), const dongle_settings::Settings& settings)
        {
            Result result;
            uint8_t combo_dpad = dpad;
            if (combo_dpad == 0 && stick_as_dpad)
                combo_dpad = stick_edge_as_dpad(lx, ly);
            const uint32_t current_combo = combo(buttons, combo_dpad);

            if (stop_search_allowed && combo_matches(current_combo, STOP_SEARCH_COMBO)) {
                last_combo_ = 0;
                count_ = 0;
                if (++stop_count_ == kCheckCount)
                    result.stop_search = true;  // once per hold
                return result;
            }
            stop_count_ = 0;

            const uint32_t active = find_matching_combo(current_combo);
            if (!(current_combo & (static_cast<uint32_t>(GamepadBits::BUTTON_START) << 16)) ||
                active == 0 || last_combo_ != active) {
                last_combo_ = active;
                count_ = 0;
                return result;
            }
            if (++count_ < kCheckCount)
                return result;
            count_ = 0;

            DeviceDriverType driver = DeviceDriverType::NONE;
            for (const auto& entry : MODE_COMBOS) {
                if (entry.combo == active && is_valid(entry.driver)) {
                    driver = entry.driver;
                    break;
                }
            }
            /* Modes whose combo is turned off in the web app (Web App mode always stays on). */
            if (driver != DeviceDriverType::NONE && driver != DeviceDriverType::WEBAPP &&
                !dongle_settings::mode_combo_enabled(settings, static_cast<uint8_t>(driver)))
                return result;
            if (driver != current)
                result.new_driver = driver;
            return result;
        }

    private:
        uint32_t last_combo_{0};
        uint8_t count_{0};
        uint8_t stop_count_{0};
    };

} // namespace mode_combos

#endif // _OGXM_COMBO_CHECK_H_
