#ifndef _OGXM_FLASH_LOG_H_
#define _OGXM_FLASH_LOG_H_

#include <cstddef>
#include <cstdint>

/* Custom: the kept log (Board/LogRing.h) also in flash, so it outlives unplugging the adapter
 * (dongle option "keep the log in flash", OGXM_FLASH_LOG; on in our own builds).
 *
 * A circular region just below the settings (NVSTool, with BTstack's bank inside it): 512 KB on
 * the RP2350, 128 KB on the RP2040. Pages of 256 bytes, each with a sequence number, the boot
 * and ring generation it was written in, the ring position of its text and a CRC, so the newest
 * page is found by scanning (no index sector to wear) and a page cut by a power loss is ignored.
 *
 * When it writes, so a flash write never gets in the way of playing (it stops XIP and parks the
 * other core):
 *  - at boot: what a warm reset (crash, hang, mode change, reboot) left in RAM, and erasing the
 *    sectors kept ready;
 *  - quiet (diag::quiet: no controller reporting, or the host's USB suspended): full pages, a
 *    partial one once the log stops growing for a few seconds, and sector erases (about 45 ms)
 *    to keep two sectors ready;
 *  - playing: at most one full page every 30 s (about a millisecond), never an erase; with no
 *    page ready it waits, and what the RAM ring dropped meanwhile is counted.
 * Callers pass allow = false where timing is tight (Wii, the PS1 / PS2 device poll). */
namespace flash_log {

    constexpr uint32_t kPageMagic = 0x4C58474Fu;  // "OGXL"
    constexpr size_t kPageText = 228;

#pragma pack(push, 1)
    struct Page {
        uint32_t magic;
        uint32_t seq;        // 1, 2, 3... across boots
        uint16_t boot;       // flash log boots (every boot, warm or not)
        uint16_t len;        // text bytes used
        uint32_t ring_gen;   // the RAM ring's generation (a new one after a power-on)
        uint32_t ring_pos;   // LogRing position of text[0] in that generation
        uint32_t uptime_s;   // when it was written
        uint8_t text[kPageText];
        uint32_t crc;        // CRC-32 of everything before it
    };
#pragma pack(pop)
    static_assert(sizeof(Page) == 256, "flash_log::Page is one flash page");

    // At boot, on Core0, once the settings are loaded: finds the newest page and saves what a
    // warm reset left in RAM.
    void init(uint32_t now_ms);
    // From Core0's main loop. allow = false where a flash write must not happen now.
    void tick(uint32_t now_ms, bool allow);

    // The oldest page with seq >= from_seq; false when there is none.
    bool read(uint32_t from_seq, Page& out);

    struct Stats {
        bool enabled;        // the dongle option
        bool available;      // the region fits after the firmware
        uint32_t capacity;   // pages
        uint32_t pages;      // valid pages kept
        uint32_t oldest_seq, newest_seq;
        uint32_t ready;      // erased pages ahead
        uint32_t dropped;    // log bytes the RAM ring dropped before they reached flash
        uint32_t ring_gen;   // the running ring generation
        uint16_t boot;
    };
    Stats stats();

    // Host tests: forget the state read at init (the flash itself is kept), as a warm reset does;
    // power_on_for_test() also forgets what survives a warm reset.
    void reset_for_test();
    void power_on_for_test();

} // namespace flash_log

#endif // _OGXM_FLASH_LOG_H_
