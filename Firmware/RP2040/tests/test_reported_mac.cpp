// MAC address reported by the emulated DS4 / DualSense (Custom/ReportedMac, dongle option
// "MAC address per controller").
#include <cstring>

#include "Custom/DongleSettings.h"
#include "Custom/ReportedMac.h"
#include "test.h"

namespace {

// bd_addr as BTstack stores it (most significant byte first).
const uint8_t kPadA[6] = {0x84, 0x30, 0x95, 0x90, 0xE9, 0x69};
const uint8_t kPadB[6] = {0x5C, 0x0C, 0xE6, 0x11, 0x9B, 0xE5};

// The tested dongle's MAC, least significant byte first (mocks/pico/unique_id.h).
const uint8_t kDongleLsb[6] = {0x8D, 0xEE, 0x1A, 0x16, 0xDB, 0xB6};

void set_option(bool on) {
    dongle_settings::Settings s = dongle_settings::defaults();
    s.mac_per_controller = on ? 1 : 0;
    dongle_settings::set(s);
}

bool is_pad(const uint8_t lsb[6], const uint8_t bd_addr[6]) {
    for (int i = 0; i < 6; ++i)
        if (lsb[i] != bd_addr[5 - i]) return false;
    return true;
}

}  // namespace

// ReportedMac keeps state for the whole boot, so the tests run as one sequence per scenario.

TEST(option_off_always_reports_the_dongle) {
    set_option(false);
    reported_mac::init();
    uint8_t mac[6];
    reported_mac::get_lsb_first(mac);
    CHECK(std::memcmp(mac, kDongleLsb, 6) == 0);
    reported_mac::on_pad_ready(kPadA);         // ignored while the option is off
    reported_mac::get_lsb_first(mac);
    CHECK(std::memcmp(mac, kDongleLsb, 6) == 0);
    CHECK(!reported_mac::take_reconnect_request());
}

TEST(option_on_reconnects_once_then_reports_the_pad) {
    set_option(true);
    // The host enumerated at boot and read the dongle MAC; then the pad connects.
    uint8_t mac[6];
    reported_mac::get_lsb_first(mac);
    CHECK(std::memcmp(mac, kDongleLsb, 6) == 0);
    reported_mac::on_pad_ready(kPadA);
    CHECK(reported_mac::take_reconnect_request());
    CHECK(!reported_mac::take_reconnect_request());   // one reconnect per change
    reported_mac::get_lsb_first(mac);                 // after re-enumeration
    CHECK(is_pad(mac, kPadA));
    // A second pad (e.g. the other Joy-Con of a pair) does not change it.
    reported_mac::on_pad_ready(kPadB);
    CHECK(!reported_mac::take_reconnect_request());
    reported_mac::get_lsb_first(mac);
    CHECK(is_pad(mac, kPadA));
}

TEST_MAIN()
