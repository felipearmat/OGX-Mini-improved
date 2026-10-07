#include "Bluepad32/JoyConSettings.h"
#include "UserSettings/DongleSettings.h"

namespace joycon_settings {

Settings get()
{
    const auto& d = dongle_settings::get();
    return Settings{
        d.joycon_pair_imu_right != 0,
        d.joycon_pair_horizontal ? Orientation::Horizontal : Orientation::Vertical,
        d.joycon_solo_horizontal ? Orientation::Horizontal : Orientation::Vertical,
        d.joycon_pair_rumble_per_side != 0,
    };
}

HalfRumble pair_half_rumble(bool left_joycon, bool per_side, uint8_t rumble_l, uint8_t rumble_r)
{
    if (!per_side)
        return HalfRumble{rumble_r, rumble_l};  // the whole request, as for a single pad
    return left_joycon ? HalfRumble{0, rumble_l} : HalfRumble{rumble_r, 0};
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
