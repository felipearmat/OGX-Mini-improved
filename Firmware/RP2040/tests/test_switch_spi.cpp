// The emulated Switch Pro Controller's SPI flash (USBDevice/DeviceDriver/Switch/SwitchSpi.h): a read
// at any address gets the factory block's bytes. Linux hid-nintendo reads the right stick's
// calibration on its own at 0x6046, which used to answer 0xFF: the right stick sat in one quadrant.
#include <cstring>

#include "USBDevice/DeviceDriver/Switch/SwitchImu.h"
#include "USBDevice/DeviceDriver/Switch/SwitchSpi.h"
#include "test.h"

namespace {

// Two 12-bit values packed in 3 bytes, as hosts unpack stick calibration.
void unpack(const uint8_t* b, int& x, int& y)
{
    x = b[0] | ((b[1] & 0x0F) << 8);
    y = (b[1] >> 4) | (b[2] << 4);
}

} // namespace

TEST(the_right_stick_calibration_reads_the_same_at_0x6046_as_inside_0x603d) {
    uint8_t both[18], right[9];
    switch_spi::read(0x603D, 18, both);
    switch_spi::read(0x6046, 9, right);
    CHECK(std::memcmp(both + 9, right, 9) == 0);
    int cx, cy;
    unpack(right, cx, cy);  // right stick: centre first
    CHECK(cx > 1800 && cx < 2300);
    CHECK(cy > 1800 && cy < 2300);
    int lx, ly;
    unpack(both + 3, lx, ly);  // left stick: centre second
    CHECK(lx > 1800 && lx < 2300);
    CHECK(ly > 1800 && ly < 2300);
}

TEST(the_declared_stick_calibration_matches_the_output) {
    // Centre = the output's centre, and a full stick (2047 +- 32767 / 22) reaches every range: the
    // copied Pro Controller values (right centre 2141, ranges to 1566) left the right stick off
    // centre and short of its travel once hosts read it at 0x6046.
    uint8_t left[9], right[9];
    switch_spi::read(0x603D, 9, left);
    switch_spi::read(0x6046, 9, right);
    const int full = 32767 / switch_spi::kStickGainInv;
    int x, y;
    unpack(left + 3, x, y);
    CHECK_EQ(x, switch_spi::kStickCenter);
    CHECK_EQ(y, switch_spi::kStickCenter);
    unpack(right + 0, x, y);
    CHECK_EQ(x, switch_spi::kStickCenter);
    CHECK_EQ(y, switch_spi::kStickCenter);
    const uint8_t* ranges[4] = {left + 0, left + 6, right + 3, right + 6};
    for (const uint8_t* r : ranges) {
        unpack(r, x, y);
        CHECK(x > 1200 && x <= full);
        CHECK(y > 1200 && y <= full);
    }
}

TEST(the_known_reads_keep_their_bytes) {
    uint8_t b[29];
    switch_spi::read(0x6000, 16, b);  // serial number: none
    for (int i = 0; i < 16; ++i) CHECK_EQ(b[i], 0xFF);
    switch_spi::read(0x6050, 13, b);  // body colour, then buttons / grips unset
    CHECK_EQ(b[0], 0x32);
    CHECK_EQ(b[2], 0x32);
    CHECK_EQ(b[3], 0xFF);
    switch_spi::read(0x6020, 24, b);  // IMU calibration
    CHECK(std::memcmp(b, switch_imu::kFactoryCalibration, 24) == 0);
    switch_spi::read(0x6080, 24, b);  // six-axis horizontal offsets, then the left stick parameters
    CHECK_EQ(b[0], 0x50);
    CHECK_EQ(b[6], 0x0F);
    switch_spi::read(0x603D, 25, b);  // a console's single read of both sticks and the colour
    CHECK_EQ(b[18], 0xFF);
    CHECK_EQ(b[19], 0x32);
}

TEST(user_calibration_and_unknown_addresses_read_ff_and_reads_are_capped) {
    uint8_t b[32];
    std::memset(b, 0, sizeof(b));
    switch_spi::read(0x8010, 24, b);  // no user calibration magic: hosts use the factory data
    for (int i = 0; i < 24; ++i) CHECK_EQ(b[i], 0xFF);
    std::memset(b, 0, sizeof(b));
    switch_spi::read(0x6098, 32, b);  // more than a reply holds: 29 bytes, the rest untouched
    CHECK_EQ(b[0], 0x0F);
    CHECK_EQ(b[28], 0xFF);
    CHECK_EQ(b[29], 0x00);
}

TEST_MAIN()
