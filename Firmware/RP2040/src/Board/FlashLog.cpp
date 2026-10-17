#include "Board/FlashLog.h"

#include <cstddef>
#include <cstring>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/flash.h>

#include "Board/LogRing.h"
#include "Diagnostics/Diagnostics.h"
#include "UserSettings/DongleSettings.h"

#ifndef OGXM_FLASH_LOG_SECTORS
#if defined(PICO_RP2350)
#define OGXM_FLASH_LOG_SECTORS 128  // 512 KB of the Pico 2 / Pico 2 W's 4 MB
#else
#define OGXM_FLASH_LOG_SECTORS 32   // 128 KB of the Pico / Pico W's 2 MB
#endif
#endif

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
extern "C" char __flash_binary_end;
#endif

namespace flash_log {

namespace {

constexpr uint32_t kSectors = OGXM_FLASH_LOG_SECTORS;
constexpr uint32_t kPagesPerSector = FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE;
constexpr uint32_t kPages = kSectors * kPagesPerSector;
// Below NVSTool's sectors (UserSettings/NVSTool.h; BTstack's bank is inside them).
constexpr uint32_t kEnd = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE * NVS_SECTORS;
constexpr uint32_t kStart = kEnd - kSectors * FLASH_SECTOR_SIZE;
static_assert(kSectors >= 4, "the flash log needs a few sectors");
static_assert(sizeof(Page) == FLASH_PAGE_SIZE, "flash_log::Page is one flash page");

constexpr uint32_t kReservePages = 2 * kPagesPerSector;  // erased while quiet, for playing
constexpr uint32_t kPlayIntervalMs = 30000;
constexpr uint32_t kQuietFlushMs = 5000;  // a partial page once the log stops growing this long
constexpr uint32_t kTickMs = 100;
constexpr uint32_t kRestMagic = 0x464C4F47u;
constexpr uint32_t kSafeExecuteTimeoutMs = 100;

/* What survives a warm reset: where the RAM ring was saved up to, and its generation. */
struct Rest {
    uint32_t magic;
    uint32_t saved_pos;
    uint32_t ring_gen;
    uint32_t check;
};
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
__attribute__((section(".uninitialized_data"))) Rest s_rest;
#else
Rest s_rest;
#endif

struct State {
    bool ready = false;
    bool available = true;
    uint32_t head = 0;          // next page to write
    uint32_t erased_ahead = 0;  // erased pages from head on
    uint32_t pages = 0;         // valid pages kept
    uint32_t oldest_seq = 0, newest_seq = 0, next_seq = 1;
    uint16_t boot = 0;
    uint32_t dropped = 0;
    uint32_t last_write_ms = 0, last_growth_ms = 0, last_total = 0, next_tick_ms = 0;
    uint32_t max_gen = 0;
};
State s;

uint32_t rest_check(const Rest& r) { return ~(r.magic ^ r.saved_pos ^ (r.ring_gen << 1)); }

void save_rest()
{
    s_rest.magic = kRestMagic;
    s_rest.check = rest_check(s_rest);
}

const Page* page_at(uint32_t index)
{
    return reinterpret_cast<const Page*>(XIP_BASE + kStart + index * FLASH_PAGE_SIZE);
}

uint32_t crc32(const uint8_t* data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool erased(const Page* p)
{
    const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
    for (size_t i = 0; i < sizeof(Page); ++i)
        if (b[i] != 0xFF) return false;
    return true;
}

bool valid(const Page* p)
{
    return p->magic == kPageMagic && p->len <= kPageText &&
           p->crc == crc32(reinterpret_cast<const uint8_t*>(p), offsetof(Page, crc));
}

/* Erase / program with nothing running from flash (as NVSTool does): the other core parked by
 * the lockout, or interrupts off when it is not running yet. A core that does not answer: skip. */
template <typename Fn>
bool flash_op(Fn fn)
{
    const int rc = flash_safe_execute([](void* p) { (*static_cast<Fn*>(p))(); }, &fn, kSafeExecuteTimeoutMs);
    if (rc == PICO_OK)
        return true;
    if (rc != PICO_ERROR_NOT_PERMITTED)
        return false;
    const uint32_t irq = save_and_disable_interrupts();
    fn();
    restore_interrupts(irq);
    return true;
}

uint32_t count_erased_from(uint32_t index)
{
    uint32_t n = 0;
    while (n < kPages && erased(page_at((index + n) % kPages)))
        ++n;
    return n;
}

void scan(uint32_t& max_gen)
{
    s.pages = 0;
    s.oldest_seq = s.newest_seq = 0;
    uint32_t newest_index = kPages;
    uint16_t max_boot = 0;
    max_gen = 0;
    for (uint32_t i = 0; i < kPages; ++i) {
        const Page* p = page_at(i);
        if (p->magic != kPageMagic || !valid(p))
            continue;
        ++s.pages;
        if (newest_index == kPages || p->seq > s.newest_seq) {
            s.newest_seq = p->seq;
            newest_index = i;
        }
        if (s.oldest_seq == 0 || p->seq < s.oldest_seq) s.oldest_seq = p->seq;
        if (p->boot > max_boot) max_boot = p->boot;
        if (p->ring_gen > max_gen) max_gen = p->ring_gen;
    }
    s.head = newest_index == kPages ? 0 : (newest_index + 1) % kPages;
    s.next_seq = s.newest_seq + 1;
    s.boot = static_cast<uint16_t>(max_boot + 1);
    s.erased_ahead = count_erased_from(s.head);
}

/* Makes room ahead: erases the sector after the erased run (the oldest data). */
bool erase_next_sector()
{
    if (s.erased_ahead >= kPages)
        return false;
    const uint32_t first = (s.head + s.erased_ahead) % kPages;
    if (first / kPagesPerSector == s.head / kPagesPerSector && s.head % kPagesPerSector != 0) {
        // The page after head holds something other than an erased page (a page cut by a
        // power loss): leave the rest of this sector and go on in the next one.
        s.head = (s.head / kPagesPerSector + 1) % kSectors * kPagesPerSector;
        s.erased_ahead = count_erased_from(s.head);
        return true;  // the caller looks again at what is ready
    }
    const uint32_t sector = first / kPagesPerSector;
    uint32_t lost = 0;
    for (uint32_t i = 0; i < kPagesPerSector; ++i)
        if (valid(page_at(sector * kPagesPerSector + i))) ++lost;
    const uint32_t offset = kStart + sector * FLASH_SECTOR_SIZE;
    if (!flash_op([offset] { flash_range_erase(offset, FLASH_SECTOR_SIZE); }))
        return false;
    s.pages -= lost < s.pages ? lost : s.pages;
    s.erased_ahead = count_erased_from(s.head);
    // The oldest page left: the first valid one after the erased run.
    s.oldest_seq = 0;
    for (uint32_t i = 0; i < kPages - s.erased_ahead; ++i) {
        const Page* p = page_at((s.head + s.erased_ahead + i) % kPages);
        if (valid(p)) { s.oldest_seq = p->seq; break; }
    }
    return true;
}

bool write_page(uint32_t now_ms, uint32_t ring_pos, const uint8_t* text, size_t len)
{
    if (s.erased_ahead == 0)
        return false;
    Page page;
    std::memset(&page, 0, sizeof(page));
    page.magic = kPageMagic;
    page.seq = s.next_seq;
    page.boot = s.boot;
    page.len = static_cast<uint16_t>(len);
    page.ring_gen = s_rest.ring_gen;
    page.ring_pos = ring_pos;
    page.uptime_s = now_ms / 1000;
    std::memcpy(page.text, text, len);
    page.crc = crc32(reinterpret_cast<const uint8_t*>(&page), offsetof(Page, crc));
    const uint32_t offset = kStart + s.head * FLASH_PAGE_SIZE;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&page);
    if (!flash_op([offset, data] { flash_range_program(offset, data, FLASH_PAGE_SIZE); }))
        return false;
    const bool ok = valid(page_at(s.head));
    s.head = (s.head + 1) % kPages;
    --s.erased_ahead;
    s.last_write_ms = now_ms;
    if (!ok)
        return false;  // not written as asked (worn page?): skipped, the next one is tried next time
    if (s.oldest_seq == 0) s.oldest_seq = page.seq;
    s.newest_seq = page.seq;
    ++s.next_seq;
    if (s.pages < kPages) ++s.pages;
    return true;
}

/* The saved position against the ring: ahead of it = the ring started over (a firmware with
 * another ring layout after a warm reboot): a new generation; behind its oldest byte = the ring
 * overwrote bytes before they were saved. Returns the bytes waiting. */
uint32_t sync_ring()
{
    uint32_t start = 0, total = 0;
    log_ring_read_since(s_rest.saved_pos, &start, &total, nullptr, 0);
    if (static_cast<int32_t>(s_rest.saved_pos - total) > 0) {
        s_rest.ring_gen = (s_rest.ring_gen > s.max_gen ? s_rest.ring_gen : s.max_gen) + 1;
        s.max_gen = s_rest.ring_gen;
        s_rest.saved_pos = start;
        save_rest();
    } else if (start != s_rest.saved_pos) {
        s.dropped += start - s_rest.saved_pos;
        s_rest.saved_pos = start;
        save_rest();
    }
    return total - start;
}

/* Writes the next page of what the ring holds past the saved position. */
bool flush_one(uint32_t now_ms)
{
    uint8_t text[kPageText];
    uint32_t start = 0, total = 0;
    sync_ring();
    const size_t n = log_ring_read_since(s_rest.saved_pos, &start, &total, text, sizeof(text));
    if (n == 0 || !write_page(now_ms, start, text, n))
        return false;
    s_rest.saved_pos = start + static_cast<uint32_t>(n);
    save_rest();
    return true;
}

bool enabled() { return dongle_settings::get().keep_log_in_flash != 0; }

} // namespace

void init(uint32_t now_ms)
{
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    s.available = reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE <= kStart;
#endif
    scan(s.max_gen);
    if (s_rest.magic != kRestMagic || s_rest.check != rest_check(s_rest)) {
        // Power-on: the RAM ring started over, in a new generation.
        s_rest.saved_pos = 0;
        s_rest.ring_gen = s.max_gen + 1;
        s.max_gen = s_rest.ring_gen;
        save_rest();
    }
    s.ready = true;
    s.last_growth_ms = now_ms;
    if (!s.available || !enabled())
        return;
    // A warm reset: what the previous boot left in RAM goes to flash now (nothing is played yet).
    while (sync_ring() > 0) {
        if (s.erased_ahead == 0 && !erase_next_sector())
            break;
        if (!flush_one(now_ms))
            break;
    }
    while (s.erased_ahead < kReservePages && erase_next_sector()) {
    }
}

void tick(uint32_t now_ms, bool allow)
{
    if (!s.ready || !s.available || !allow || static_cast<int32_t>(now_ms - s.next_tick_ms) < 0)
        return;
    s.next_tick_ms = now_ms + kTickMs;
    if (!enabled())
        return;
    const uint32_t waiting = sync_ring();
    uint32_t start = 0, total = 0;
    log_ring_read_since(s_rest.saved_pos, &start, &total, nullptr, 0);
    if (total != s.last_total) {
        s.last_total = total;
        s.last_growth_ms = now_ms;
    }
    if (diag::quiet(now_ms)) {
        if (s.erased_ahead < kReservePages) {
            erase_next_sector();  // one per tick: about 45 ms, nothing is being played
            return;
        }
        if (waiting >= kPageText || (waiting > 0 && now_ms - s.last_growth_ms >= kQuietFlushMs))
            flush_one(now_ms);
    } else if (waiting >= kPageText && now_ms - s.last_write_ms >= kPlayIntervalMs) {
        flush_one(now_ms);  // one page, about a millisecond
    }
}

bool read(uint32_t from_seq, Page& out)
{
    if (!s.ready || s.pages == 0 || from_seq > s.newest_seq)
        return false;
    if (from_seq < s.oldest_seq) from_seq = s.oldest_seq;
    // Pages are in sequence order from the oldest on: try where from_seq should be first.
    const uint32_t oldest_index = (s.head + s.erased_ahead) % kPages;
    const uint32_t guess = (oldest_index + (from_seq - s.oldest_seq)) % kPages;
    const Page* p = page_at(guess);
    if (valid(p) && p->seq == from_seq) {
        std::memcpy(&out, p, sizeof(out));
        return true;
    }
    const Page* best = nullptr;  // gaps (skipped pages): the smallest seq >= from_seq
    for (uint32_t i = 0; i < kPages; ++i) {
        const Page* q = page_at(i);
        if (q->magic == kPageMagic && q->seq >= from_seq && (!best || q->seq < best->seq) && valid(q))
            best = q;
    }
    if (!best)
        return false;
    std::memcpy(&out, best, sizeof(out));
    return true;
}

Stats stats()
{
    Stats st{};
    st.enabled = enabled();
    st.available = s.available;
    st.capacity = kPages;
    st.pages = s.pages;
    st.oldest_seq = s.oldest_seq;
    st.newest_seq = s.newest_seq;
    st.ready = s.erased_ahead;
    st.dropped = s.dropped;
    st.ring_gen = s_rest.ring_gen;
    st.boot = s.boot;
    return st;
}

void reset_for_test()
{
    s = State{};
}

void power_on_for_test()
{
    s = State{};
    std::memset(&s_rest, 0, sizeof(s_rest));
}

} // namespace flash_log
