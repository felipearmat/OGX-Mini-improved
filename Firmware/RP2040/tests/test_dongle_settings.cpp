// Dongle options (UserSettings/DongleSettings): build-time defaults and the 24-byte wire/flash format
// (version 8; records of versions 1-7 are still accepted).
#include "UserSettings/DongleSettings.h"
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
    CHECK_EQ(d.combo_disabled_modes, 0u);
    CHECK_EQ(d.idle_off_minutes, 15);
    CHECK_EQ(d.link_diagnostics, 0);  // OGXM_DIAGNOSTICS_LINK not defined
}

TEST(decode_normalises_flags) {
    const uint8_t bytes[24] = {dongle_settings::kVersion, 1, 0, 7, 0, 0xFF, 1, 0x55, 0};
    Settings s{};
    CHECK(dongle_settings::decode(bytes, sizeof(bytes), s));
    CHECK_EQ(s.disconnect_pads_on_mode_change, 1);
    CHECK_EQ(s.joycon_pair_imu_right, 0);
    CHECK_EQ(s.joycon_pair_horizontal, 1);
    CHECK_EQ(s.joycon_solo_horizontal, 0);
    CHECK_EQ(s.mac_per_controller, 1);
    CHECK_EQ(s.ps4_legacy_motion_scale, 1);
    CHECK_EQ(s.link_diagnostics, 1);  // byte 7: detailed controller diagnostics since version 8
    CHECK_EQ(s.joycon_pair_rumble_per_side, 0);
}

TEST(decode_accepts_version_1_with_defaults_for_newer_options) {
    const uint8_t v1[8] = {1, 1, 0, 1, 0, 1, 0, 1};
    Settings s{};
    CHECK(dongle_settings::decode(v1, sizeof(v1), s));
    CHECK_EQ(s.version, dongle_settings::kVersion);  // stored again as the current version
    CHECK_EQ(s.disconnect_pads_on_mode_change, 1);
    CHECK_EQ(s.joycon_pair_horizontal, 1);
    CHECK_EQ(s.mac_per_controller, 1);
    CHECK_EQ(dongle_settings::full_search_s(s), 0);  // its single controller byte was on
    CHECK_EQ(s.joycon_pair_rumble_per_side, dongle_settings::defaults().joycon_pair_rumble_per_side);
    CHECK_EQ(s.link_diagnostics, 0);  // byte 7 was its single controller option, not this one
}

