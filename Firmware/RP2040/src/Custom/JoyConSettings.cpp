#include "Custom/JoyConSettings.h"

#ifndef OGXM_JOYCON_PAIR_IMU_RIGHT
#define OGXM_JOYCON_PAIR_IMU_RIGHT 1
#endif
#ifndef OGXM_JOYCON_PAIR_HORIZONTAL
#define OGXM_JOYCON_PAIR_HORIZONTAL 0
#endif
#ifndef OGXM_JOYCON_SOLO_HORIZONTAL
#define OGXM_JOYCON_SOLO_HORIZONTAL 1
#endif

namespace joycon_settings {

Settings& get()
{
    static Settings settings{
        OGXM_JOYCON_PAIR_IMU_RIGHT != 0,
        OGXM_JOYCON_PAIR_HORIZONTAL ? Orientation::Horizontal : Orientation::Vertical,
        OGXM_JOYCON_SOLO_HORIZONTAL ? Orientation::Horizontal : Orientation::Vertical,
    };
    return settings;
}

void apply_orientation(bool left_joycon, Orientation orientation, int32_t accel[3], int32_t gyro[3])
{
    if (orientation == Orientation::Vertical)
        return;
    /* Sideways, the left Joy-Con's top points left (device X -> virtual left, device Y ->
     * virtual back): (x, y, z) -> (-y, x, z). The right one is the mirror: (y, -x, z). */
    int32_t* vectors[2] = {accel, gyro};
    for (int32_t* v : vectors)
    {
        const int32_t x = v[0];
        const int32_t y = v[1];
        v[0] = left_joycon ? -y : y;
        v[1] = left_joycon ? x : -x;
    }
}

} // namespace joycon_settings
