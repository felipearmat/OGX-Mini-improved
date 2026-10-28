// Host mock of the Pico SDK USB controller registers: what XInput's wake signal touches
// (USBDevice/DeviceDriver/XInput/XInput.cpp), plus the SDK's register bit helpers it uses.
#pragma once

#include <stdint.h>

typedef struct {
    volatile uint32_t sie_ctrl;
} usb_hw_t;

static usb_hw_t mock_usb_hw;
#define usb_hw (&mock_usb_hw)

static inline void hw_write_masked(volatile uint32_t* addr, uint32_t values, uint32_t mask) {
    *addr = (*addr & ~mask) | (values & mask);
}
static inline void hw_clear_bits(volatile uint32_t* addr, uint32_t mask) { *addr &= ~mask; }
