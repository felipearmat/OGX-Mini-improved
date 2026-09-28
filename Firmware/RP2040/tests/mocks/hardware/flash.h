// Host mock of the Pico SDK flash API: a RAM array stands in for the XIP-mapped flash.
// Erase/program follow NOR semantics and record whether interrupts were disabled.
#pragma once

#include <cstdint>
#include <cstring>

#include "hardware/sync.h"

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 256u
#endif
#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE 4096u
#endif
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (64u * 1024u)
#endif

namespace mock_flash {
inline uint8_t* memory() {
    static uint8_t mem[PICO_FLASH_SIZE_BYTES];
    return mem;
}
struct Stats {
    int erases = 0;
    int programs = 0;
    int ops_with_irqs_enabled = 0;
};
inline Stats& stats() {
    static Stats s;
    return s;
}
}  // namespace mock_flash

#define XIP_BASE (reinterpret_cast<uintptr_t>(mock_flash::memory()))

static inline void flash_range_erase(uint32_t offset, size_t count) {
    auto& s = mock_flash::stats();
    ++s.erases;
    if (mock_sync::irq_disable_depth() == 0)
        ++s.ops_with_irqs_enabled;
    std::memset(mock_flash::memory() + offset, 0xFF, count);
}

static inline void flash_range_program(uint32_t offset, const uint8_t* data, size_t count) {
    auto& s = mock_flash::stats();
    ++s.programs;
    if (mock_sync::irq_disable_depth() == 0)
        ++s.ops_with_irqs_enabled;
    uint8_t* dst = mock_flash::memory() + offset;
    for (size_t i = 0; i < count; ++i)
        dst[i] &= data[i];  // NOR flash can only clear bits
}
