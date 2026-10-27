// Host -> pad output path of Gamepad (rumble peak latch, host lightbar), and when it reports new
// input. Regression covered (input): with no pad, after boot or a disconnect (reset_pad_in), the
// "new input" flag stayed set, so every output driver resent the same neutral report on every loop.
// Regression covered: the Bluetooth feedback loop samples rumble every 250 ms, so a host pulse
// that started and ended between two samples (Steam's ~200 ms trigger test) never reached the
// pad. take_pad_out_peak() returns the strongest rumble since the previous call.
#include "Gamepad/Gamepad.h"
#include "test.h"

uint64_t mock_time_us = 0;  // pico/time.h (tests/mocks)

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

TEST(no_pad_reports_its_neutral_state_once) {
    Gamepad gp;  // the constructor resets the input, as a disconnect does
    CHECK(gp.new_pad_in());
    (void)gp.get_pad_in();
    CHECK(!gp.new_pad_in());  // nothing new until a pad reports
    (void)gp.get_pad_in();
    CHECK(!gp.new_pad_in());
}

TEST(a_disconnect_reports_the_neutral_state_once) {
    Gamepad gp;
    (void)gp.get_pad_in();
    Gamepad::PadIn in;
    in.buttons = 1;
    gp.set_pad_in(in);
    CHECK(gp.new_pad_in());
    CHECK_EQ(gp.get_pad_in().buttons, 1);
    CHECK(!gp.new_pad_in());
    gp.reset_pad_in();  // the pad went away
    CHECK(gp.new_pad_in());
    CHECK_EQ(gp.get_pad_in().buttons, 0);
    CHECK(!gp.new_pad_in());
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
TEST(hd_rumble_reports_reach_the_bt_side_once_and_in_order) {
    // HD Rumble blocks can be relative (step up / down): every host report once, in order.
    Gamepad gp;
    for (uint8_t i = 0; i < 5; ++i) {
        uint8_t blocks[8] = {i, 1, 2, 3, i, 5, 6, 7};
        gp.push_hd_rumble(blocks);
    }
    hd_rumble::Frame f;
    for (uint8_t i = 0; i < 5; ++i) {
        CHECK(gp.pop_hd_rumble(f));
        CHECK_EQ(f.blocks[0], i);
    }
    CHECK(!gp.pop_hd_rumble(f));
    // A stalled BT side: the oldest reports go, the latest stay.
    for (uint8_t i = 0; i < Gamepad::HD_RUMBLE_QUEUE_SIZE + 3; ++i) {
        uint8_t blocks[8] = {i};
        gp.push_hd_rumble(blocks);
    }
    CHECK(gp.pop_hd_rumble(f));
    CHECK_EQ(f.blocks[0], 3);
}

TEST(hd_rumble_feed_stops_a_host_that_went_quiet) {
    hd_rumble::Feed feed;
    CHECK(!feed.owns_rumble(0));
    hd_rumble::Frame on{{0x28, 0x88, 0x60, 0x61, 0x28, 0x88, 0x60, 0x61}};
    feed.on_frame(on, 1000);
    CHECK(feed.playing());
    CHECK(feed.owns_rumble(1500));
    CHECK(!feed.stop_due(1000 + hd_rumble::kStopAfterMs - 1));
    CHECK(feed.stop_due(1000 + hd_rumble::kStopAfterMs));  // one neutral report
    CHECK(!feed.stop_due(2000));                          // and only one
    CHECK(!feed.owns_rumble(1000 + hd_rumble::kOwnMs));   // intensity rumble back for the pad
    // The host's own neutral: nothing more to send.
    feed.on_frame(on, 3000);
    feed.on_frame(hd_rumble::neutral_frame(), 3015);
    CHECK(!feed.playing());
    CHECK(!feed.stop_due(4000));
    CHECK(hd_rumble::is_neutral(hd_rumble::neutral_frame().blocks));
}

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
