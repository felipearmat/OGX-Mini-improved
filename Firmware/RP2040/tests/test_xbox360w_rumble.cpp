// Xbox 360 wireless receiver (Xbox360WHost::send_feedback), on a stand-in for the TinyUSB XInput
// class. Regression covered (upstream #108, fixed upstream by #158): the driver returned early
// when both motors were 0, so the receiver never got the stop once a game started rumble, and the
// pad kept vibrating until it was power-cycled. Also: an OUT transfer that could not be queued is
// sent again on the next feedback tick, and unchanged values are not resent.
#include <vector>

#include "USBHost/HostDriver/XInput/Xbox360W.h"
#include "USBHost/HostDriver/XInput/tuh_xinput/tuh_xinput.h"
#include "test.h"

uint64_t mock_time_us = 0;  // pico/time.h (tests/mocks)

namespace {

struct Sent {
    uint8_t instance, l, r;
};

std::vector<Sent> sent;
bool connected = true;
bool queue_ok = true;  // false: the OUT endpoint is busy, the transfer is not queued

Gamepad::PadOut rumble(uint8_t l, uint8_t r) {
    Gamepad::PadOut out;
    out.rumble_l = l;
    out.rumble_r = r;
    return out;
}

void reset() {
    sent.clear();
    connected = true;
    queue_ok = true;
}

}  // namespace

namespace tuh_xinput {
bool is_connected(uint8_t, uint8_t) { return connected; }
bool set_rumble(uint8_t, uint8_t instance, uint8_t l, uint8_t r, bool) {
    if (!queue_ok) {
        return false;
    }
    sent.push_back({instance, l, r});
    return true;
}
bool receive_report(uint8_t, uint8_t) { return true; }
void xbox360_chatpad_init(uint8_t, uint8_t) {}
}  // namespace tuh_xinput

TEST(the_stop_reaches_the_pad_after_rumble) {
    reset();
    Xbox360WHost host(0);
    Gamepad gp;
    host.connect_cb(gp, 1, 2);
    gp.set_pad_out(rumble(200, 100));
    host.send_feedback(gp, 1, 2);
    gp.set_pad_out(rumble(0, 0));  // the game stops the motors
    host.send_feedback(gp, 1, 2);
    CHECK_EQ(sent.size(), 2u);
    CHECK_EQ(sent.back().l, 0);
    CHECK_EQ(sent.back().r, 0);
    CHECK_EQ(sent.back().instance, 2);
}

TEST(unchanged_values_are_not_sent_again) {
    reset();
    Xbox360WHost host(0);
    Gamepad gp;
    host.connect_cb(gp, 1, 0);
    gp.set_pad_out(rumble(50, 50));
    for (int tick = 0; tick < 5; ++tick) {
        host.send_feedback(gp, 1, 0);  // called on every feedback tick
    }
    CHECK_EQ(sent.size(), 1u);
}

TEST(a_stop_that_could_not_be_queued_is_sent_on_the_next_tick) {
    reset();
    Xbox360WHost host(0);
    Gamepad gp;
    host.connect_cb(gp, 1, 0);
    gp.set_pad_out(rumble(255, 255));
    host.send_feedback(gp, 1, 0);
    gp.set_pad_out(rumble(0, 0));
    queue_ok = false;  // the endpoint is still busy with the previous report
    CHECK(!host.send_feedback(gp, 1, 0));
    queue_ok = true;
    host.send_feedback(gp, 1, 0);  // next tick, the console sending nothing new
    CHECK_EQ(sent.size(), 2u);
    CHECK_EQ(sent.back().l, 0);
    CHECK_EQ(sent.back().r, 0);
}

TEST(a_pad_connecting_again_gets_the_current_state) {
    reset();
    Xbox360WHost host(0);
    Gamepad gp;
    host.connect_cb(gp, 1, 0);
    gp.set_pad_out(rumble(0, 0));
    host.send_feedback(gp, 1, 0);
    host.disconnect_cb(gp, 1, 0);
    host.connect_cb(gp, 1, 0);  // the pad is a new one to the receiver: tell it the motors are off
    host.send_feedback(gp, 1, 0);
    CHECK_EQ(sent.size(), 2u);
}

TEST_MAIN()
