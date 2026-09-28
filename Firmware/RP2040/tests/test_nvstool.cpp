// NVSTool (settings in flash) against a simulated XIP flash.
// Regressions covered: flash erase/program with interrupts enabled (hard fault on a real
// RP2350 when an IRQ ran from flash), and entries past the first NVS sector being written
// at the wrong offset.
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
    // 16 entries of 256 bytes fill one 4 KB sector; go well past it.
    const int count = 40;
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

TEST_MAIN()
