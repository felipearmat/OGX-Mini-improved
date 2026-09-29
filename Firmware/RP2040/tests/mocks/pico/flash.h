// Host mock of the Pico SDK flash_safe_execute(). By default it behaves like the SDK with the
// other core registered for lockout: interrupts off, run, restore. Tests can make it return an
// error instead (other core not registered, or not answering the lockout).
#pragma once

#include <cstdint>

#include "hardware/sync.h"

#ifndef PICO_OK
#define PICO_OK 0
#define PICO_ERROR_TIMEOUT (-1)
#define PICO_ERROR_NOT_PERMITTED (-4)
#endif

namespace mock_flash_safe {
inline int& result() {  // what the next calls return; PICO_OK runs the function
    static int rc = PICO_OK;
    return rc;
}
inline int& calls() {
    static int n = 0;
    return n;
}
}  // namespace mock_flash_safe

static inline int flash_safe_execute(void (*func)(void*), void* param, uint32_t) {
    ++mock_flash_safe::calls();
    if (mock_flash_safe::result() != PICO_OK)
        return mock_flash_safe::result();
    const uint32_t irq = save_and_disable_interrupts();
    func(param);
    restore_interrupts(irq);
    return PICO_OK;
}
