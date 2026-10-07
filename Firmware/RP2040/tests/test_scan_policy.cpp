// Search for new controllers while others are connected (Custom/ScanPolicy.h). Regression covered:
// Bluepad32 kept a 100% duty BLE scan next to a Classic pad, and a DS4 lost 7 reports in 8.
#include "Custom/ScanPolicy.h"
#include "test.h"

using scan_policy::Scan;
using scan_policy::decide;

namespace {
const scan_policy::Times kDefault{60000, scan_policy::kNoLimit};
}

TEST(no_pad_searches_fully) {
    CHECK(decide(0, 1, false, 0, kDefault) == Scan::Full);
    CHECK(decide(0, 4, false, 0, {0, 0}) == Scan::Full);  // even with both times 0
    CHECK(decide(0, 1, false, 0, kDefault, true) == Scan::Full);
}

TEST(all_slots_in_use_stops_the_search) {
    CHECK(decide(1, 1, false, 0, kDefault) == Scan::Off);  // one DS4, or a Joy-Con pair (one output)
    CHECK(decide(4, 4, false, 99999, kDefault) == Scan::Off);
}

TEST(an_open_slot_searches_fully_then_reduced) {
    CHECK(decide(1, 4, false, 0, kDefault) == Scan::Full);
    CHECK(decide(1, 4, false, 59999, kDefault) == Scan::Full);
    CHECK(decide(1, 4, false, 60000, kDefault) == Scan::Reduced);
    CHECK(decide(1, 1, true, 3600000, kDefault) == Scan::Reduced);  // lone Joy-Con, no limit
}

TEST(reduced_search_time_runs_out) {
    const scan_policy::Times t{15000, 30000};
    CHECK(decide(1, 1, true, 14999, t) == Scan::Full);
    CHECK(decide(1, 1, true, 15000, t) == Scan::Reduced);
    CHECK(decide(1, 1, true, 44999, t) == Scan::Reduced);
    CHECK(decide(1, 1, true, 45000, t) == Scan::Off);
}

TEST(both_times_zero_stop_the_search_once_a_pad_is_connected) {
    const scan_policy::Times none{0, 0};
    CHECK(decide(1, 1, true, 0, none) == Scan::Off);
    CHECK(decide(1, 4, false, 0, none) == Scan::Off);
    const scan_policy::Times full_only{20000, 0};
    CHECK(decide(1, 1, true, 19999, full_only) == Scan::Full);
    CHECK(decide(1, 1, true, 20000, full_only) == Scan::Off);
}

TEST(stopped_from_the_controller) {
    CHECK(decide(1, 1, true, 0, kDefault, true) == Scan::Off);
    CHECK(decide(1, 4, false, 0, kDefault, true) == Scan::Off);
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
