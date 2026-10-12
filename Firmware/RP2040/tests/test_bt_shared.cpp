// Pieces shared by every Bluepad32 board (Pico W / Pico 2 W and the ESP32 of the hybrid):
// the disconnect-combo hold (Bluepad32/ComboHold.h) and the user-profile mapper
// (Gamepad/GamepadMapper.h). One copy each, so these pin the behaviour for both.
#include <cstdint>

#include "Bluepad32/ComboHold.h"
#include "Gamepad/GamepadMapper.h"
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

TEST_MAIN()
