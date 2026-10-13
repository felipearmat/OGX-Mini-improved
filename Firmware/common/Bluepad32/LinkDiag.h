#ifndef _OGXM_LINK_DIAG_H_
#define _OGXM_LINK_DIAG_H_

#include <cstdint>

/* Bluetooth link details for the diagnostics report (Diagnostics/Diagnostics.h): LE connection
 * parameters, remote chip and version, RSSI, channels in use, failed contacts, transmit power,
 * sniff mode, the controllers' own report counters, BLE Device Information. Shared by the Pico W
 * path (diag:: is the report itself) and the hybrid's ESP32 (diag:: forwards to the RP2040 over
 * I2C). Runs on the BTstack run loop; the Bluepad32 hooks (uni_diag_hooks.h) are defined here. */
namespace link_diag {

    void init();                    // after uni_init(): HCI event handler
    void on_slot_ready(int slot);   // ask its remote version and maximum transmit power once
    /* Every feedback tick: one HCI query per 700 ms, rotating over the connected slots. */
    void query(uint32_t now_ms, bool (*slot_connected)(int slot));

} // namespace link_diag

#endif // _OGXM_LINK_DIAG_H_
