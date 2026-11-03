#include "Board/DebugProbe.h"

#if defined(OGXM_PROBE)

#include <algorithm>
#include <cstring>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/uart.h"

#include "UserSettings/DongleSettings.h"

namespace {

#if defined(PICO_RP2350)
constexpr uint32_t kSize = 16 * 1024;  // about 1 s of every report of four pads at 1 kHz USB
#else
constexpr uint32_t kSize = 4 * 1024;
#endif
constexpr uint32_t kHeader = 8;
constexpr uint32_t kHelloEveryUs = 1000000;
constexpr uint kPin = OGXM_DEBUG_UART_TX_PIN;
/* UART TX pins: uart0 on GP0 / 12 / 16 / 28, uart1 on GP4 / 8 / 20 / 24 (CMake checks the pin). */
uart_inst_t* uart() { return ((kPin + 4) >> 3) & 1 ? uart1 : uart0; }

uint8_t s_data[kSize];
uint32_t s_head = 0;    // next byte written
uint32_t s_tail = 0;    // next byte sent
uint32_t s_used = 0;    // waiting and in flight
uint32_t s_flight = 0;  // bytes the DMA sends, from s_tail
uint32_t s_frames = 0, s_dropped = 0;
uint32_t s_hello_us = 0;
int s_dma = -1;
bool s_ready = false, s_failed = false, s_hello_sent = false;

/* Both cores record: a spin lock of its own (the kept log has OS1), which also masks this core's
 * interrupts for the few dozen instructions of a copy. */
struct Guard
{
    spin_lock_t* const lock = spin_lock_instance(PICO_SPINLOCK_ID_OS2);
    const uint32_t irq = spin_lock_blocking(lock);
    ~Guard() { spin_unlock(lock, irq); }
};

bool on()
{
    return dongle_settings::activity_log(dongle_settings::get());
}

/* The UART and the DMA channel, the first time the log is on. Under the lock. */
bool ready()
{
    if (s_ready || s_failed) return s_ready;
    s_dma = dma_claim_unused_channel(false);
    if (s_dma < 0) {
        s_failed = true;
        return false;
    }
    uart_init(uart(), OGXM_PROBE_BAUD);
    gpio_set_function(kPin, GPIO_FUNC_UART);
    dma_channel_config c = dma_channel_get_default_config(static_cast<uint>(s_dma));
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, uart_get_dreq_num(uart(), true));
    dma_channel_configure(static_cast<uint>(s_dma), &c, &uart_get_hw(uart())->dr, s_data, 0, false);
    s_ready = true;
    return true;
}

/* Frees what the DMA finished and starts it on what waits (up to the end of the ring). Under the
 * lock. */
void pump()
{
    if (s_flight) {
        if (dma_channel_is_busy(static_cast<uint>(s_dma))) return;
        s_tail = (s_tail + s_flight) % kSize;
        s_used -= s_flight;
        s_flight = 0;
    }
    if (!s_used) return;
    s_flight = std::min(s_used, kSize - s_tail);
    dma_channel_transfer_from_buffer_now(static_cast<uint>(s_dma), s_data + s_tail, s_flight);
}

void copy_in(const uint8_t* p, uint32_t len)
{
    const uint32_t first = std::min(len, kSize - s_head);
    std::memcpy(s_data + s_head, p, first);
    std::memcpy(s_data, p + first, len - first);
    s_head = (s_head + len) % kSize;
}

void frame(uint8_t type, const uint8_t* a, size_t a_len, const uint8_t* b, size_t b_len)
{
    const uint32_t len = static_cast<uint32_t>(std::min<size_t>(a_len + b_len, 255));
    b_len = len - std::min<size_t>(a_len, len);
    a_len = len - b_len;
    const uint32_t t = time_us_32();
    uint8_t h[kHeader] = {0xA5, type, static_cast<uint8_t>(len), 0,
                          static_cast<uint8_t>(t), static_cast<uint8_t>(t >> 8),
                          static_cast<uint8_t>(t >> 16), static_cast<uint8_t>(t >> 24)};
    uint32_t sum = 0;
    for (uint32_t i = 1; i < kHeader; ++i) sum += h[i];
    for (size_t i = 0; i < a_len; ++i) sum += a[i];
    for (size_t i = 0; i < b_len; ++i) sum += b[i];
    h[3] = static_cast<uint8_t>(0u - sum);

    Guard g;
    if (!ready()) return;
    if (kSize - s_used < kHeader + len) {
        ++s_dropped;
    } else {
        copy_in(h, kHeader);
        if (a_len) copy_in(a, static_cast<uint32_t>(a_len));
        if (b_len) copy_in(b, static_cast<uint32_t>(b_len));
        s_used += kHeader + len;
        ++s_frames;
    }
    pump();
}

void put_u32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

} // namespace

extern "C" void probe_record(uint8_t type, const void* payload, size_t len)
{
    if (!on()) return;
    frame(type, static_cast<const uint8_t*>(payload), len, nullptr, 0);
}

extern "C" void probe_text(const char* source, const char* text, size_t len)
{
    if (!on() || !len) return;
    const uint8_t letter = static_cast<uint8_t>(source && source[0] ? source[0] : '?');
    const auto* p = reinterpret_cast<const uint8_t*>(text);
    while (len) {
        const size_t n = std::min<size_t>(len, 254);
        frame(PROBE_TEXT, &letter, 1, p, n);
        p += n;
        len -= n;
    }
}

extern "C" void probe_tick(void)
{
    if (!on()) return;
    const uint32_t now = time_us_32();
    if (!s_hello_sent || now - s_hello_us >= kHelloEveryUs) {  // the first one at once: a reader opens its file on it
        s_hello_sent = true;
        s_hello_us = now;
        static const char kId[] = OGXM_VERSION " " OGXM_BUILD_ID " " OGXM_BOARD_NAME;
        uint8_t counts[12];
        put_u32(counts, s_frames);
        put_u32(counts + 4, s_dropped);
        put_u32(counts + 8, kSize);
        frame(PROBE_HELLO, counts, sizeof(counts), reinterpret_cast<const uint8_t*>(kId), sizeof(kId) - 1);
        return;  // frame() pumped
    }
    Guard g;
    if (ready()) pump();
}

#endif // OGXM_PROBE
