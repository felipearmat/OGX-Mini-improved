// NVSTool (settings in flash) against a simulated XIP flash.
// Regressions covered: flash erase/program with interrupts enabled (hard fault on a real
// RP2350 when an IRQ ran from flash), and entries past the first NVS sector being written
// at the wrong offset, and the settings overlapping BTstack's link keys on the Pico W boards.
#include <cstring>
#include <string>

#include "UserSettings/NVSTool.h"
#include "test.h"

namespace {

std::string key_for(int i) { return "key_" + std::to_string(i); }

}  // namespace

TEST(flash_is_only_touched_with_interrupts_disabled) {
    NVSTool& nvs = NVSTool::get_instance();  // constructor formats the empty flash
    mock_flash::stats() = {};
    const uint32_t value = 0x12345678;
    CHECK(nvs.write("driver_type", &value, sizeof(value)));
    nvs.erase_all();
    CHECK(mock_flash::stats().erases > 0);
    CHECK(mock_flash::stats().programs > 0);
    CHECK_EQ(mock_flash::stats().ops_with_irqs_enabled, 0);
    CHECK_EQ(mock_sync::irq_disable_depth(), 0);  // interrupts restored afterwards
}

TEST(round_trip_and_overwrite) {
    NVSTool& nvs = NVSTool::get_instance();
    nvs.erase_all();
    uint8_t mode = 4;
    CHECK(nvs.write("driver_type", &mode, 1));
    uint8_t out = 0;
    CHECK(nvs.read("driver_type", &out, 1));
    CHECK_EQ(out, 4);
    mode = 8;
    CHECK(nvs.write("driver_type", &mode, 1));
    CHECK(nvs.read("driver_type", &out, 1));
    CHECK_EQ(out, 8);
    CHECK(!nvs.read("missing", &out, 1));
}

TEST(entries_across_sectors_keep_their_values) {
    NVSTool& nvs = NVSTool::get_instance();
    nvs.erase_all();
    // 16 entries of 256 bytes fill one 4 KB sector; go past it, up to the last entry.
    const int count = static_cast<int>(NVSTool::MAX_ENTRIES) - 1;
    for (int i = 0; i < count; ++i) {
        const uint32_t v = 0xA5000000u | static_cast<uint32_t>(i);
        CHECK(nvs.write(key_for(i), &v, sizeof(v)));
    }
    for (int i = 0; i < count; ++i) {
        uint32_t v = 0;
        CHECK(nvs.read(key_for(i), &v, sizeof(v)));
        CHECK_EQ(v, 0xA5000000u | static_cast<uint32_t>(i));
    }
    CHECK_EQ(mock_flash::stats().ops_with_irqs_enabled, 0);
}

TEST(btstacks_bank_is_never_touched) {
    // The last NVS_BT_BANK_SECTORS sectors hold BTstack's link keys on the Pico W boards:
    // formatting and filling the settings leave them as they were.
    const uint32_t bank = PICO_FLASH_SIZE_BYTES - NVS_BT_BANK_SECTORS * FLASH_SECTOR_SIZE;
    for (uint32_t i = 0; i < NVS_BT_BANK_SECTORS * FLASH_SECTOR_SIZE; ++i)
        mock_flash::memory()[bank + i] = static_cast<uint8_t>(i * 7 + 3);
    NVSTool& nvs = NVSTool::get_instance();
    nvs.erase_all();
    for (uint32_t i = 0; i < NVSTool::MAX_ENTRIES + 5; ++i) {  // past the last entry: refused
        const uint32_t v = i;
        nvs.write(key_for(static_cast<int>(i)), &v, sizeof(v));
    }
    bool intact = true;
    for (uint32_t i = 0; i < NVS_BT_BANK_SECTORS * FLASH_SECTOR_SIZE; ++i)
        intact = intact && mock_flash::memory()[bank + i] == static_cast<uint8_t>(i * 7 + 3);
    CHECK(intact);
    const uint32_t v = 1;
    CHECK(!nvs.write("one_too_many", &v, sizeof(v)));
    nvs.erase_all();  // room again for the next tests
}

