#ifndef _OGXM_BT_ADAPTER_ADDRESS_H_
#define _OGXM_BT_ADAPTER_ADDRESS_H_

#include <atomic>
#include <cstdint>
#include <cstring>

/* The adapter's Bluetooth address on boards whose radio is not on the RP2040: the RP2040 + ESP32
 * hybrid gets the ESP32's from the I2C link (SET_DIAG ADAPTER_ADDRESS). A DualShock 3 plugged
 * into the USB host port is told this address (USBHost/HostDriver/PS3), so it then connects
 * to the ESP32 over Bluetooth. */
namespace bt_adapter_address {

    inline uint8_t s_address[6]{};
    inline std::atomic<bool> s_known{false};

    inline void set(const uint8_t address[6])
    {
        std::memcpy(s_address, address, sizeof(s_address));
        s_known.store(true, std::memory_order_release);
    }

    /* False until the ESP32 has sent it. */
    inline bool get(uint8_t address[6])
    {
        if (!s_known.load(std::memory_order_acquire))
            return false;
        std::memcpy(address, s_address, sizeof(s_address));
        return true;
    }

} // namespace bt_adapter_address

#endif // _OGXM_BT_ADAPTER_ADDRESS_H_
