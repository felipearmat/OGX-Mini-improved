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
    setup();
    diag::slot_connected(0, 0, "pad", 1, 2, 0, false, 0x0b, kAddr);
    for (uint32_t i = 0; i < 100; ++i)
        diag::slot_counter(0, i * 7, 8);  // not a per-report counter
    r = report(10);
    CHECK(has(r, "\"lost_reports_pct\":null"));
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
    CHECK(has(r, "\"bluetooth\":{\"bredr_inquiry_running\":false,\"bredr_inquiries\":0,\"accepting_new_controllers\":true}"));
    diag::inquiry_complete(2000);
    CHECK(has(report(9000), "\"bredr_inquiry_running\":true,\"bredr_inquiries\":1"));
    CHECK(has(report(30000), "\"bredr_inquiry_running\":false"));
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

TEST(session_summary_survives_as_previous_session) {
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
    setup();  // the reboot
    diag::set_previous_session(blob, n);
    const std::string r = report(5);
    CHECK(has(r, "\"previous_session\":{\"mode\":\"XINPUT\",\"uptime_s\":10,\"usb_configured_s\":10,\"usb_reports_sent\":5000"));
    CHECK(has(r, "\"usb_reports_sent_per_s\":500"));
    CHECK(has(r, "\"input_to_output_latency\":{\"samples\":900,\"avg_us\":1200,\"max_us\":4800}"));
    CHECK(has(r, "\"reports_per_s\":100"));
    CHECK(has(r, "\"le_interval_ms\":30.00"));
    blob[0] = 99;  // another format version is ignored
    diag::set_previous_session(blob, n);
    CHECK(!has(report(6), "previous_session"));
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

TEST_MAIN()
