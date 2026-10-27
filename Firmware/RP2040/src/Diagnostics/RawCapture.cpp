#include <atomic>
#include <cstring>

#include "Diagnostics/RawCapture.h"

namespace raw_capture {

namespace {

/* One producer (the BT core), one consumer (the USB core): a byte ring of whole records. */
constexpr size_t kRingSize = 16384;  // power of two
uint8_t s_ring[kRingSize];
std::atomic<uint32_t> s_head{0};  // written by the producer: end of the last whole record
std::atomic<uint32_t> s_tail{0};  // written by the consumer
std::atomic<bool> s_enabled{false};
uint16_t s_dropped{0};            // producer only

void put(uint32_t at, const uint8_t* src, size_t len)
{
    for (size_t i = 0; i < len; ++i)
        s_ring[(at + i) & (kRingSize - 1)] = src[i];
}

void get(uint32_t at, uint8_t* dst, size_t len)
{
    for (size_t i = 0; i < len; ++i)
        dst[i] = s_ring[(at + i) & (kRingSize - 1)];
}

} // namespace

void set_enabled(bool on)
{
    if (on && !s_enabled.load())
        s_tail.store(s_head.load());  // a new capture starts empty
    s_enabled.store(on);
}

bool enabled()
{
    return s_enabled.load();
}

void on_input_report(uint8_t slot, uint8_t controller_type, uint16_t vid, uint16_t pid, uint32_t now_ms,
                     const uint8_t* report, size_t len)
{
    if (!s_enabled.load(std::memory_order_relaxed) || report == nullptr || len == 0)
        return;
    if (len > kMaxReport)
        len = kMaxReport;
    const uint32_t head = s_head.load(std::memory_order_relaxed);
    const uint32_t tail = s_tail.load(std::memory_order_acquire);
    const size_t size = kHeader + len;
    if (kRingSize - (head - tail) < size) {
        if (s_dropped < 0xFFFF)
            s_dropped++;
        return;
    }
    uint8_t h[kHeader];
    h[0] = kVersion;
    h[1] = slot;
    h[2] = controller_type;
    h[3] = static_cast<uint8_t>(vid);
    h[4] = static_cast<uint8_t>(vid >> 8);
    h[5] = static_cast<uint8_t>(pid);
    h[6] = static_cast<uint8_t>(pid >> 8);
    for (int i = 0; i < 4; ++i)
        h[7 + i] = static_cast<uint8_t>(now_ms >> (8 * i));
    h[11] = static_cast<uint8_t>(s_dropped);
    h[12] = static_cast<uint8_t>(s_dropped >> 8);
    h[13] = static_cast<uint8_t>(len);
    put(head, h, kHeader);
    put(head + kHeader, report, len);
    s_dropped = 0;
    s_head.store(head + static_cast<uint32_t>(size), std::memory_order_release);
}

size_t take(uint8_t* out)
{
    const uint32_t tail = s_tail.load(std::memory_order_relaxed);
    const uint32_t head = s_head.load(std::memory_order_acquire);
    if (head == tail)
        return 0;
    get(tail, out, kHeader);
    const size_t size = kHeader + out[13];
    get(tail + kHeader, out + kHeader, out[13]);
    s_tail.store(tail + static_cast<uint32_t>(size), std::memory_order_release);
    return size;
}

} // namespace raw_capture
