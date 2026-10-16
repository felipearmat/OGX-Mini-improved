#ifndef _OGXM_MODE_SYNC_H_
#define _OGXM_MODE_SYNC_H_

#include <cstdint>

#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"

/* Who decides the output mode on the RP2040 + ESP32 hybrid (custom addition). The RP2040 keeps the
 * settings and restarts in the mode it stored; the ESP32 reads the mode combos and asks for a
 * change (SET_DRIVER), and learns the RP2040's mode from its PacketOutExt (GET_EXT). An RP2040
 * that keeps no settings (older firmware) never answers GET_EXT: then the ESP32's own mode, in its
 * flash, is the one to use. Used by both firmwares; tests/test_esp32_bridge.cpp plays the start-up
 * sequences through these rules. */
namespace mode_sync {

    /* PacketOutExt answers the RP2040 sends before it takes mode requests from the ESP32: until the
     * ESP32 has read one, a freshly started ESP32 sends the mode in its own flash, which would undo
     * the mode the RP2040 just restarted in. */
    constexpr uint8_t kRepliesBeforeRequests = 2;

    /* How long the ESP32 waits for an RP2040 answer before it takes the RP2040 for one that keeps
     * no settings and sends its own mode. */
    constexpr uint32_t kNoSettingsRp2040Ms = 3000;

    // RP2040: whether a SET_DRIVER from the ESP32 changes the mode.
    constexpr bool rp2040_accepts(uint8_t replies_sent, DeviceDriverType requested, DeviceDriverType current)
    {
        return replies_sent >= kRepliesBeforeRequests && requested != DeviceDriverType::NONE &&
               normalized(requested) != current;
    }

    // ESP32: whether it sends its own (flash) mode to the RP2040 this tick.
    constexpr bool esp32_sends_own_mode(bool rp2040_keeps_settings, uint32_t ms_since_start)
    {
        return !rp2040_keeps_settings && ms_since_start >= kNoSettingsRp2040Ms;
    }

} // namespace mode_sync

#endif // _OGXM_MODE_SYNC_H_
