// Host mock of the Pico SDK multicore API: nothing runs on a second core in the tests.
#pragma once

static inline void multicore_reset_core1(void) {}
static inline void multicore_launch_core1(void (*entry)(void)) { (void)entry; }
