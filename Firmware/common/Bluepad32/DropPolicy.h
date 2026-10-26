#ifndef _OGXM_DROP_POLICY_H_
#define _OGXM_DROP_POLICY_H_

#include <cstdint>

/* What the boards running Bluepad32 do when pads drop (Bluepad32/BtPad.h includes it; no Bluepad32
 * dependency, so the host tests use it as is). */
namespace bt_pad {

    /* After the last pad that reached "ready" disconnects, the board reboots (not after a failed
     * pairing attempt, or pairing would loop): some Classic HID pads leave Bluepad32 / BTstack in
     * a stale state where they reconnect but never send usable input again. The reboot is armed
     * on a hardware timer too (kLastPadRebootFallbackMs), as a run-loop timer never fires if the
     * BT core hangs while the pads go away. */
    constexpr uint32_t kLastPadRebootDelayMs = 500;
    constexpr uint32_t kLastPadRebootFallbackMs = 3000;

    /* A pad that keeps dropping (a Joy-Con with a flat battery connected and dropped every 20-30
     * s, 2026-10-25) restarted the hybrid's ESP32 each time, and each restart left it without the
     * RP2040's settings for a while (the mode combo then did nothing). In the first
     * kLastPadRestartMinUptimeMs after a start, the restart after the last pad waits until then:
     * a pad connecting meanwhile calls it off (the ESP32 restarts on its own; the Pico W reboots
     * the whole adapter, USB included, and keeps its immediate reboot). */
    constexpr uint32_t kLastPadRestartMinUptimeMs = 60000;

    constexpr uint32_t last_pad_restart_delay_ms(uint32_t uptime_ms)
    {
        return uptime_ms + kLastPadRebootDelayMs >= kLastPadRestartMinUptimeMs
                   ? kLastPadRebootDelayMs
                   : kLastPadRestartMinUptimeMs - uptime_ms;
    }

    /* Battery level (Bluepad32: 1 empty .. 255 full, 0 unknown) at which the diagnostics log says
     * so once per connection: a pad dropping on a flat battery looks like a radio problem. */
    constexpr uint8_t kBatteryLowLevel = 64;  // about 25%

    constexpr bool battery_low(uint8_t level)
    {
        return level != 0 && level <= kBatteryLowLevel;  // 0: UNI_CONTROLLER_BATTERY_NOT_AVAILABLE
    }

} // namespace bt_pad

#endif // _OGXM_DROP_POLICY_H_
