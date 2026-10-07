#ifndef _OGXM_CUSTOM_REPORTED_MAC_H_
#define _OGXM_CUSTOM_REPORTED_MAC_H_

#include <cstdint>

/*  MAC address reported by the emulated DS4 / DualSense pairing-info feature (custom addition).
 *
 *  Default: the dongle's own address (Board/BoardMac). With the "MAC address per controller"
 *  dongle option, the Bluetooth address of the pad connected since boot instead, so hosts such
 *  as Steam keep separate settings per controller. Hosts read it once, when the device
 *  enumerates, and the pad connects after that: if the host already read another address, the
 *  USB device is reconnected once so it reads the pad's (see take_reconnect_request()). A pad
 *  disconnect reboots the dongle, which starts over.
 */
namespace reported_mac {

    // Bluetooth side (Core1): a pad is ready. bd_addr as BTstack stores it (most significant
    // byte first). Only the first pad since boot counts (a Joy-Con pair keeps its first half).
    void on_pad_ready(const uint8_t bd_addr[6]);

    // USB side: the MAC to report, least significant byte first. Remembers what was reported.
    void get_lsb_first(uint8_t mac[6]);

    // Core0 main loop: true once when the USB device should reconnect to report a new MAC.
    bool take_reconnect_request();

    // Call once from Core0 before Core1 starts (sets up the lock shared by both cores).
    void init();

} // namespace reported_mac

#endif // _OGXM_CUSTOM_REPORTED_MAC_H_
