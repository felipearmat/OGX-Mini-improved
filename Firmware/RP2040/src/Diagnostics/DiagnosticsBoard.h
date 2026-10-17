#ifndef _OGXM_CUSTOM_DIAGNOSTICS_BOARD_H_
#define _OGXM_CUSTOM_DIAGNOSTICS_BOARD_H_

#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"

class Gamepad;

namespace diag {

    const char* driver_name(DeviceDriverType type);
    /* The gamepad whose input-to-use latency goes into the session summary (Diagnostics.h
     * set_latency_source); each board sets it at boot. */
    void set_latency_gamepad(Gamepad* gamepad);
    // Board info + "boot" event; call once the output mode is known (after flash init).
    void board_boot();

} // namespace diag

#endif // _OGXM_CUSTOM_DIAGNOSTICS_BOARD_H_
