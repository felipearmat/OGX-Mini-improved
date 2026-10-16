// Virtual adapter: TinyUSB's BSP call the CDC descriptors use.
#pragma once
#include <stddef.h>
#include <stdint.h>
extern "C" size_t board_usb_get_serial(uint16_t desc_str[], size_t max_chars);
