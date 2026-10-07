// Diagnostics (Custom/Diagnostics): per-controller identity, link and input timing, wired
// controllers, USB output, the session summary kept across the mode change, and the JSON report.
#include <cstring>
#include <string>

#include "Custom/Diagnostics.h"
#include "test.h"

namespace {

std::string report(uint32_t now_ms) {
    static char buf[16384];
    diag::report_json(buf, sizeof(buf), now_ms);
    return buf;
}

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

const uint8_t kAddr[6] = {0x98, 0x7B, 0xF3, 0x11, 0x22, 0x33};

void setup() {
    diag::reset_for_tests();
    diag::init(diag::BoardInfo{"v1.0.0.13a", "PI_PICO2W", "RP2350", 150, "Release", "XINPUT", "power-on", 1});
}

}  // namespace

TEST(report_has_board_info_and_no_controllers_when_idle) {
    setup();
    const std::string r = report(1234);
    CHECK(has(r, "\"firmware\":\"v1.0.0.13a\""));
    CHECK(has(r, "\"chip\":\"RP2350\",\"clock_mhz\":150"));
    CHECK(has(r, "\"output_mode\":\"XINPUT\""));
    CHECK(has(r, "\"controllers\":[]"));
    CHECK(has(r, "\"wired_controllers\":[]"));
    CHECK(!has(r, "previous_session"));
}

TEST(input_rate_gaps_and_late_reports) {
    setup();
    diag::slot_connected(0, 1000, "Xbox Wireless Controller", 0x045e, 0x0b13, 3, true, 0x40, kAddr);
    uint32_t t = 1000;
    diag::tick(t);
    for (int i = 0; i < 100; ++i) {  // 100 reports 10 ms apart, then 5 late ones (35 ms)
        t += 10;
        diag::slot_report(0, t);
    }
    for (int i = 0; i < 5; ++i) {
        t += 35;
        diag::slot_report(0, t);
    }
    diag::tick(2300);
    const std::string r = report(2300);
    CHECK(has(r, "\"bt_address_prefix\":\"98:7B:F3\""));
    CHECK(!has(r, "11:22:33"));  // device part of the address never reported
    CHECK(has(r, "\"reports\":105"));
    CHECK(has(r, "\"interval_median_ms\":10"));
    CHECK(has(r, "\"late_reports_pct\":4.8"));  // 5 of 104 intervals > 20 ms
    CHECK(has(r, "\"max_gap_ms_recent\":35"));
}

TEST(counter_losses_and_untrusted_counter) {
    setup();
    diag::slot_connected(0, 0, "DS4", 0x054c, 0x09cc, 1, false, 0x0b, kAddr);
    uint32_t c = 0;
    for (int i = 0; i < 200; ++i) {  // 6-bit counter, one report skipped every 20
        c += (i % 20 == 19) ? 2 : 1;
        diag::slot_counter(0, c, 6);
    }
    std::string r = report(10);
    CHECK(has(r, "\"lost_reports_pct\":4.7"));  // 10 lost of 211
    CHECK(has(r, "\"counter_usual_step\":\"1\""));
    setup();
    diag::slot_connected(0, 0, "pad", 1, 2, 0, false, 0x0b, kAddr);
    for (uint32_t i = 0; i < 100; ++i)
        diag::slot_counter(0, i * 131, 8);  // not a counter: jumps of half the range or more
    r = report(10);
    CHECK(has(r, "\"lost_reports_pct\":null"));
    // Half of the reports lost (the DS4 on a link that drops them): every other value received.
    setup();
    diag::slot_connected(0, 0, "DS4", 0x054c, 0x09cc, 1, false, 0x0b, kAddr);
    for (uint32_t i = 0; i < 200; ++i)
        diag::slot_counter(0, i * 2, 6);
    r = report(10);
    CHECK(has(r, "\"lost_reports_pct\":49.8"));  // 199 lost of 399
    CHECK(has(r, "\"counter_usual_step\":\"2\""));
}

