#ifndef _OGXM_FIXTURE_CONSOLE_OUTPUTS_H_
#define _OGXM_FIXTURE_CONSOLE_OUTPUTS_H_

#include "USBDevice/DeviceDriver/N64/n64_device.h"
#include "USBDevice/DeviceDriver/PS1PS2/psx_simulator.h"
#include "USBHost/GPIOHost/dreamcast_host/dreamcast_structures.h"

/* What the GPIO console outputs hand their bus loop (console_outputs.cpp): the loops themselves
 * (PIO, Maple Bus) never run on the PC. */
const N64Report& n64_report();
const PSXInputState* psx_state();
const controller_condition_t& dreamcast_condition();

#endif // _OGXM_FIXTURE_CONSOLE_OUTPUTS_H_
