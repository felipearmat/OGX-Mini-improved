// Switch Pro input report button bits, checked against the layout Linux hid-nintendo
// (and the console) expect. hid-nintendo reads the three button bytes as one 24-bit
// field: byte 0 = right side, byte 1 = shared, byte 2 = left side.
#include "Descriptors/SwitchPro.h"
#include "test.h"

namespace {

// hid-nintendo's JC_BTN_* bit numbers within the 24-bit field.
constexpr int kRStick = 10;
constexpr int kLStick = 11;
constexpr int kMinus = 8;
constexpr int kPlus = 9;
constexpr int kHome = 12;
constexpr int kCapture = 13;

// Bit of the shared button byte (byte 1 of the field) as a 24-bit field position.
int shared_bit(uint8_t mask) {
    for (int i = 0; i < 8; ++i)
        if (mask == (1U << i)) return 8 + i;
    return -1;
}

}  // namespace

TEST(stick_clicks_are_not_swapped) {
    CHECK_EQ(shared_bit(SwitchPro::Buttons1::R3), kRStick);
    CHECK_EQ(shared_bit(SwitchPro::Buttons1::L3), kLStick);
    CHECK_EQ(shared_bit(SwitchPro::Btn::R3), kRStick);
    CHECK_EQ(shared_bit(SwitchPro::Btn::L3), kLStick);
}

TEST(other_shared_buttons) {
    CHECK_EQ(shared_bit(SwitchPro::Buttons1::MINUS), kMinus);
    CHECK_EQ(shared_bit(SwitchPro::Buttons1::PLUS), kPlus);
    CHECK_EQ(shared_bit(SwitchPro::Buttons1::HOME), kHome);
    CHECK_EQ(shared_bit(SwitchPro::Buttons1::CAPTURE), kCapture);
}

TEST_MAIN()
