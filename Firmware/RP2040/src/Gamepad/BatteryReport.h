#ifndef _OGXM_BATTERY_REPORT_H_
#define _OGXM_BATTERY_REPORT_H_

#include <cstdint>

/* Custom: the pad's battery (Gamepad::PadIn battery, Bluepad32 scale 1-255, 0 = unknown, and
 * battery_charging) in each output mode's own field, so the host shows it (Steam, Linux, a
 * console). Unknown reads as full on power, as these modes reported it before. */
namespace battery_report {

    /* 0-10 (DS4 / DualSense level). */
    constexpr uint8_t level_0_10(uint8_t battery)
    {
        return static_cast<uint8_t>((battery * 10u + 127u) / 255u);
    }

    /* DS4 status byte: bits 0-3 the level (0-10; 11 = full on a cable), bit 4 cable connected. */
    constexpr uint8_t ds4(uint8_t battery, bool charging)
    {
        if (battery == 0) return 0x1B;  // unknown: on a cable, full
        const uint8_t level = level_0_10(battery);
        return charging ? static_cast<uint8_t>(0x10 | (level >= 10 ? 11 : level)) : level;
    }

    /* DualSense status byte: bits 0-3 the level (0-10), bits 4-7 0 discharging, 1 charging, 2 full. */
    constexpr uint8_t ds5(uint8_t battery, bool charging)
    {
        if (battery == 0) return 0x2A;  // unknown: full
        const uint8_t level = level_0_10(battery);
        if (!charging) return level;
        return level >= 10 ? 0x2A : static_cast<uint8_t>(0x10 | level);
    }

    /* Switch Pro Controller byte 2: bits 5-7 the level (8 full, 6 medium, 4 low, 2 critical, 0 empty,
     * as a nibble with bit 4 = charging), bits 0-3 the connection (1: Pro Controller, Switch / USB
     * powered). */
    constexpr uint8_t switch_pro(uint8_t battery, bool charging)
    {
        if (battery == 0) return 0x81;  // unknown: full
        const uint8_t pct = static_cast<uint8_t>((battery * 100u + 127u) / 255u);
        const uint8_t level = pct >= 88 ? 8 : pct >= 50 ? 6 : pct >= 20 ? 4 : pct > 0 ? 2 : 0;
        return static_cast<uint8_t>(((level | (charging ? 1 : 0)) << 4) | 0x01);
    }

    /* DualShock 3 report: power status (Descriptors/PS3.h PowerState: 0xEE charging, 0xEF charged,
     * 0x05 full ... 0x01 shutting down) and plugged (0x02 plugged, 0x03 unplugged). */
    struct Ds3 {
        uint8_t power_status;
        uint8_t plugged;
    };
    constexpr Ds3 ds3(uint8_t battery, bool charging)
    {
        if (battery == 0) return {0x05, 0x02};  // unknown: full, plugged (as before)
        if (charging) return {static_cast<uint8_t>(battery >= 250 ? 0xEF : 0xEE), 0x02};
        const uint8_t pct = static_cast<uint8_t>((battery * 100u + 127u) / 255u);
        return {static_cast<uint8_t>(pct >= 80 ? 0x05 : pct >= 55 ? 0x04 : pct >= 30 ? 0x03 : pct > 10 ? 0x02 : 0x01),
                0x03};
    }

} // namespace battery_report

#endif // _OGXM_BATTERY_REPORT_H_
