#include "Board/LogRing.h"

#include <cstring>
#include <hardware/sync.h>

namespace {

constexpr size_t kSize = 8192;
constexpr uint32_t kMagic = 0x10C0FFEEu;

/* .uninitialized_data: not cleared by the C runtime, so it survives a warm reboot. */
struct Ring
{
    uint32_t magic;
    uint32_t head;  // next write position
    uint32_t used;
    uint8_t line_start;  // the next byte starts a line
    uint8_t data[kSize];
};
__attribute__((section(".uninitialized_data"))) Ring s_ring;

void ensure_valid()
{
    if (s_ring.magic != kMagic || s_ring.head >= kSize || s_ring.used > kSize) {
        s_ring.magic = kMagic;
        s_ring.head = 0;
        s_ring.used = 0;
        s_ring.line_start = 1;
    }
}

void put(uint8_t byte)
{
    s_ring.data[s_ring.head] = byte;
    s_ring.head = (s_ring.head + 1) % kSize;
    if (s_ring.used < kSize)
        ++s_ring.used;
}

} // namespace

extern "C" void log_ring_append(const char* source, const char* text, size_t len)
{
    const uint32_t irq = save_and_disable_interrupts();
    ensure_valid();
    for (size_t i = 0; i < len; ++i) {
        const char c = text[i];
        if (c == '\r' || c == '\0')
            continue;
        if (s_ring.line_start) {
            put('[');
            for (const char* s = source; *s; ++s)
                put(static_cast<uint8_t>(*s));
            put(']');
            put(' ');
            s_ring.line_start = 0;
        }
        put(static_cast<uint8_t>(c));
        if (c == '\n')
            s_ring.line_start = 1;
    }
    restore_interrupts(irq);
}

extern "C" size_t log_ring_size(void)
{
    const uint32_t irq = save_and_disable_interrupts();
    ensure_valid();
    const size_t used = s_ring.used;
    restore_interrupts(irq);
    return used;
}

extern "C" size_t log_ring_read(size_t offset, uint8_t* out, size_t len)
{
    const uint32_t irq = save_and_disable_interrupts();
    ensure_valid();
    size_t count = 0;
    if (offset < s_ring.used) {
        const size_t oldest = (s_ring.head + kSize - s_ring.used) % kSize;
        count = s_ring.used - offset < len ? s_ring.used - offset : len;
        for (size_t i = 0; i < count; ++i)
            out[i] = s_ring.data[(oldest + offset + i) % kSize];
    }
    restore_interrupts(irq);
    return count;
}
