// Virtual adapter: tests/mocks/pico/time.h plus the SDK's sleeps (virtual_board.cpp keeps the
// serial port served meanwhile).
#pragma once
#include_next "pico/time.h"
void sleep_ms(uint32_t ms);
void busy_wait_ms(uint32_t ms);