TEST(link_details_arrive_before_and_after_the_slot) {
    setup();
    diag::le_parameters(0x41, 24, 4, 72);
    diag::remote_version(0x41, 9, 0x000F, 0x4103);
    diag::device_information(0x41, "firmware", "5.17.3202.0");
    diag::device_information(0x41, "manufacturer", "Microsoft");
    diag::pnp_id(0x41, 2, 0x045e, 0x0b13, 0x0517);
    diag::slot_connected(1, 500, "Xbox", 0x045e, 0x0b13, 3, true, 0x41, kAddr);
    diag::le_parameters(0x41, 7, 0, 600);
    diag::rssi(0x41, -61);
    diag::channels(0x41, 30, 37);
    const std::string r = report(700);
    CHECK(has(r, "\"le_interval_ms\":8.75,\"le_latency\":0,\"le_timeout_ms\":6000"));
    CHECK(has(r, "\"bt_chip_vendor_id\":15,\"bt_chip_vendor\":\"Broadcom\",\"bt_version\":\"5.0\""));
    CHECK(has(r, "\"manufacturer\":\"Microsoft\""));
    CHECK(has(r, "\"firmware\":\"5.17.3202.0\""));
    CHECK(has(r, "\"pnp\":{\"source\":2,\"vid\":\"045e\",\"pid\":\"0b13\",\"version\":\"0517\"}"));
    CHECK(has(r, "\"rssi_dbm\":-61"));
    CHECK(has(r, "\"channels_in_use\":30,\"channels_total\":37"));
}

TEST(classic_switch_battery_and_disconnect) {
    setup();
    diag::slot_connected(0, 0, "Joy-Con (L)", 0x057e, 0x2006, 5, false, 0x0b, kAddr);
    diag::slot_battery(0, 255);
    diag::slot_switch_firmware(0, 4, 33);
    diag::failed_contacts(0x0b, 3);
    diag::rssi(0x0b, 0);
    diag::link_mode(0x0b, 2, 106);  // sniff, 66.25 ms
    std::string r = report(10);
    CHECK(has(r, "\"link\":\"Classic\""));
    CHECK(has(r, "\"rssi_golden_range_db\":0"));  // Classic RSSI is relative, not dBm
    CHECK(!has(r, "rssi_dbm"));
    CHECK(has(r, "\"link_mode\":\"sniff\",\"sniff_interval_ms\":66.25,\"link_mode_changes\":0"));
    diag::link_mode(0x0b, 0, 0);
    CHECK(has(report(11), "\"link_mode\":\"active\""));
    CHECK(has(report(11), "\"link_mode_changes\":1"));
    CHECK(!has(r, "le_interval_ms"));
    CHECK(has(r, "\"battery_pct\":100"));
    CHECK(has(r, "\"switch_firmware\":\"4.33\""));
    CHECK(has(r, "\"failed_contacts\":3"));
    diag::slot_disconnected(0, 20);
    r = report(30);
    CHECK(has(r, "\"controllers\":[]"));
}

TEST(bredr_inquiry_running_only_while_inquiries_complete) {
    setup();
    diag::searching(true);
    std::string r = report(1000);
    CHECK(has(r, "\"bluetooth\":{\"bredr_inquiry_running\":false,\"bredr_inquiries\":0,\"le_scan_adv_reports_per_s\":0,\"accepting_new_controllers\":true}"));
    diag::inquiry_complete(2000);
    CHECK(has(report(9000), "\"bredr_inquiry_running\":true,\"bredr_inquiries\":1"));
    CHECK(has(report(30000), "\"bredr_inquiry_running\":false"));
    for (int i = 0; i < 40; ++i) diag::le_adv_report();
    diag::tick(31000);
    CHECK(has(report(31000), "\"le_scan_adv_reports_per_s\":40"));
}

