// Virtual adapter: BOOTSEL ends the process with exit code 4 (virtual_board.cpp).
#pragma once
#include <stdint.h>
extern "C" void reset_usb_boot(uint32_t gpio_activity_pin_mask, uint32_t disable_interface_mask);
