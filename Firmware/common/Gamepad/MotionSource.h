#ifndef _OGXM_MOTION_SOURCE_H_
#define _OGXM_MOTION_SOURCE_H_

#include <cstdint>

/* Where a pad's motion data comes from (Gamepad::PadIn::motion_source on the RP2040, and the I2C
 * link of the hybrid: I2C/Esp32Bridge.h). Values cross the link: never move them. */
namespace motion_source {

    constexpr uint8_t NONE = 0;
    constexpr uint8_t DS4 = 1;
    constexpr uint8_t DS5 = 2;
    constexpr uint8_t SWITCH_PRO = 3;
    constexpr uint8_t DS5_USB = 4;
    constexpr uint8_t DS4_USB = 5;
    constexpr uint8_t SWITCH_USB = 6;
    constexpr uint8_t WII_BT = 7;

} // namespace motion_source

#endif // _OGXM_MOTION_SOURCE_H_
