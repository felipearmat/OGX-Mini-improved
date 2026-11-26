// Host stand-in for tuh_xinput.h (TinyUSB XInput host class): the calls the XInput host drivers
// make, recorded by the test that includes this folder (test_xbox360w_rumble.cpp).
#pragma once

#include <cstdint>

namespace tuh_xinput {

bool is_connected(uint8_t dev_addr, uint8_t instance);
bool set_rumble(uint8_t dev_addr, uint8_t instance, uint8_t rumble_l, uint8_t rumble_r, bool block);
bool receive_report(uint8_t dev_addr, uint8_t instance);
void xbox360_chatpad_init(uint8_t address, uint8_t instance);

}  // namespace tuh_xinput
