#include "Board/LogRing.h"

#include "Board/DebugProbe.h"

#include <cstring>
#include <hardware/sync.h>
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#if defined(PICO_RP2350)
#include <hardware/structs/powman.h>
#else
#include <hardware/structs/vreg_and_chip_reset.h>
#endif
#endif

namespace {

#if defined(PICO_RP2350)
constexpr size_t kSize = 32 * 1024;
#else
constexpr size_t kSize = 8192;
#endif
constexpr uint32_t kMagic = 0x10C0FFF1u;  // a new layout: an older firmware's ring starts over

struct Ring
{
    uint32_t magic;
    uint32_t head;   // next write position
    uint32_t used;
    uint32_t total;  // bytes ever written (wraps), the position GET_LOG counts in
    uint8_t line_start;  // the next byte starts a line
    char line_source[8];  // the source of the open line (another source closes it first)
    uint8_t data[kSize];
};
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
/* .uninitialized_data: not cleared by the C runtime, so it survives a warm reboot. */
__attribute__((section(".uninitialized_data"))) Ring s_ring;
#else
Ring s_ring;  // host builds (virtual adapter, tests)
#endif

/* Both cores write (diagnostics events come from the Bluetooth core too): a spin lock, which also
 * masks this core's interrupts. PICO_SPINLOCK_ID_OS1 is reserved for system code and unused here. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
struct Guard
{
    spin_lock_t* const lock = spin_lock_instance(PICO_SPINLOCK_ID_OS1);
    const uint32_t irq = spin_lock_blocking(lock);
    ~Guard() { spin_unlock(lock, irq); }
};
#else
struct Guard
{
    const uint32_t irq = save_and_disable_interrupts();
    ~Guard() { restore_interrupts(irq); }
};
#endif

/* Custom fix: the power went away. The ring's RAM is not cleared at boot, so it survives a warm
 * reboot (watchdog, RUN pin) as wanted, but after a power cut it can survive too, or come back half
 * decayed, its magic still matching (2026-10-22: a 9 s unplug kept it): dropped on a power-on (or,
 * on the RP2350, brown-out) reset. */
bool power_was_lost()
{
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#if defined(PICO_RP2350)
    return (powman_hw->chip_reset & (POWMAN_CHIP_RESET_HAD_POR_BITS | POWMAN_CHIP_RESET_HAD_BOR_BITS)) != 0;
#else
    return (vreg_and_chip_reset_hw->chip_reset & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS) != 0;
#endif
#else
    return false;
#endif
}

void ensure_valid()
{
    static bool s_boot_checked = false;
    if (!s_boot_checked) {
        s_boot_checked = true;
        if (power_was_lost())
            s_ring.magic = 0;  // starts over below
    }
    if (s_ring.magic != kMagic || s_ring.head >= kSize || s_ring.used > kSize) {
        s_ring.magic = kMagic;
        s_ring.head = 0;
        s_ring.used = 0;
        s_ring.total = 0;
        s_ring.line_start = 1;
        s_ring.line_source[0] = '\0';
    }
}

void put(uint8_t byte)
{
    s_ring.data[s_ring.head] = byte;
    s_ring.head = (s_ring.head + 1) % kSize;
    if (s_ring.used < kSize)
        ++s_ring.used;
    ++s_ring.total;
}

void append(const char* source, const char* text, size_t len)
{
    Guard guard;
    ensure_valid();
    /* A line another source left open (the ESP32's output arrives in pieces) ends here; its rest
     * starts a line of its own, with its prefix again. */
    if (!s_ring.line_start && std::strncmp(s_ring.line_source, source, sizeof(s_ring.line_source)) != 0) {
        put('\n');
        s_ring.line_start = 1;
    }
    std::strncpy(s_ring.line_source, source, sizeof(s_ring.line_source) - 1);
    s_ring.line_source[sizeof(s_ring.line_source) - 1] = '\0';
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
}

} // namespace

extern "C" void log_ring_append(const char* source, const char* text, size_t len)
{
    append(source, text, len);
    probe_text(source, text, len);  // the probe point too (Board/DebugProbe.h), outside the lock
}

extern "C" size_t log_ring_size(void)
{
    Guard guard;
    ensure_valid();
    const size_t used = s_ring.used;
    return used;
}

extern "C" size_t log_ring_read(size_t offset, uint8_t* out, size_t len)
{
    Guard guard;
    ensure_valid();
    size_t count = 0;
    if (offset < s_ring.used) {
        const size_t oldest = (s_ring.head + kSize - s_ring.used) % kSize;
        count = s_ring.used - offset < len ? s_ring.used - offset : len;
        for (size_t i = 0; i < count; ++i)
            out[i] = s_ring.data[(oldest + offset + i) % kSize];
    }
    return count;
}

extern "C" void log_ring_reset_for_test(void)
{
    Guard guard;
    s_ring.magic = 0;
}

extern "C" size_t log_ring_read_since(uint32_t since, uint32_t* start, uint32_t* total, uint8_t* out, size_t len)
{
    Guard guard;
    ensure_valid();
    /* Bytes from `since` on; a position already dropped (or not reached: the ring started over)
     * reads from the oldest kept byte. */
    uint32_t behind = s_ring.total - since;
    if (behind > s_ring.used)
        behind = s_ring.used;
    const size_t count = behind < len ? behind : len;
    const size_t oldest = (s_ring.head + kSize - s_ring.used) % kSize;
    const size_t offset = s_ring.used - behind;
    for (size_t i = 0; i < count; ++i)
        out[i] = s_ring.data[(oldest + offset + i) % kSize];
    *start = s_ring.total - behind;
    *total = s_ring.total;
    return count;
}
