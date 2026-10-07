// Dongle options (Custom/DongleSettings): build-time defaults and the 16-byte wire/flash format
// (version 3; version 2 and version 1 records, the first 8 bytes, are still accepted).
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
    CHECK_EQ(d.joycon_pair_rumble_per_side, 1);
    CHECK_EQ(dongle_settings::full_search_s(d), 60);
    CHECK_EQ(dongle_settings::reduced_search_s(d), dongle_settings::kSearchNoLimit);
}

TEST(decode_normalises_flags) {
    const uint8_t bytes[16] = {dongle_settings::kVersion, 1, 0, 7, 0, 0xFF, 1, 0x55, 0};
    Settings s{};
    CHECK(dongle_settings::decode(bytes, sizeof(bytes), s));
    CHECK_EQ(s.disconnect_pads_on_mode_change, 1);
    CHECK_EQ(s.joycon_pair_imu_right, 0);
    CHECK_EQ(s.joycon_pair_horizontal, 1);
    CHECK_EQ(s.joycon_solo_horizontal, 0);
    CHECK_EQ(s.mac_per_controller, 1);
    CHECK_EQ(s.ps4_legacy_motion_scale, 1);
    CHECK_EQ(s.unused_single_controller, 0);  // byte 7 is no option in version 3
    CHECK_EQ(s.joycon_pair_rumble_per_side, 0);
}

TEST(decode_accepts_version_1_with_defaults_for_newer_options) {
    const uint8_t v1[8] = {1, 1, 0, 1, 0, 1, 0, 1};
    Settings s{};
    CHECK(dongle_settings::decode(v1, sizeof(v1), s));
    CHECK_EQ(s.version, dongle_settings::kVersion);  // stored again as version 3
    CHECK_EQ(s.disconnect_pads_on_mode_change, 1);
    CHECK_EQ(s.joycon_pair_horizontal, 1);
    CHECK_EQ(s.mac_per_controller, 1);
    CHECK_EQ(dongle_settings::full_search_s(s), 0);  // its single controller byte was on
    CHECK_EQ(s.joycon_pair_rumble_per_side, dongle_settings::defaults().joycon_pair_rumble_per_side);
}

TEST(decode_rejects_other_versions_and_short_buffers) {
    Settings s = dongle_settings::defaults();
    const uint8_t other[16] = {static_cast<uint8_t>(dongle_settings::kVersion + 1), 1, 1, 1, 1, 1, 1, 0};
    CHECK(!dongle_settings::decode(other, sizeof(other), s));
    const uint8_t shorty[8] = {dongle_settings::kVersion, 1, 1, 1, 1, 1, 1, 1};  // v3 needs 16
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

TEST(mode_combo_mask_in_bytes_12_to_15) {
    uint8_t bytes[16] = {dongle_settings::kVersion, 1, 1, 0, 1, 0, 0, 0, 1};
    Settings s{};
    CHECK(dongle_settings::decode(bytes, sizeof(bytes), s));
    CHECK_EQ(s.combo_disabled_modes, 0u);  // older records: every combo on
    CHECK(dongle_settings::mode_combo_enabled(s, 4));
    bytes[12] = 0x10;  // bit 4 (XInput) off
    bytes[14] = 0x02;  // bit 17 (Mouse + Keyboard) off
    CHECK(dongle_settings::decode(bytes, sizeof(bytes), s));
    CHECK(!dongle_settings::mode_combo_enabled(s, 4));
    CHECK(!dongle_settings::mode_combo_enabled(s, 17));
    CHECK(dongle_settings::mode_combo_enabled(s, 8));
    CHECK(dongle_settings::mode_combo_enabled(s, 100));  // Web App: outside the mask, always on
}

TEST(a_change_of_the_combo_mask_or_search_times_alone_needs_no_restart) {
    Settings a = dongle_settings::defaults();
    Settings b = a;
    b.combo_disabled_modes = 0x10;
    dongle_settings::set_search_times(b, 120, 0);
    CHECK(dongle_settings::same_except_live(a, b));
    b.mac_per_controller = !a.mac_per_controller;
    CHECK(!dongle_settings::same_except_live(a, b));
}

TEST(search_times_are_two_12_bit_fields_in_bytes_9_to_11) {
    uint8_t bytes[16] = {dongle_settings::kVersion};
    // full 600 s (0x258), reduced 0xFFF (no limit): 58 | 2 + F<<4 | FF
    bytes[9] = 0x58;
    bytes[10] = 0xF2;
    bytes[11] = 0xFF;
    Settings s{};
    CHECK(dongle_settings::decode(bytes, sizeof(bytes), s));
    CHECK_EQ(dongle_settings::full_search_s(s), 600);
    CHECK_EQ(dongle_settings::reduced_search_s(s), dongle_settings::kSearchNoLimit);
    dongle_settings::set_search_times(s, 1, 599);
    CHECK_EQ(dongle_settings::full_search_s(s), 1);
    CHECK_EQ(dongle_settings::reduced_search_s(s), 599);
    dongle_settings::set_search_times(s, 4000, 700);  // clamped to 10 minutes
    CHECK_EQ(dongle_settings::full_search_s(s), 600);
    CHECK_EQ(dongle_settings::reduced_search_s(s), 600);
    CHECK_EQ(s.combo_disabled_modes, 0u);  // the mask next to them is untouched
}

TEST(older_records_get_default_search_times_or_none_for_single_controller) {
    uint8_t v2[16] = {2, 1, 1, 0, 1, 0, 0, 0, 1};
    Settings s{};
    CHECK(dongle_settings::decode(v2, sizeof(v2), s));
    CHECK_EQ(s.version, dongle_settings::kVersion);
    CHECK_EQ(dongle_settings::full_search_s(s), dongle_settings::kDefaultFullSearchS);
    CHECK_EQ(dongle_settings::reduced_search_s(s), dongle_settings::kSearchNoLimit);
    v2[7] = 1;  // single controller option on: no search once a pad is connected
    CHECK(dongle_settings::decode(v2, sizeof(v2), s));
    CHECK_EQ(dongle_settings::full_search_s(s), 0);
    CHECK_EQ(dongle_settings::reduced_search_s(s), 0);
    CHECK_EQ(s.unused_single_controller, 0);
    const uint8_t v1[8] = {1, 0, 1, 0, 1, 0, 0, 1};
    CHECK(dongle_settings::decode(v1, sizeof(v1), s));
    CHECK_EQ(dongle_settings::reduced_search_s(s), 0);
}

TEST_MAIN()
