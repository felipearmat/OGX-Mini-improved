// Pieces shared by every Bluepad32 board (Pico W / Pico 2 W and the ESP32 of the hybrid):
// the disconnect-combo hold (Bluepad32/ComboHold.h) and the user-profile mapper
// (Gamepad/GamepadMapper.h). One copy each, so these pin the behaviour for both.
#include <cstdint>

#include "Bluepad32/ComboHold.h"
#include "Gamepad/GamepadMapper.h"
#include "UserSettings/ComboCheck.h"
#include "UserSettings/UserProfile.h"
#include "test.h"

TEST(combo_fires_after_the_hold_time) {
    ComboHold hold;
    CHECK(!hold.update(true, 1000, 3000));
    CHECK(!hold.update(true, 3999, 3000));
    CHECK(hold.update(true, 4000, 3000));
    CHECK(!hold.update(true, 4001, 3000));  // fires once, then the hold starts over
}

TEST(combo_release_restarts_the_hold) {
    ComboHold hold;
    CHECK(!hold.update(true, 1000, 3000));
    CHECK(!hold.update(false, 2000, 3000));
    CHECK(!hold.update(true, 3500, 3000));
    CHECK(!hold.update(true, 6000, 3000));
    CHECK(hold.update(true, 6500, 3000));
}

TEST(combo_ignored_during_grace) {
    ComboHold hold;
    hold.reset(5000);  // pad ready at 1500 with a 3500 ms grace
    CHECK(!hold.update(true, 1500, 3000));
    CHECK(!hold.update(true, 4999, 3000));
    CHECK(!hold.update(true, 5000, 3000));  // the hold starts on the first report after the grace
    CHECK(!hold.update(true, 7999, 3000));
    CHECK(hold.update(true, 8000, 3000));
}

TEST(combo_held_from_time_zero) {
    ComboHold hold;
    CHECK(!hold.update(true, 0, 3000));
    CHECK(hold.update(true, 3000, 3000));
}

static UserProfile custom_profile()
{
    UserProfile profile;
    profile.joystick_settings_l.dz_inner = fix16_from_float(0.2f);
    profile.trigger_settings_l.dz_inner = fix16_from_float(0.2f);
    profile.button_a = GamepadBits::BUTTON_B;
    profile.button_b = GamepadBits::BUTTON_A;
    return profile;
}

TEST(profile_applied_twice_stays_applied) {
    // Applying the same profile again (boot, then a mode change) must not turn its stick and
    // trigger settings off: a small stick deflection stays inside the 20% deadzone.
    GamepadMapper mapper;
    const UserProfile profile = custom_profile();
    mapper.set_profile(profile, DeviceDriverType::DINPUT);
    mapper.set_profile(profile, DeviceDriverType::DINPUT);
    const auto stick = mapper.scale_joystick_l<10>(static_cast<int32_t>(50), static_cast<int32_t>(0));
    CHECK_EQ(stick.first, 0);
    CHECK_EQ(mapper.scale_trigger_l<10>(static_cast<uint16_t>(100)), 0);
    CHECK_EQ(mapper.MAP_BUTTON_A, GamepadBits::BUTTON_B);
}

TEST(web_app_mode_uses_the_default_mapping) {
    // The web app shows the controller's own buttons; stick / trigger settings still apply.
    GamepadMapper mapper;
    mapper.set_profile(custom_profile(), DeviceDriverType::WEBAPP);
    CHECK_EQ(mapper.MAP_BUTTON_A, GamepadBits::BUTTON_A);
    CHECK_EQ(mapper.scale_trigger_l<10>(static_cast<uint16_t>(100)), 0);
}

static bool all_modes_valid(DeviceDriverType) { return true; }

static mode_combos::ComboCheck::Result hold(mode_combos::ComboCheck& check, uint16_t buttons, uint8_t dpad,
                                            bool stick_as_dpad = false, int16_t lx = 0, int16_t ly = 0,
                                            const dongle_settings::Settings& settings = dongle_settings::defaults())
{
    mode_combos::ComboCheck::Result last;
    for (uint8_t i = 0; i <= mode_combos::kCheckCount; ++i) {
        const auto r = check.update(buttons, dpad, stick_as_dpad, lx, ly, DeviceDriverType::XBOXOG, true,
                                    all_modes_valid, settings);
        if (r.new_driver != DeviceDriverType::NONE || r.stop_search)
            return r;
        last = r;
    }
    return last;
}

TEST(mode_combo_fires_after_three_seconds) {
    mode_combos::ComboCheck check;
    const auto r = hold(check, GamepadBits::BUTTON_START, GamepadBits::DPAD_DOWN);
    CHECK_EQ(static_cast<int>(r.new_driver), static_cast<int>(DeviceDriverType::SWITCH));
}

TEST(extra_buttons_do_not_block_and_the_longest_combo_wins) {
    mode_combos::ComboCheck check;  // Start + LB + Up is STEAM, not XInput (Start + Up)
    const auto r = hold(check, GamepadBits::BUTTON_START | GamepadBits::BUTTON_LB | GamepadBits::BUTTON_A,
                        GamepadBits::DPAD_UP);
    CHECK_EQ(static_cast<int>(r.new_driver), static_cast<int>(DeviceDriverType::STEAM));
}

TEST(lone_joycon_stick_stands_in_for_the_dpad) {
    mode_combos::ComboCheck check;
    CHECK_EQ(static_cast<int>(hold(check, GamepadBits::BUTTON_START, 0, false, 0, 30000).new_driver),
             static_cast<int>(DeviceDriverType::NONE));
    mode_combos::ComboCheck joycon;
    CHECK_EQ(static_cast<int>(hold(joycon, GamepadBits::BUTTON_START, 0, true, 0, 30000).new_driver),
             static_cast<int>(DeviceDriverType::SWITCH));  // stick down = D-pad down
}

TEST(start_l3_stops_the_search) {
    mode_combos::ComboCheck check;
    CHECK(hold(check, GamepadBits::BUTTON_START | GamepadBits::BUTTON_L3, 0).stop_search);
}

TEST(disabled_mode_combo_does_nothing) {
    dongle_settings::Settings settings = dongle_settings::defaults();
    settings.combo_disabled_modes = 1u << static_cast<uint8_t>(DeviceDriverType::SWITCH);
    mode_combos::ComboCheck check;
    CHECK_EQ(static_cast<int>(hold(check, GamepadBits::BUTTON_START, GamepadBits::DPAD_DOWN, false, 0, 0, settings).new_driver),
             static_cast<int>(DeviceDriverType::NONE));
}

TEST_MAIN()
