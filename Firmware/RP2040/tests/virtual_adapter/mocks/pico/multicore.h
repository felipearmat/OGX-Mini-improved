// Virtual adapter: one core, nothing to lock out.
#pragma once
static inline void multicore_lockout_victim_init(void) {}
static inline void multicore_reset_core1(void) {}
