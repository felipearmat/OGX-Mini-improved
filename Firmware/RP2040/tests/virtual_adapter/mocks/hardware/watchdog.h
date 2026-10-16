// Virtual adapter: the watchdog's scratch registers (Board/BootMode.h) and a no-op watchdog.
#pragma once
#include <stdint.h>
typedef struct { uint32_t scratch[8]; } watchdog_hw_t;
inline watchdog_hw_t* virtual_watchdog() { static watchdog_hw_t hw{}; return &hw; }
#define watchdog_hw (virtual_watchdog())
static inline void watchdog_enable(uint32_t, bool) {}
static inline void watchdog_update(void) {}
static inline bool watchdog_caused_reboot(void) { return false; }
static inline bool watchdog_enable_caused_reboot(void) { return false; }
