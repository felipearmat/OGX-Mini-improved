// Host -> pad output path of Gamepad (rumble peak latch, host lightbar).
// Regression covered: the Bluetooth feedback loop samples rumble every 250 ms, so a host pulse
// that started and ended between two samples (Steam's ~200 ms trigger test) never reached the
// pad. take_pad_out_peak() returns the strongest rumble since the previous call.
#include "Gamepad/Gamepad.h"
#include "test.h"

namespace {

Gamepad::PadOut rumble(uint8_t l, uint8_t r) {
    Gamepad::PadOut out;
    out.rumble_l = l;
    out.rumble_r = r;
    return out;
}

}  // namespace

TEST(short_pulse_between_samples_is_not_lost) {
    Gamepad gp;
    gp.set_pad_out(rumble(127, 127));  // pulse starts...
    gp.set_pad_out(rumble(0, 0));      // ...and ends before the feedback loop looks
    const Gamepad::PadOut seen = gp.take_pad_out_peak();
    CHECK_EQ(seen.rumble_l, 127);
    CHECK_EQ(seen.rumble_r, 127);
    // Next sample: the pulse is over.
    const Gamepad::PadOut next = gp.take_pad_out_peak();
    CHECK_EQ(next.rumble_l, 0);
    CHECK_EQ(next.rumble_r, 0);
}

TEST(peak_is_per_motor) {
    Gamepad gp;
    gp.set_pad_out(rumble(200, 0));
    gp.set_pad_out(rumble(10, 90));
    const Gamepad::PadOut seen = gp.take_pad_out_peak();
    CHECK_EQ(seen.rumble_l, 200);
    CHECK_EQ(seen.rumble_r, 90);
}

TEST(sustained_rumble_keeps_reading_current_value) {
    Gamepad gp;
    gp.set_pad_out(rumble(80, 80));
    CHECK_EQ(gp.take_pad_out_peak().rumble_l, 80);
    CHECK_EQ(gp.take_pad_out_peak().rumble_l, 80);   // still on: current value, not zero
}

TEST(get_pad_out_is_unchanged) {
    Gamepad gp;
    gp.set_pad_out(rumble(127, 0));
    gp.set_pad_out(rumble(0, 0));
    CHECK_EQ(gp.get_pad_out().rumble_l, 0);           // other consumers see the current value
}

TEST(host_lightbar) {
    Gamepad gp;
    CHECK(!gp.get_host_lightbar().valid);
    gp.set_host_lightbar(1, 2, 3);
    const Gamepad::Lightbar l = gp.get_host_lightbar();
    CHECK(l.valid);
    CHECK_EQ(l.r, 1);
    CHECK_EQ(l.g, 2);
    CHECK_EQ(l.b, 3);
    gp.reset_pad_out();
    CHECK(!gp.get_host_lightbar().valid);
}


// Web App mode shows the controller's own buttons: the stored profile's mapping is not applied
// (the web app lights up each mapping row for its physical input).
TEST(web_app_mode_ignores_the_profile_mapping) {
    UserProfile swapped;
    swapped.button_a = Gamepad::BUTTON_B;
    swapped.button_b = Gamepad::BUTTON_A;
    swapped.dpad_up = Gamepad::DPAD_DOWN;
    Gamepad gp;
    gp.set_profile(swapped, DeviceDriverType::XINPUT);
    CHECK_EQ(gp.MAP_BUTTON_A, Gamepad::BUTTON_B);
    CHECK_EQ(gp.MAP_DPAD_UP, Gamepad::DPAD_DOWN);
    gp.set_profile(swapped, DeviceDriverType::WEBAPP);
    CHECK_EQ(gp.MAP_BUTTON_A, Gamepad::BUTTON_A);
    CHECK_EQ(gp.MAP_BUTTON_B, Gamepad::BUTTON_B);
    CHECK_EQ(gp.MAP_DPAD_UP, Gamepad::DPAD_UP);
}

TEST_MAIN()