TEST(wired_controllers_and_receivers) {
    setup();
    diag::usb_mounted(1, 0, 0x045e, 0x0719, 0x0100, 0, "XInput");
    for (uint32_t t = 4; t <= 400; t += 4)
        diag::usb_report(1, t);
    diag::tick(1000);
    const std::string r = report(1000);
    CHECK(has(r, "\"vid\":\"045e\",\"pid\":\"0719\",\"bcd_device\":\"0100\",\"speed\":\"full\""));
    CHECK(has(r, "\"connection\":\"2.4 GHz receiver\""));
    CHECK(has(r, "\"interval_median_ms\":4"));
    diag::usb_unmounted(1);
    CHECK(has(report(1100), "\"wired_controllers\":[]"));
}

TEST(session_summary_stored_in_flash_is_reported) {
    setup();
    diag::usb_output_state(0, true, false);
    for (int i = 0; i < 5000; ++i) diag::usb_report_sent();
    diag::pipeline_latency(900, 1200, 4800);
    diag::slot_connected(0, 0, "Xbox", 0x045e, 0x0b13, 3, true, 0x40, kAddr);
    diag::le_parameters(0x40, 24, 4, 72);
    for (uint32_t t = 10; t <= 10000; t += 10) diag::slot_report(0, t);
    uint8_t blob[diag::kSessionBytes];
    const size_t n = diag::session_capture(blob, sizeof(blob), 10000);
    CHECK(n > 0);
    setup();  // a power cycle: only flash is left
    diag::set_stored_session(blob, n);
    const std::string r = report(5);
    CHECK(!has(r, "previous_session"));
    CHECK(has(r, "\"stored_session\":{\"mode\":\"XINPUT\",\"uptime_s\":10,\"usb_configured_s\":10,\"usb_reports_sent\":5000"));
    CHECK(has(r, "\"usb_reports_sent_per_s\":500"));
    CHECK(has(r, "\"input_to_output_latency\":{\"samples\":900,\"avg_us\":1200,\"max_us\":4800}"));
    CHECK(has(r, "\"reports_per_s\":100"));
    CHECK(has(r, "\"le_interval_ms\":30.00"));
    blob[0] = 99;  // another format version is ignored
    diag::set_stored_session(blob, n);
    CHECK(!has(report(6), "stored_session"));
}

TEST(events_keep_the_latest_in_order_and_are_escaped) {
    setup();
    for (int i = 0; i < 70; ++i)
        diag::event(static_cast<uint32_t>(i), "event %d", i);
    diag::event(100, "name \"quoted\" \\ back");
    const std::string r = report(200);
    CHECK(!has(r, "\"event 6\""));
    CHECK(has(r, "\"event 7\""));
    CHECK(r.find("\"event 7\"") < r.find("\"event 69\""));
    CHECK(has(r, "name \\\"quoted\\\" \\\\ back"));
}

TEST(report_is_truncated_safely) {
    setup();
    diag::event(1, "a long enough event text to overflow a tiny buffer");
    char small[40];
    const size_t n = diag::report_json(small, sizeof(small), 5);
    CHECK(n < sizeof(small));
    CHECK_EQ(small[n], '\0');
}