TEST(decode_rejects_other_versions_and_short_buffers) {
    Settings s = dongle_settings::defaults();
    const uint8_t other[24] = {static_cast<uint8_t>(dongle_settings::kVersion + 1), 1, 1, 1, 1, 1, 1, 0};
    CHECK(!dongle_settings::decode(other, sizeof(other), s));
    const uint8_t shorty[23] = {dongle_settings::kVersion, 1, 1, 1, 1, 1, 1, 1};  // v8 needs 24
    CHECK(!dongle_settings::decode(shorty, sizeof(shorty), s));
    const uint8_t short_v3[8] = {3, 1, 1, 1, 1, 1, 1, 1};  // v3 needs 16
    CHECK(!dongle_settings::decode(short_v3, sizeof(short_v3), s));
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
    uint8_t bytes[24] = {dongle_settings::kVersion, 1, 1, 0, 1, 0, 0, 0, 1};
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

TEST(a_change_of_the_combo_mask_search_times_or_idle_time_alone_needs_no_restart) {
    Settings a = dongle_settings::defaults();
    Settings b = a;
    b.combo_disabled_modes = 0x10;
    dongle_settings::set_search_times(b, 120, 0);
    b.idle_off_minutes = 0;
    CHECK(dongle_settings::same_except_live(a, b));
    b.mac_per_controller = !a.mac_per_controller;
    CHECK(!dongle_settings::same_except_live(a, b));
}

TEST(search_times_are_two_12_bit_fields_in_bytes_9_to_11) {
    uint8_t bytes[24] = {dongle_settings::kVersion};
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
    CHECK_EQ(dongle_settings::full_search_s(s), dongle_settings::full_search_s(dongle_settings::defaults()));
    CHECK_EQ(dongle_settings::reduced_search_s(s), dongle_settings::kSearchNoLimit);
    v2[7] = 1;  // single controller option on: no search once a pad is connected
    CHECK(dongle_settings::decode(v2, sizeof(v2), s));
    CHECK_EQ(dongle_settings::full_search_s(s), 0);
    CHECK_EQ(dongle_settings::reduced_search_s(s), 0);
    CHECK_EQ(s.link_diagnostics, 0);  // the single controller byte does not turn this on
    const uint8_t v1[8] = {1, 0, 1, 0, 1, 0, 0, 1};
    CHECK(dongle_settings::decode(v1, sizeof(v1), s));
    CHECK_EQ(dongle_settings::reduced_search_s(s), 0);
}

TEST(idle_off_minutes_in_byte_16) {
    uint8_t bytes[24] = {dongle_settings::kVersion};
    bytes[16] = 30;
    Settings s{};
    CHECK(dongle_settings::decode(bytes, sizeof(bytes), s));
    CHECK_EQ(s.idle_off_minutes, 30);
    bytes[16] = 0;  // never
    CHECK(dongle_settings::decode(bytes, sizeof(bytes), s));
    CHECK_EQ(s.idle_off_minutes, 0);
}

TEST(version_3_records_get_the_default_idle_time) {
    uint8_t v3[16] = {3, 1, 1, 0, 1, 0, 0, 0, 1};
    v3[9] = 0x1E;  // full search 30 s, reduced 0
    v3[12] = 0x10;
    Settings s{};
    CHECK(dongle_settings::decode(v3, sizeof(v3), s));
    CHECK_EQ(s.version, dongle_settings::kVersion);
    CHECK_EQ(dongle_settings::full_search_s(s), 30);
    CHECK_EQ(s.combo_disabled_modes, 0x10u);
    CHECK_EQ(s.idle_off_minutes, dongle_settings::defaults().idle_off_minutes);
}

TEST(version_5_high_tx_power) {
    uint8_t bytes[18] = {5};
    bytes[16] = 15;
    bytes[17] = 1;
    dongle_settings::Settings s{};
    CHECK(dongle_settings::decode(bytes, sizeof(bytes), s));
    CHECK_EQ(s.high_tx_power, 1);
    CHECK_EQ(s.idle_off_minutes, 15);
    // a version 4 record (17 bytes) keeps the default: off
    uint8_t v4[17] = {4};
    v4[16] = 10;
    CHECK(dongle_settings::decode(v4, sizeof(v4), s));
    CHECK_EQ(s.high_tx_power, 0);
    CHECK_EQ(s.idle_off_minutes, 10);
    // applied live: no restart for this option alone
    dongle_settings::Settings a = dongle_settings::defaults(), b = a;
    b.high_tx_power = 1;
    CHECK(dongle_settings::same_except_live(a, b));
}

TEST(version_6_status_leds_applied_live) {
    uint8_t bytes[24] = {dongle_settings::kVersion};
    bytes[18] = 0;   // Pico LED off
    bytes[19] = 1;   // Pico external LED on
    bytes[20] = 7;   // ESP32 LED on (normalised)
    bytes[21] = 0;   // ESP32 external LED off
    bytes[22] = 200; // brightness
    Settings s{};
    CHECK(dongle_settings::decode(bytes, sizeof(bytes), s));
    CHECK_EQ(s.status_led_pico, 0);
    CHECK_EQ(s.status_led_pico_ext, 1);
    CHECK_EQ(s.status_led_esp32, 1);
    CHECK_EQ(s.status_led_esp32_ext, 0);
    CHECK_EQ(s.status_led_brightness, 200);
    // brightness 0 reads as the default
    bytes[22] = 0;
    CHECK(dongle_settings::decode(bytes, sizeof(bytes), s));
    CHECK_EQ(s.status_led_brightness, dongle_settings::defaults().status_led_brightness);
    // a version 5 record keeps the defaults: every LED on
    uint8_t v5[18] = {5};
    CHECK(dongle_settings::decode(v5, sizeof(v5), s));
    CHECK_EQ(s.status_led_pico, 1);
    CHECK_EQ(s.status_led_pico_ext, 1);
    CHECK_EQ(s.status_led_esp32, 1);
    CHECK_EQ(s.status_led_esp32_ext, 1);
    // no restart for them
    Settings a = dongle_settings::defaults(), b = a;
    b.status_led_pico = 0;
    b.status_led_pico_ext = 0;
    b.status_led_esp32 = 0;
    b.status_led_esp32_ext = 0;
    b.status_led_brightness = 10;
    CHECK(dongle_settings::same_except_live(a, b));
}

TEST(v7_keeps_the_log_in_flash_option_and_reads_v6_records) {
    uint8_t bytes[24] = {dongle_settings::kVersion};
    bytes[23] = 1;
    dongle_settings::Settings s{};
    CHECK(dongle_settings::decode(bytes, sizeof(bytes), s));
    CHECK_EQ(s.keep_log_in_flash, 1);
    // A version 6 record (23 bytes): the option takes its build default.
    uint8_t v6[23] = {6};
    v6[22] = 100;
    CHECK(dongle_settings::decode(v6, sizeof(v6), s));
    CHECK_EQ(s.version, dongle_settings::kVersion);
    CHECK_EQ(s.status_led_brightness, 100);
    CHECK_EQ(s.keep_log_in_flash, dongle_settings::defaults().keep_log_in_flash);
}

TEST(the_log_in_flash_option_is_applied_live) {
    dongle_settings::Settings a = dongle_settings::defaults();
    dongle_settings::Settings b = a;
    b.keep_log_in_flash = !a.keep_log_in_flash;
    CHECK(dongle_settings::same_except_live(a, b));
}

TEST(the_i2c_link_carries_the_v6_record) {
    // The hybrid's ESP32 reads the version 6 record, so its packet keeps its size and an ESP32
    // with older firmware still takes the settings.
    dongle_settings::Settings s = dongle_settings::defaults();
    s.status_led_brightness = 77;
    s.keep_log_in_flash = 1;
    uint8_t v6[dongle_settings::kV6Length];
    dongle_settings::encode_v6(s, v6);
    CHECK_EQ(v6[0], 6);
    CHECK_EQ(v6[22], 77);
    dongle_settings::Settings back{};
    CHECK(dongle_settings::decode(v6, sizeof(v6), back));
    CHECK_EQ(back.status_led_brightness, 77);
}

TEST(v8_link_diagnostics_in_byte_7_applied_live_and_carried_to_the_esp32) {
    uint8_t bytes[24] = {dongle_settings::kVersion};
    bytes[7] = 1;
    dongle_settings::Settings s{};
    CHECK(dongle_settings::decode(bytes, sizeof(bytes), s));
    CHECK_EQ(s.link_diagnostics, 1);
    // A stored version 7 / 6 record: byte 7 was always zero there, so off.
    uint8_t v7[24] = {7};
    CHECK(dongle_settings::decode(v7, sizeof(v7), s));
    CHECK_EQ(s.link_diagnostics, 0);
    // Version 3-5 records: the build default.
    uint8_t v5[18] = {5};
    v5[7] = 1;
    CHECK(dongle_settings::decode(v5, sizeof(v5), s));
    CHECK_EQ(s.link_diagnostics, dongle_settings::defaults().link_diagnostics);
    // The hybrid's ESP32 gets it in byte 7 of the version 6 record (an older ESP32 ignores it).
    dongle_settings::Settings on = dongle_settings::defaults();
    on.link_diagnostics = 1;
    uint8_t v6[dongle_settings::kV6Length];
    dongle_settings::encode_v6(on, v6);
    CHECK_EQ(v6[7], 1);
    dongle_settings::Settings back{};
    CHECK(dongle_settings::decode(v6, sizeof(v6), back));
    CHECK_EQ(back.link_diagnostics, 1);
    // No restart for it.
    CHECK(dongle_settings::same_except_live(dongle_settings::defaults(), on));
}

TEST_MAIN()
