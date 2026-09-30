// Host mock of the Pico SDK time API. Tests set mock_time_us to control the clock.
#pragma once

#include <stdint.h>

typedef uint64_t absolute_time_t;

extern uint64_t mock_time_us;

static inline absolute_time_t get_absolute_time(void) { return mock_time_us; }
static inline uint64_t time_us_64(void) { return mock_time_us; }
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000u); }
