// Dongle options (Custom/DongleSettings): build-time defaults and the 16-byte wire/flash format
// (version 2; version 1 records, the first 8 bytes, are still accepted).
#include "Custom/DongleSettings.h"
#include "test.h"

using dongle_settings::Settings;

TEST(defaults_follow_the_build) {
    const Settings d = dongle_settings::defaults();
    CHECK_EQ(d.version, dongle_settings::kVersion);
    CHECK_EQ(d.disconnect_pads_on_mode_change, 0);  // host test build: option not defined
    CHECK_EQ(d.joycon_pair_imu_right, 1);
    CHECK_EQ(d.joycon_pair_horizontal, 0);
    CHECK_EQ(d.joycon_solo_horizontal, 1);
    CHECK_EQ(d.mac_per_controller, 0);
    CHECK_EQ(d.ps4_legacy_motion_scale, 0);
    CHECK_EQ(d.single_controller, 0);
    CHECK_EQ(d.joycon_pair_rumble_per_side, 1);
}

TEST(decode_normalises_flags) {
    const uint8_t bytes[16] = {dongle_settings::kVersion, 1, 0, 7, 0, 0xFF, 1, 0x55, 0, 9, 9};
    Settings s{};
    CHECK(dongle_settings::decode(bytes, sizeof(bytes), s));
    CHECK_EQ(s.disconnect_pads_on_mode_change, 1);
    CHECK_EQ(s.joycon_pair_imu_right, 0);
    CHECK_EQ(s.joycon_pair_horizontal, 1);
    CHECK_EQ(s.joycon_solo_horizontal, 0);
    CHECK_EQ(s.mac_per_controller, 1);
    CHECK_EQ(s.ps4_legacy_motion_scale, 1);
    CHECK_EQ(s.single_controller, 1);
    CHECK_EQ(s.joycon_pair_rumble_per_side, 0);
    CHECK_EQ(s.reserved[0], 0);  // unknown bytes not carried over
}

TEST(decode_accepts_version_1_with_defaults_for_newer_options) {
    const uint8_t v1[8] = {1, 1, 0, 1, 0, 1, 0, 1};
    Settings s{};
    CHECK(dongle_settings::decode(v1, sizeof(v1), s));
    CHECK_EQ(s.version, dongle_settings::kVersion);  // stored again as version 2
    CHECK_EQ(s.disconnect_pads_on_mode_change, 1);
    CHECK_EQ(s.joycon_pair_horizontal, 1);
    CHECK_EQ(s.mac_per_controller, 1);
    CHECK_EQ(s.single_controller, 1);
    CHECK_EQ(s.joycon_pair_rumble_per_side, dongle_settings::defaults().joycon_pair_rumble_per_side);
}

TEST(decode_rejects_other_versions_and_short_buffers) {
    Settings s = dongle_settings::defaults();
    const uint8_t other[16] = {static_cast<uint8_t>(dongle_settings::kVersion + 1), 1, 1, 1, 1, 1, 1, 0};
    CHECK(!dongle_settings::decode(other, sizeof(other), s));
    const uint8_t shorty[8] = {dongle_settings::kVersion, 1, 1, 1, 1, 1, 1, 1};  // v2 needs 16
    CHECK(!dongle_settings::decode(shorty, sizeof(shorty), s));
    const uint8_t short_v1[4] = {1, 1, 1, 1};
    CHECK(!dongle_settings::decode(short_v1, sizeof(short_v1), s));
    CHECK(!dongle_settings::decode(nullptr, 8, s));
    CHECK_EQ(s.mac_per_controller, 0);  // untouched on failure
}

TEST(set_replaces_current) {
    CHECK_EQ(dongle_settings::get().ps4_legacy_motion_scale, 0);
    Settings s = dongle_settings::defaults();
    s.ps4_legacy_motion_scale = 1;
    dongle_settings::set(s);
    CHECK_EQ(dongle_settings::get().ps4_legacy_motion_scale, 1);
    dongle_settings::set(dongle_settings::defaults());
}

TEST_MAIN()
