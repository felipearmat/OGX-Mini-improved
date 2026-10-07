// Search for new controllers while others are connected (Custom/ScanPolicy.h). Regression covered:
// Bluepad32 kept a 100% duty BLE scan next to a Classic pad, and a DS4 lost 7 reports in 8.
#include "Custom/ScanPolicy.h"
#include "test.h"

using scan_policy::Scan;
using scan_policy::decide;

TEST(no_pad_searches_fully) {
    CHECK(decide(0, 1, false, 0) == Scan::Full);
    CHECK(decide(0, 4, false, 0) == Scan::Full);
}

TEST(all_slots_in_use_stops_the_search) {
    CHECK(decide(1, 1, false, 0) == Scan::Off);  // one DS4, or a Joy-Con pair (one output)
    CHECK(decide(4, 4, false, 99999) == Scan::Off);
}

TEST(free_slots_left_reduce_the_ble_scan) {
    CHECK(decide(1, 4, false, 0) == Scan::Full);       // a slot just opened: full search
    CHECK(decide(1, 4, false, 59999) == Scan::Full);
    CHECK(decide(1, 4, false, 60000) == Scan::Reduced);  // still open after a minute
    CHECK(decide(3, 4, false, 120000) == Scan::Reduced);
}

TEST(a_lone_joycon_searches_fully_for_a_minute_then_reduced) {
    CHECK(decide(1, 1, true, 0) == Scan::Full);
    CHECK(decide(1, 1, true, 60000) == Scan::Reduced);  // not Off: it still needs its other half
}

TEST(full_search_time_is_configurable) {
    CHECK(decide(1, 1, true, 14999, 15000) == Scan::Full);
    CHECK(decide(1, 1, true, 15000, 15000) == Scan::Reduced);
}

TEST(reduced_scan_is_a_tenth_of_the_time) {
    CHECK_EQ(scan_policy::kReducedWindow * 10, scan_policy::kReducedInterval);
    CHECK_EQ(scan_policy::kFullWindow, scan_policy::kFullInterval);
    // Periodic inquiry: 3 units in every 30-31 is about 10% (and max > min > length, as HCI requires).
    CHECK(scan_policy::kReducedMaxPeriod > scan_policy::kReducedMinPeriod);
    CHECK(scan_policy::kReducedMinPeriod > scan_policy::kInquiryLength);
    CHECK(scan_policy::kInquiryLength * 10 <= scan_policy::kReducedMinPeriod);
}

TEST_MAIN()
