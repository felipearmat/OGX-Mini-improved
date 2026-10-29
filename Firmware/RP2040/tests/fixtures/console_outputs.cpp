// The GPIO console outputs' side of the fixture replay (console_outputs.h): their bus loops
// stubbed, the state each driver hands them kept for the test. The Dreamcast driver keeps its
// controller condition in a file-local variable, so its source is compiled here.
#include "console_outputs.h"
#include "USBDevice/DeviceDriver/GameCube/gc_simulator.h"
#include "USBDevice/DeviceDriver/Dreamcast/Dreamcast.cpp"

namespace {
N64Report s_n64{};
PSXInputState* s_psx = nullptr;
}  // namespace

extern "C" {
void gc_device_main(unsigned, GCReport*, int) {}
void n64_device_main(int) {}
void n64_device_set_report(const N64Report* report) { s_n64 = *report; }
void psx_device_init(unsigned, PSXInputState* data, void (*)(void)) { s_psx = data; }
void psx_device_main(void) {}
}

const N64Report& n64_report() { return s_n64; }
const PSXInputState* psx_state() { return s_psx; }
const controller_condition_t& dreamcast_condition() { return s_device_condition; }