TEST(writes_go_through_flash_safe_execute) {
    NVSTool& nvs = NVSTool::get_instance();
    mock_flash_safe::result() = PICO_OK;
    const int before = mock_flash_safe::calls();
    const uint8_t v = 7;
    CHECK(nvs.write("mode", &v, 1));
    CHECK(mock_flash_safe::calls() > before);  // the other core gets parked
}

TEST(other_core_not_registered_still_writes) {
    // Boards whose Core1 never registers for lockout: write as before, interrupts off.
    NVSTool& nvs = NVSTool::get_instance();
    mock_flash_safe::result() = PICO_ERROR_NOT_PERMITTED;
    mock_flash::stats() = {};
    const uint8_t v = 9;
    CHECK(nvs.write("mode", &v, 1));
    uint8_t out = 0;
    CHECK(nvs.read("mode", &out, 1));
    CHECK_EQ(out, 9);
    CHECK_EQ(mock_flash::stats().ops_with_irqs_enabled, 0);
    mock_flash_safe::result() = PICO_OK;
}

TEST(unresponsive_other_core_skips_the_write) {
    // The other core did not park in time: writing now could crash it, so nothing is written.
    NVSTool& nvs = NVSTool::get_instance();
    const uint8_t v1 = 1;
    CHECK(nvs.write("mode", &v1, 1));
    mock_flash_safe::result() = PICO_ERROR_TIMEOUT;
    mock_flash::stats() = {};
    const uint8_t v2 = 2;
    CHECK(!nvs.write("mode", &v2, 1));
    CHECK_EQ(mock_flash::stats().erases, 0);
    uint8_t out = 0;
    CHECK(nvs.read("mode", &out, 1));
    CHECK_EQ(out, 1);
    mock_flash_safe::result() = PICO_OK;
}

TEST(halted_other_core_writes_directly) {
    // Mode change: usb::disconnect_all() reset Core1, a lockout would never be answered.
    // Keep this test last: the halted flag stays set.
    NVSTool& nvs = NVSTool::get_instance();
    NVSTool::set_other_core_halted();
    mock_flash_safe::result() = PICO_ERROR_TIMEOUT;
    const int before = mock_flash_safe::calls();
    mock_flash::stats() = {};
    const uint8_t v = 3;
    CHECK(nvs.write("mode", &v, 1));
    CHECK_EQ(mock_flash_safe::calls(), before);
    CHECK_EQ(mock_flash::stats().ops_with_irqs_enabled, 0);
    uint8_t out = 0;
    CHECK(nvs.read("mode", &out, 1));
    CHECK_EQ(out, 3);
}

TEST(an_erased_sector_counts_as_free_entries) {
    // 2026-10-22, hybrid: the second settings sector read all 0xFF (a rewrite cut between its erase
    // and its program). Those entries counted as taken, so the adapter options were missing (read
    // as defaults) and every new write failed: the table looked full.
    NVSTool& nvs = NVSTool::get_instance();
    nvs.erase_all();
    const uint32_t nvs_start = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE * NVS_SECTORS;
    uint8_t v = 1;
    for (int i = 0; i < 15; ++i)  // fill the first sector (entry 0 is the header)
        CHECK(nvs.write(key_for(i), &v, 1));
    std::memset(mock_flash::memory() + nvs_start + FLASH_SECTOR_SIZE, 0xFF, FLASH_SECTOR_SIZE);
    uint8_t cfg[24] = {8, 0, 0};
    CHECK(nvs.write("dongle_cfg", cfg, sizeof(cfg)));  // failed: "no space"
    uint8_t out[24] = {};
    CHECK(nvs.read("dongle_cfg", out, sizeof(out)));
    CHECK_EQ(out[0], 8);
    CHECK(nvs.read(key_for(14), &v, 1));                // the first sector's entries stay
}

TEST_MAIN()
