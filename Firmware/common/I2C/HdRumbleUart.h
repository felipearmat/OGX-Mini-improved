#ifndef _OGXM_HD_RUMBLE_UART_H_
#define _OGXM_HD_RUMBLE_UART_H_

#include <cstddef>
#include <cstdint>
#include <cstring>

/* Custom: Switch HD Rumble from the RP2040 to the hybrid's ESP32 over the UART wire that flashes the
 * ESP32 (RP2040 UART0 TX -> ESP32 RX0), free outside the bridge mode. On the I2C link the RP2040 is
 * the slave and waits to be asked, so each report waited for the ESP32's next read, sharing the bus
 * with the pad reports and the settings: 1.2 ms median and irregular, felt as a blurrier HD Rumble
 * than the Pico W's (2026-11-04). Here the RP2040 sends each report the moment the host's arrives.
 *
 * A frame: 0xA5 0x5A, the output index, a sequence number, the report's 8 bytes, then the XOR of
 * the four bytes before it and the 8 (kFrameLen bytes, about 1.1 ms at 115200 baud). Header only:
 * the RP2040 encodes, the ESP32 decodes a byte stream (a lost or corrupted byte costs that frame). */
namespace hd_rumble_uart {

    constexpr uint8_t kSync0 = 0xA5;
    constexpr uint8_t kSync1 = 0x5A;
    constexpr size_t kFrameLen = 13;

    inline uint8_t check(const uint8_t* frame)
    {
        uint8_t x = 0;
        for (size_t i = 0; i < kFrameLen - 1; ++i)
            x ^= frame[i];
        return x;
    }

    inline void encode(uint8_t index, uint8_t seq, const uint8_t blocks[8], uint8_t out[kFrameLen])
    {
        out[0] = kSync0;
        out[1] = kSync1;
        out[2] = index;
        out[3] = seq;
        std::memcpy(out + 4, blocks, 8);
        out[kFrameLen - 1] = check(out);
    }

    /* Byte by byte; true when a whole, checked frame is in index / seq / blocks. */
    class Decoder
    {
    public:
        bool feed(uint8_t b)
        {
            if (len_ == 0 && b != kSync0) return false;
            if (len_ == 1 && b != kSync1) {
                len_ = (b == kSync0) ? 1 : 0;
                return false;
            }
            buf_[len_++] = b;
            if (len_ < kFrameLen) return false;
            len_ = 0;
            if (check(buf_) != buf_[kFrameLen - 1]) {
                ++bad_;
                return false;
            }
            index = buf_[2];
            seq = buf_[3];
            std::memcpy(blocks, buf_ + 4, 8);
            return true;
        }
        uint32_t bad_frames() const { return bad_; }

        uint8_t index{0};
        uint8_t seq{0};
        uint8_t blocks[8]{};

    private:
        uint8_t buf_[kFrameLen]{};
        size_t len_{0};
        uint32_t bad_{0};
    };

} // namespace hd_rumble_uart

#endif // _OGXM_HD_RUMBLE_UART_H_
