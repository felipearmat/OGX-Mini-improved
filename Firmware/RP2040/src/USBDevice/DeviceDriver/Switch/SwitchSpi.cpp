#include "USBDevice/DeviceDriver/Switch/SwitchSpi.h"

#include <cstring>

#include "USBDevice/DeviceDriver/Switch/SwitchImu.h"

namespace switch_spi {

namespace {

constexpr uint32_t kBase = 0x6000;
constexpr size_t kSize = 0xAA;  // 0x6000-0x60A9

/* Factory stick calibration, 12-bit values in 3 bytes per pair (x, y). Left (0x603D): max above
 * centre, centre, min below centre. Right (0x6046): centre, min below, max above. The output's
 * centre and a range it reaches (SwitchSpi.h). */
void pack(uint8_t* b, int32_t x, int32_t y)
{
    b[0] = static_cast<uint8_t>(x & 0xFF);
    b[1] = static_cast<uint8_t>(((x >> 8) & 0x0F) | ((y & 0x0F) << 4));
    b[2] = static_cast<uint8_t>((y >> 4) & 0xFF);
}

void stick_cal(uint8_t* left, uint8_t* right)
{
    pack(left + 0, kStickCalRange, kStickCalRange);
    pack(left + 3, kStickCenter, kStickCenter);
    pack(left + 6, kStickCalRange, kStickCalRange);
    pack(right + 0, kStickCenter, kStickCenter);
    pack(right + 3, kStickCalRange, kStickCalRange);
    pack(right + 6, kStickCalRange, kStickCalRange);
}
/* Stick parameters (dead zone, range ratio...), left at 0x6086 and right at 0x6098. */
constexpr uint8_t kStickParams[18] = {0x0F, 0x30, 0x61, 0x96, 0x30, 0xF3, 0xD4, 0x14, 0x54,
                                      0x41, 0x15, 0x54, 0xC7, 0x79, 0x9C, 0x33, 0x36, 0x63};

struct Image {
    uint8_t bytes[kSize];
    Image()
    {
        std::memset(bytes, 0xFF, sizeof(bytes));  // serial number (0x6000) and unused bytes
        std::memcpy(&bytes[0x20], switch_imu::kFactoryCalibration, 24);  // IMU (0x6020)
        stick_cal(&bytes[0x3D], &bytes[0x46]);
        std::memset(&bytes[0x50], 0x32, 3);  // body colour (0x6050); buttons and grips stay 0xFF
        const uint8_t six_axis_h[6] = {0x50, 0xFD, 0x00, 0x00, 0xC6, 0x0F};  // 0x6080
        std::memcpy(&bytes[0x80], six_axis_h, 6);
        std::memcpy(&bytes[0x86], kStickParams, 18);
        std::memcpy(&bytes[0x98], kStickParams, 18);
    }
};

const Image& image()
{
    static const Image img;
    return img;
}

} // namespace

void read(uint32_t addr, uint8_t len, uint8_t* out)
{
    if (len > kMaxRead)
        len = kMaxRead;
    for (uint8_t i = 0; i < len; ++i) {
        const uint32_t a = addr + i;
        out[i] = (a >= kBase && a < kBase + kSize) ? image().bytes[a - kBase] : 0xFF;
    }
}

} // namespace switch_spi
