// Host mock of the Pico SDK interrupt API. Tracks whether interrupts are "disabled" so tests
// can assert that flash is only erased/programmed with interrupts off.
#pragma once

#include <cstdint>

namespace mock_sync {
inline int& irq_disable_depth() {
    static int depth = 0;
    return depth;
}
}  // namespace mock_sync

static inline uint32_t save_and_disable_interrupts() {
    return static_cast<uint32_t>(mock_sync::irq_disable_depth()++);
}

static inline void restore_interrupts(uint32_t) { --mock_sync::irq_disable_depth(); }
