// The kept log in flash (Board/FlashLog.h), on the RAM flash of tests/mocks: what is written when,
// what survives a warm reset and a power cut, and what is never done while playing.
#include <cstring>
#include <string>

#include "Board/FlashLog.h"
#include "Board/LogRing.h"
#include "Diagnostics/Diagnostics.h"
#include "UserSettings/DongleSettings.h"
#include "hardware/flash.h"
#include "test.h"

uint64_t mock_time_us = 0;  // pico/time.h (tests/mocks)

namespace {

constexpr uint32_t kPage = 256;
constexpr uint32_t kRegionPages = OGXM_FLASH_LOG_SECTORS * (FLASH_SECTOR_SIZE / kPage);
const uint8_t kAddr[6] = {1, 2, 3, 4, 5, 6};

void enable(bool on)
{
    dongle_settings::Settings s = dongle_settings::defaults();
    s.keep_log_in_flash = on ? 1 : 0;
    dongle_settings::set(s);
}

// A new board: erased flash, nothing kept in RAM.
void fresh()
{
    std::memset(mock_flash::memory(), 0xFF, PICO_FLASH_SIZE_BYTES);
    mock_flash::stats() = {};
    log_ring_reset_for_test();
    diag::reset_for_tests();
    diag::init(diag::BoardInfo{"test", "PI_PICO2W", "RP2350", 150, "Release", "XINPUT", "power-on", 1});
    flash_log::power_on_for_test();
    enable(true);
}

void line(const std::string& text) { log_ring_append("rp", (text + "\n").data(), text.size() + 1); }

// Ticks every 100 ms from `from` for `ms`.
uint32_t run(uint32_t from, uint32_t ms, bool allow = true)
{
    for (uint32_t t = from; t <= from + ms; t += 100)
        flash_log::tick(t, allow);
    return from + ms;
}

std::string all_text()
{
    std::string text;
    flash_log::Page p;
    uint32_t seq = 1;
    while (flash_log::read(seq, p)) {
        text.append(reinterpret_cast<const char*>(p.text), p.len);
        seq = p.seq + 1;
    }
    return text;
}

void playing(uint32_t now_ms)
{
    diag::slot_connected(0, 0, "DS4", 0x054c, 0x09cc, 3, false, 0x40, kAddr);
    diag::slot_report(0, now_ms);
}

} // namespace

TEST(quiet_the_log_reaches_flash_and_reads_back_in_order) {
    fresh();
    flash_log::init(0);
    line("hello from boot");
    line("slot 0 ready");
    run(0, 6000);  // no controller: quiet; the log stopped growing for 5 s
    const std::string text = all_text();
    CHECK(text.find("[rp] hello from boot\n[rp] slot 0 ready\n") != std::string::npos);
    const flash_log::Stats st = flash_log::stats();
    CHECK(st.pages >= 1);
    CHECK(st.ready >= 2 * (FLASH_SECTOR_SIZE / kPage));  // two sectors kept ready
}

TEST(a_warm_reset_saves_what_the_ram_kept) {
    fresh();
    flash_log::init(0);
    run(0, 1000);
    line("just before the crash");
    flash_log::reset_for_test();  // a warm reset: RAM (ring and saved position) kept
    flash_log::init(0);           // saved at boot
    CHECK(all_text().find("just before the crash") != std::string::npos);
    const uint32_t pages = flash_log::stats().pages;
    flash_log::reset_for_test();
    flash_log::init(0);
    CHECK_EQ(flash_log::stats().pages, pages);  // nothing saved twice
}

TEST(nothing_is_written_while_disabled_or_not_allowed) {
    fresh();
    enable(false);
    flash_log::init(0);
    line("not kept");
    run(0, 10000);
    CHECK_EQ(flash_log::stats().pages, 0u);
    enable(true);
    run(10000, 10000, false);  // allow = false (Wii, the PS1 / PS2 poll)
    CHECK_EQ(flash_log::stats().pages, 0u);
    run(20000, 6000);
    CHECK_EQ(flash_log::stats().pages, 1u);
}

TEST(playing_writes_full_pages_sparingly_and_never_erases) {
    fresh();
    flash_log::init(0);
    run(0, 1000);  // quiet: the reserve is erased
    playing(1000);
    const int erases = mock_flash::stats().erases;
    const uint32_t before = flash_log::stats().pages;
    for (int i = 0; i < 40; ++i)
        line("controller input event " + std::to_string(i) + " with some text to fill pages");
    uint32_t t = 1000;
    for (; t < 60000; t += 100) {
        diag::slot_report(0, t);
        flash_log::tick(t, true);
    }
    const uint32_t written = flash_log::stats().pages - before;
    CHECK(written >= 1 && written <= 2);  // one page per 30 s
    CHECK_EQ(mock_flash::stats().erases, erases);
}

TEST(a_page_cut_by_a_power_loss_is_ignored) {
    fresh();
    flash_log::init(0);
    line("first page");
    run(0, 6000);
    line("second page");
    run(6000, 6000);
    CHECK_EQ(flash_log::stats().pages, 2u);
    // Cut the second page: a byte of its text changed (half programmed).
    const uint32_t region = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE * NVS_SECTORS - kRegionPages * kPage;
    mock_flash::memory()[region + kPage + 40] ^= 0x01;
    flash_log::reset_for_test();
    flash_log::init(20000);
    CHECK_EQ(flash_log::stats().pages, 1u);
    CHECK(all_text().find("second page") == std::string::npos);
    line("after the cut");
    run(20000, 6000);
    CHECK(all_text().find("after the cut") != std::string::npos);
}

TEST(a_full_region_drops_the_oldest_pages) {
    fresh();
    flash_log::init(0);
    uint32_t t = 0;
    for (uint32_t i = 0; i < kRegionPages + 40; ++i) {
        line("entry " + std::to_string(i) + std::string(200, '.'));
        t = run(t, 300);
    }
    const flash_log::Stats st = flash_log::stats();
    CHECK(st.pages <= kRegionPages);
    CHECK(st.oldest_seq > 1);
    flash_log::Page p;
    CHECK(flash_log::read(1, p));  // from the oldest kept
    CHECK_EQ(p.seq, st.oldest_seq);
    CHECK(all_text().find("entry " + std::to_string(kRegionPages + 39)) != std::string::npos);
}

TEST(a_power_on_starts_a_new_ring_generation) {
    fresh();
    flash_log::init(0);
    line("before unplugging");
    run(0, 6000);
    const uint32_t gen = flash_log::stats().ring_gen;
    log_ring_reset_for_test();  // unplugged: the RAM is gone
    flash_log::power_on_for_test();
    flash_log::init(0);
    CHECK_EQ(flash_log::stats().ring_gen, gen + 1);
    flash_log::Page p;
    CHECK(flash_log::read(1, p));
    CHECK_EQ(p.ring_gen, gen);  // the logger tells what it already read from RAM by (gen, pos)
}

TEST_MAIN()