TEST(mode_change_keeps_the_session_in_ram_across_the_reboot) {
    setup();  // XINPUT
    diag::slot_connected(0, 0, "DS4", 0x054c, 0x09cc, 1, false, 0x0b, kAddr);
    for (uint32_t t = 10; t <= 5000; t += 10) diag::slot_report(0, t);
    diag::session_freeze(5000);           // mode change starts
    diag::slot_disconnected(0, 5100);     // pads turned off
    uint8_t blob[diag::kSessionBytes];
    CHECK(diag::session_capture(blob, sizeof(blob), 5200) > 0);  // still the frozen session
    // The reboot into Web App mode keeps the RAM: no flash write needed.
    diag::init(diag::BoardInfo{"v", "PI_PICO2W", "RP2350", 150, "Release", "WEBAPP", "reboot", 1});
    std::string r = report(5);
    CHECK(has(r, "\"previous_session\":{\"mode\":\"XINPUT\""));
    CHECK(has(r, "\"vid\":\"054c\",\"pid\":\"09cc\",\"link\":\"Classic\",\"reports_per_s\":100"));
    uint8_t again[diag::kSessionBytes];
    CHECK_EQ(diag::session_capture(again, sizeof(again), 6000), 0u);  // a Web App session is not kept
    // Leaving Web App mode (another reboot) does not replace it with a Web App session: it stays.
    diag::session_freeze(7000);
    diag::init(diag::BoardInfo{"v", "PI_PICO2W", "RP2350", 150, "Release", "XINPUT", "reboot", 1});
    CHECK(has(report(5), "\"previous_session\":{\"mode\":\"XINPUT\""));
    CHECK_EQ(diag::take_replaced_session(again, sizeof(again)), 0u);  // nothing written
}

TEST(a_new_session_sends_the_one_in_ram_to_flash_once) {
    setup();  // XINPUT
    diag::slot_connected(0, 0, "DS4", 0x054c, 0x09cc, 1, false, 0x0b, kAddr);
    diag::session_freeze(40000);  // session A ends (mode change)
    uint8_t blob[diag::kSessionBytes];
    CHECK_EQ(diag::take_replaced_session(blob, sizeof(blob)), 0u);  // the RAM was empty
    diag::init(diag::BoardInfo{"v", "PI_PICO2W", "RP2350", 150, "Release", "SWITCH", "reboot", 1});
    diag::slot_connected(0, 0, "DS4", 0x054c, 0x09cc, 1, false, 0x0b, kAddr);
    diag::session_freeze(50000);  // session B ends: A goes to flash, B to RAM
    CHECK(diag::take_replaced_session(blob, sizeof(blob)) > 0);
    CHECK_EQ(blob[1], 'X');  // A was the XINPUT one
    CHECK_EQ(diag::take_replaced_session(blob, sizeof(blob)), 0u);
    // B stored by turning the controller off: the next session does not write it again.
    CHECK(diag::session_capture(blob, sizeof(blob), 50000) > 0);
    diag::mark_session_stored();
    diag::init(diag::BoardInfo{"v", "PI_PICO2W", "RP2350", 150, "Release", "XINPUT", "reboot", 1});
    diag::set_stored_session(blob, sizeof(blob));
    CHECK(!has(report(5), "stored_session"));  // same as the RAM one: shown once
    diag::slot_connected(0, 0, "DS4", 0x054c, 0x09cc, 1, false, 0x0b, kAddr);
    diag::session_freeze(60000);
    CHECK_EQ(diag::take_replaced_session(blob, sizeof(blob)), 0u);
}

TEST(short_sessions_without_a_controller_are_not_kept) {
    setup();
    diag::session_freeze(10000);  // 10 s, no controller: passing through a mode
    uint8_t blob[diag::kSessionBytes];
    CHECK_EQ(diag::session_capture(blob, sizeof(blob), 10000), 0u);
    diag::init(diag::BoardInfo{"v", "PI_PICO2W", "RP2350", 150, "Release", "WEBAPP", "reboot", 1});
    CHECK(!has(report(5), "previous_session"));
    setup();
    diag::session_freeze(45000);  // 45 s: kept even without a controller
    CHECK(diag::session_capture(blob, sizeof(blob), 45000) > 0);
}

TEST(events_survive_a_reboot_that_keeps_the_ram) {
    setup();
    diag::event(100, "before the reboot");
    diag::init(diag::BoardInfo{"v", "PI_PICO2W", "RP2350", 150, "Release", "WEBAPP", "reboot", 1});  // RAM kept
    diag::event(5, "after the reboot");
    const std::string r = report(10);
    CHECK(has(r, "{\"ms\":100,\"boot\":-1,\"text\":\"before the reboot\"}"));
    CHECK(has(r, "{\"ms\":5,\"text\":\"after the reboot\"}"));
    CHECK(r.find("before the reboot") < r.find("after the reboot"));
}

