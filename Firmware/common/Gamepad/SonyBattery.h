#ifndef _OGXM_SONY_BATTERY_H_
#define _OGXM_SONY_BATTERY_H_

#include <cstdint>

#include "Diagnostics/Diagnostics.h"

/* Battery of a DualShock 4 / DualSense from the status byte of its own input report, read as Linux
 * hid-playstation does (custom addition). Bluepad32 keeps only a level (DS4: nibble * 25 + 1), with
 * no charging state. */
namespace sony_battery {

    struct Reading {
        uint8_t percent;
        diag::BatteryState state;
    };

    /* DS4: bits 0-3 capacity (0-10; 11 = full on a cable), bit 4 cable connected. */
    constexpr Reading ds4(uint8_t status)
    {
        const uint8_t capacity = status & 0x0F;
        if ((status & 0x10) == 0)
            return {static_cast<uint8_t>(capacity < 10 ? capacity * 10 + 5 : 100), diag::BatteryState::DISCHARGING};
        if (capacity < 10)
            return {static_cast<uint8_t>(capacity * 10 + 5), diag::BatteryState::CHARGING};
        if (capacity == 10)
            return {100, diag::BatteryState::CHARGING};
        if (capacity == 11)
            return {100, diag::BatteryState::FULL};
        return {0, diag::BatteryState::UNKNOWN};
    }

    /* DualSense: bits 0-3 capacity (0-10), bits 4-7 charging status (0 discharging, 1 charging,
     * 2 full, 0xA / 0xB voltage or temperature error). */
    constexpr Reading ds5(uint8_t status)
    {
        const uint8_t capacity = status & 0x0F;
        const uint8_t pct = static_cast<uint8_t>(capacity * 10 + 5 > 100 ? 100 : capacity * 10 + 5);
        switch (status >> 4) {
            case 0x0: return {pct, diag::BatteryState::DISCHARGING};
            case 0x1: return {pct, diag::BatteryState::CHARGING};
            case 0x2: return {100, diag::BatteryState::FULL};
            case 0xA:
            case 0xB: return {0, diag::BatteryState::NOT_CHARGING};
            default: return {0, diag::BatteryState::UNKNOWN};
        }
    }

    // Offsets of the status byte in the Bluetooth input reports (report ID at [0]).
    constexpr uint16_t kDs4StatusBt11 = 3 + 29;   // 0x11: ID, 2 bytes, then the common report
    constexpr uint16_t kDs5StatusBt31 = 2 + 52;   // 0x31: ID, 1 byte, then the common report

} // namespace sony_battery

#endif // _OGXM_SONY_BATTERY_H_
