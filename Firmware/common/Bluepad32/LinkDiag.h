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
    /* Ask the controller to poll each Classic link at least this often (HCI QoS Setup, guaranteed
     * service), once per pad; 0 (default) leaves it to the controller. The ESP32's controller
     * otherwise leaves gaps of up to about 90 ms on a Joy-Con pair; the Pico W's links are in sniff. */
    void set_poll_latency_us(uint32_t us);
    /* Every feedback tick: one HCI query per 700 ms, rotating over the connected slots. */
    void query(uint32_t now_ms, bool (*slot_connected)(int slot));

} // namespace link_diag

#endif // _OGXM_LINK_DIAG_H_
