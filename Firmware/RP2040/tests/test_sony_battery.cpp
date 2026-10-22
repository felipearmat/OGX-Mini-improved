// DS4 / DualSense battery and charging state from the pad's status byte (Gamepad/SonyBattery.h),
// as Linux hid-playstation reads it. Bluepad32 gave a DS4 nibble * 25 + 1 and no charging state.
#include "Gamepad/SonyBattery.h"
#include "test.h"

using diag::BatteryState;

TEST(ds4_on_battery) {
    CHECK_EQ(sony_battery::ds4(0x00).percent, 5);
    CHECK(sony_battery::ds4(0x00).state == BatteryState::DISCHARGING);
    CHECK_EQ(sony_battery::ds4(0x07).percent, 75);
    CHECK_EQ(sony_battery::ds4(0x0A).percent, 100);
}

TEST(ds4_on_a_cable) {
    CHECK_EQ(sony_battery::ds4(0x13).percent, 35);
    CHECK(sony_battery::ds4(0x13).state == BatteryState::CHARGING);
    CHECK(sony_battery::ds4(0x1A).state == BatteryState::CHARGING);
    CHECK_EQ(sony_battery::ds4(0x1A).percent, 100);
    CHECK(sony_battery::ds4(0x1B).state == BatteryState::FULL);
    CHECK(sony_battery::ds4(0x1F).state == BatteryState::UNKNOWN);
}

TEST(dualsense_states) {
    CHECK_EQ(sony_battery::ds5(0x04).percent, 45);
    CHECK(sony_battery::ds5(0x04).state == BatteryState::DISCHARGING);
    CHECK(sony_battery::ds5(0x16).state == BatteryState::CHARGING);
    CHECK_EQ(sony_battery::ds5(0x16).percent, 65);
    CHECK(sony_battery::ds5(0x2A).state == BatteryState::FULL);
    CHECK(sony_battery::ds5(0xA3).state == BatteryState::NOT_CHARGING);
    CHECK_EQ(sony_battery::ds5(0x0A).percent, 100);
}

TEST_MAIN()
