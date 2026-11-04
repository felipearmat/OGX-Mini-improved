// The pad's battery in each output mode's report (Gamepad/BatteryReport.h) and the battery of a
// Bluetooth pad read from its own reports, a Joy-Con pair's lower half (Bluepad32/PadBattery.h).
// 2026-11: a Joy-Con ran out mid-session; the Switch mode said "full" and no mode said charging.
#include "Gamepad/BatteryReport.h"
#include "Bluepad32/PadBattery.h"
#include "Bluepad32/InputWatch.h"
#include "test.h"

namespace br = battery_report;

TEST(unknown_reads_as_before) {
    CHECK_EQ(br::ds4(0, false), 0x1B);
    CHECK_EQ(br::ds5(0, false), 0x2A);
    CHECK_EQ(br::switch_pro(0, false), 0x81);
    CHECK_EQ(br::ds3(0, false).power_status, 0x05);
}

TEST(levels_and_charging_in_each_mode) {
    CHECK_EQ(br::ds4(26, false), 1);             // 10 %
    CHECK_EQ(br::ds4(26, true), 0x11);           // cable bit
    CHECK_EQ(br::ds4(255, true), 0x1B);          // full on a cable
    CHECK_EQ(br::ds5(128, false), 5);
    CHECK_EQ(br::ds5(128, true), 0x15);          // charging
    CHECK_EQ(br::ds5(255, true), 0x2A);          // full
    CHECK_EQ(br::switch_pro(255, false), 0x81);  // full
    CHECK_EQ(br::switch_pro(26, false), 0x21);   // critical
    CHECK_EQ(br::switch_pro(26, true), 0x31);    // critical, charging
    CHECK_EQ(br::switch_pro(166, false), 0x61);  // medium (65 %)
    CHECK_EQ(br::ds3(26, false).power_status, 0x01);
    CHECK_EQ(br::ds3(26, false).plugged, 0x03);
    CHECK_EQ(br::ds3(26, true).power_status, 0xEE);
}

TEST(switch_status_byte) {
    const auto full_charging = pad_battery::from_switch(0x90 | 0x01);  // level 4, bit 4
    CHECK_EQ(full_charging.percent, 100);
    CHECK(full_charging.state == diag::BatteryState::FULL);
    const auto critical = pad_battery::from_switch(0x20 | 0x0E);       // level 1
    CHECK_EQ(critical.percent, 10);
    CHECK(critical.state == diag::BatteryState::DISCHARGING);
    const auto low_charging = pad_battery::from_switch(0x40 | 0x10);   // level 2, charging
    CHECK(low_charging.state == diag::BatteryState::CHARGING);
}

TEST(a_pair_gives_its_lower_half_and_critical_needs_no_charger) {
    pad_battery::clear(0);
    pad_battery::clear(1);
    pad_battery::set(0, 65, diag::BatteryState::DISCHARGING);
    pad_battery::set(1, 10, diag::BatteryState::DISCHARGING);
    pad_battery::State s = pad_battery::for_pad(0, 1);
    CHECK_EQ(s.percent, 10);
    CHECK(pad_battery::critical(s));
    pad_battery::set(1, 10, diag::BatteryState::CHARGING);  // on a charger: no alert
    s = pad_battery::for_pad(0, 1);
    CHECK(!pad_battery::critical(s));
    CHECK(pad_battery::charging(s.state));
    CHECK_EQ(pad_battery::for_pad(0, -1).percent, 65);  // a lone pad: its own
    // Both full, one off its charger: the pair is not charging, from either half.
    pad_battery::set(0, 100, diag::BatteryState::FULL);
    pad_battery::set(1, 100, diag::BatteryState::DISCHARGING);
    CHECK(!pad_battery::charging(pad_battery::for_pad(0, 1).state));
    CHECK(!pad_battery::charging(pad_battery::for_pad(1, 0).state));
    pad_battery::set(0, 65, diag::BatteryState::DISCHARGING);
    pad_battery::clear(1);
    CHECK_EQ(pad_battery::for_pad(0, 1).percent, 65);   // the partner gone
    CHECK(!pad_battery::for_pad(3, -1).known);
}

TEST(for_host_is_what_the_pico_w_and_the_esp32_send) {
    // One function for both boards (Bluepad32/BtPad.h host_battery): level, charging, critical.
    for (int i = 0; i < 8; ++i) pad_battery::clear(i);
    pad_battery::ForHost h = pad_battery::for_host(0, 1, 200);
    CHECK_EQ(h.level, 200);  // nothing from the reports yet: Bluepad32's level
    CHECK(!h.charging && !h.critical);
    CHECK(!pad_battery::any_critical());
    pad_battery::set(0, 65, diag::BatteryState::CHARGING);
    pad_battery::set(1, 10, diag::BatteryState::DISCHARGING);
    h = pad_battery::for_host(0, 1, 200);
    CHECK_EQ(h.level, pad_battery::to_255(10));  // the lower half
    CHECK(!h.charging);                          // ... which is off its charger
    CHECK(h.critical);
    CHECK(pad_battery::for_host(0, -1, 0).charging);    // the left half alone
    CHECK(!pad_battery::for_host(0, -1, 0).critical);
    CHECK(pad_battery::any_critical());
    pad_battery::clear(1);  // the critical half went
    CHECK(!pad_battery::any_critical());
}

TEST(a_quiet_switch_pad_is_asked_again_then_dropped) {
    // 2026-11-04: a Joy-Con (R) finished its setup twice and stayed at 3-4 reports a second.
    namespace iw = input_watch;
    iw::on_ready(1, 1000);
    CHECK(iw::check(1, 3000) == iw::Action::NONE);                  // still in its grace time
    for (int i = 0; i < 10; ++i) iw::on_report(1);
    uint32_t n = 0;
    CHECK(iw::check(1, 4000, &n) == iw::Action::ASK_AGAIN);
    CHECK_EQ(n, 10u);
    CHECK(iw::check(1, 6000) == iw::Action::NONE);                  // a new grace time
    CHECK(iw::check(1, 7000) == iw::Action::DROP);                  // still quiet
    CHECK(iw::check(1, 20000) == iw::Action::NONE);                 // once
    // A pad that streams is left alone.
    iw::on_ready(2, 0);
    for (int i = 0; i < 200; ++i) iw::on_report(2);
    CHECK(iw::check(2, 3000) == iw::Action::NONE);
    CHECK(iw::check(2, 9000) == iw::Action::NONE);
    iw::clear(1);
    CHECK(iw::check(1, 50000) == iw::Action::NONE);
}

TEST_MAIN()
