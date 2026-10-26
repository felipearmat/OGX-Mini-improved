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

    // RP2040 (one that keeps the settings): whether a SET_DRIVER from the ESP32 changes the mode.
    // Only a button combo does (from_combo, PacketIn::FROM_COMBO): the ESP32's own stored mode is
    // for RP2040s without settings, and taking it undid the mode this RP2040 restarted in whenever
    // the ESP32 missed its first answers (lost I2C reads, a slow start).
    constexpr bool rp2040_accepts(uint8_t replies_sent, DeviceDriverType requested, DeviceDriverType current,
                                  bool from_combo)
    {
        return from_combo && replies_sent >= kRepliesBeforeRequests && requested != DeviceDriverType::NONE &&
               normalized(requested) != current;
    }

    // ESP32: whether it sends its own (flash) mode to the RP2040 this tick.
    constexpr bool esp32_sends_own_mode(bool rp2040_keeps_settings, uint32_t ms_since_start)
    {
        return !rp2040_keeps_settings && ms_since_start >= kNoSettingsRp2040Ms;
    }

    /* RP2040: a SET_DRIVER counts once the same request came twice within kConfirmWindowMs (the
     * ESP32 repeats a combo's request every tick): one corrupted packet does not change the mode. */
    constexpr uint32_t kConfirmWindowMs = 2000;

    struct RequestConfirm
    {
        DeviceDriverType last{DeviceDriverType::NONE};
        uint32_t last_ms{0};

        bool take(DeviceDriverType requested, uint32_t now_ms)
        {
            const bool confirmed = requested == last && now_ms - last_ms < kConfirmWindowMs;
            last = requested;
            last_ms = now_ms;
            return confirmed;
        }
    };

    /* ESP32: ticks (mode_combos::kCheckDelayMs apart) a combo's request is repeated for, unless the
     * RP2040 reports the new mode first. */
    constexpr uint8_t kComboRequestTicks = 10;

    enum class Esp32Send : uint8_t { NOTHING, OWN_MODE, COMBO_REQUEST };

    /* ESP32: what it sends the RP2040 this tick. A combo's request goes first, flagged as a combo
     * every time: an ESP32 that had not got the RP2040's settings yet (just restarted, reads lost)
     * flagged it once and then sent its own mode unflagged, so the RP2040 never had the same
     * combo request twice and the combo did nothing (2026-10-25). */
    constexpr Esp32Send esp32_send(bool rp2040_keeps_settings, uint32_t ms_since_start, bool combo_pending)
    {
        return combo_pending ? Esp32Send::COMBO_REQUEST
                             : (esp32_sends_own_mode(rp2040_keeps_settings, ms_since_start) ? Esp32Send::OWN_MODE
                                                                                           : Esp32Send::NOTHING);
    }

} // namespace mode_sync

#endif // _OGXM_MODE_SYNC_H_