TEST(a_crash_is_reported_after_the_reboot_and_stored_once) {
    setup();
    diag::CrashInfo c{};
    c.kind = 1;
    c.core = 1;
    c.pc = 0x10001234;
    c.has_fault_regs = 1;
    c.cfsr = 0x01000000;
    diag::crash_record(c);
    diag::init(diag::BoardInfo{"v", "PI_PICO2W", "RP2350", 150, "Release", "XINPUT", "reboot", 1});
    std::string r = report(10);
    CHECK(has(r, "\"last_reset\":\"crash (see last_crash)\""));
    CHECK(has(r, "\"last_crash\":{\"when\":\"previous boot\",\"kind\":\"hard fault\",\"core\":1"));
    CHECK(has(r, "\"pc\":\"0x10001234\""));
    CHECK(has(r, "\"cfsr\":\"0x01000000\""));
    uint8_t blob[diag::kCrashBytes];
    CHECK(diag::new_crash(blob, sizeof(blob)) > 0);  // to be stored in flash
    // Next boot (RAM kept, no new crash): nothing new to store; the stored one is reported.
    diag::init(diag::BoardInfo{"v", "PI_PICO2W", "RP2350", 150, "Release", "XINPUT", "reboot", 1});
    CHECK_EQ(diag::new_crash(blob, sizeof(blob)), 0u);
    diag::reset_for_tests();
    diag::init(diag::BoardInfo{"v", "PI_PICO2W", "RP2350", 150, "Release", "XINPUT", "power-on", 1});
    diag::set_stored_crash(blob, sizeof(blob));
    CHECK(has(report(10), "\"when\":\"stored (an earlier boot)\""));
}

TEST(a_panic_keeps_its_message) {
    setup();
    diag::CrashInfo c{};
    c.kind = 2;
    std::snprintf(c.message, sizeof(c.message), "No spin locks are available");
    diag::crash_record(c);
    diag::init(diag::BoardInfo{"v", "PI_PICO2W", "RP2350", 150, "Release", "WEBAPP", "reboot", 1});
    const std::string r = report(10);
    CHECK(has(r, "\"kind\":\"panic\""));
    CHECK(has(r, "\"message\":\"No spin locks are available\""));
    CHECK(!has(r, "\"pc\""));
}

TEST(session_keeps_its_last_events) {
    setup();
    diag::slot_connected(0, 0, "DS4", 0x054c, 0x09cc, 1, false, 0x0b, kAddr);
    for (int i = 0; i < 6; ++i) diag::event(static_cast<uint32_t>(1000 + i), "event %d", i);
    uint8_t blob[diag::kSessionBytes];
    const size_t n = diag::session_capture(blob, sizeof(blob), 2000);
    CHECK(n > 0 && n <= diag::kSessionBytes);
    setup();
    diag::set_stored_session(blob, n);
    const std::string r = report(5);
    CHECK(has(r, "\"last_events\":[{\"ms\":1002,\"text\":\"event 2\"}"));
    CHECK(has(r, "{\"ms\":1005,\"text\":\"event 5\"}]"));
}

TEST(oldest_events_left_out_when_the_report_is_full) {
    setup();
    for (int i = 0; i < 64; ++i) diag::event(static_cast<uint32_t>(i), "a fairly long event text number %d to fill", i);
    static char buf[2500];
    diag::report_json(buf, sizeof(buf), 100);
    const std::string r = buf;
    CHECK(has(r, "event text number 63"));
    CHECK(!has(r, "event text number 0 "));
    CHECK(has(r, "\"events_left_out\":"));
    CHECK_EQ(r.back(), '}');  // still valid JSON: closed
}

TEST_MAIN()
