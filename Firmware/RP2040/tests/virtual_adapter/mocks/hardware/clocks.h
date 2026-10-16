// Virtual adapter: the clock DiagnosticsBoard reports (an RP2350 at 150 MHz).
#pragma once
#include <stdint.h>
enum clock_index { clk_sys = 5 };
static inline uint32_t clock_get_hz(enum clock_index) { return 150000000u; }
