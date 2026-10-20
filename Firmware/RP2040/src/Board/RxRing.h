#pragma once

/* Custom (OGX-Mini-improved): bytes a DMA channel writes into a power-of-two ring (its write
 * address wraps, channel_config_set_ring), read back in order by the main loop. The DMA keeps
 * receiving while the loop is busy or a flash write has the interrupts off; the UART's own FIFO
 * holds only 32 bytes (about 2.8 ms at 115200). */

#include <cstddef>
#include <cstdint>

template <unsigned SizeBits>
class RxRing
{
public:
    static constexpr size_t kSize = size_t{1} << SizeBits;
    static constexpr unsigned kSizeBits = SizeBits;

    uint8_t* buffer() { return buf_; }

    /* `produced`: bytes written since the start (wraps at 2^32). Calls out(byte) for each new
     * byte in order and returns how many were lost because the writer went more than a whole
     * ring ahead (the oldest ones). */
    template <typename Out>
    uint32_t drain(uint32_t produced, Out&& out)
    {
        uint32_t lost = 0;
        const uint32_t pending = produced - read_;
        if (pending > kSize)
        {
            lost = pending - static_cast<uint32_t>(kSize);
            read_ += lost;
        }
        while (read_ != produced)
        {
            out(buf_[read_ & (kSize - 1)]);
            ++read_;
        }
        return lost;
    }

private:
    alignas(kSize) uint8_t buf_[kSize]{};
    uint32_t read_ = 0;
};
