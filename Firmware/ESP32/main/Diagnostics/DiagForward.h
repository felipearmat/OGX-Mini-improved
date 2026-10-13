#ifndef _OGXM_DIAG_FORWARD_H_
#define _OGXM_DIAG_FORWARD_H_

#include <cstdint>

#include "I2C/Esp32Bridge.h"

/* The ESP32's side of Diagnostics/Diagnostics.h: each diag:: call the shared Bluetooth code makes
 * (Bluepad32/LinkDiag, BTManager) goes to the RP2040 as a SET_DIAG packet, where the report is
 * kept. Input timing and report counters ride on SET_PAD instead (per report). */
namespace diag_forward {

    using Sender = void (*)(const Esp32Bridge::PacketDiag& packet);
    void set_sender(Sender send);

    /* The controller's report counter last seen on this slot (diag::slot_counter), for the next
     * SET_PAD; false when there is none. */
    bool take_counter(int slot, uint8_t& value, uint8_t& bits, uint8_t& unit);

    /* Every feedback tick: counts gathered since the last one (BLE advertising reports). */
    void tick();

} // namespace diag_forward

#endif // _OGXM_DIAG_FORWARD_H_
