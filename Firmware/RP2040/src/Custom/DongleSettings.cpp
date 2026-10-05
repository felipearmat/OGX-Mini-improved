#include "Custom/DongleSettings.h"

#ifndef OGXM_DISCONNECT_PADS_ON_MODE_CHANGE
#define OGXM_DISCONNECT_PADS_ON_MODE_CHANGE 0
#endif
#ifndef OGXM_JOYCON_PAIR_IMU_RIGHT
#define OGXM_JOYCON_PAIR_IMU_RIGHT 1
#endif
#ifndef OGXM_JOYCON_PAIR_HORIZONTAL
#define OGXM_JOYCON_PAIR_HORIZONTAL 0
#endif
#ifndef OGXM_JOYCON_SOLO_HORIZONTAL
#define OGXM_JOYCON_SOLO_HORIZONTAL 1
#endif
#ifndef OGXM_MAC_PER_CONTROLLER
#define OGXM_MAC_PER_CONTROLLER 0
#endif
#ifndef OGXM_PS4_LEGACY_MOTION_SCALE
#define OGXM_PS4_LEGACY_MOTION_SCALE 0
#endif
#ifndef OGXM_SINGLE_CONTROLLER
#define OGXM_SINGLE_CONTROLLER 0
#endif
#ifndef OGXM_JOYCON_PAIR_RUMBLE_PER_SIDE
#define OGXM_JOYCON_PAIR_RUMBLE_PER_SIDE 1
#endif

namespace dongle_settings {

namespace {

uint8_t flag(int v)
{
    return v ? 1 : 0;
}

Settings& current()
{
    static Settings settings = defaults();
    return settings;
}

} // namespace

Settings defaults()
{
    Settings s{};
    s.version = kVersion;
    s.disconnect_pads_on_mode_change = flag(OGXM_DISCONNECT_PADS_ON_MODE_CHANGE);
    s.joycon_pair_imu_right = flag(OGXM_JOYCON_PAIR_IMU_RIGHT);
    s.joycon_pair_horizontal = flag(OGXM_JOYCON_PAIR_HORIZONTAL);
    s.joycon_solo_horizontal = flag(OGXM_JOYCON_SOLO_HORIZONTAL);
    s.mac_per_controller = flag(OGXM_MAC_PER_CONTROLLER);
    s.ps4_legacy_motion_scale = flag(OGXM_PS4_LEGACY_MOTION_SCALE);
    s.single_controller = flag(OGXM_SINGLE_CONTROLLER);
    s.joycon_pair_rumble_per_side = flag(OGXM_JOYCON_PAIR_RUMBLE_PER_SIDE);
    return s;
}

bool decode(const uint8_t* data, size_t len, Settings& out)
{
    if (data == nullptr || len < 1)
        return false;
    const bool v2 = data[0] == kVersion && len >= sizeof(Settings);
    const bool v1 = data[0] == 1 && len >= kV1Length;
    if (!v2 && !v1)
        return false;
    Settings s = defaults();  // a version 1 record leaves the newer options at their defaults
    s.disconnect_pads_on_mode_change = flag(data[1]);
    s.joycon_pair_imu_right = flag(data[2]);
    s.joycon_pair_horizontal = flag(data[3]);
    s.joycon_solo_horizontal = flag(data[4]);
    s.mac_per_controller = flag(data[5]);
    s.ps4_legacy_motion_scale = flag(data[6]);
    s.single_controller = flag(data[7]);
    if (v2)
        s.joycon_pair_rumble_per_side = flag(data[8]);
    out = s;
    return true;
}

const Settings& get()
{
    return current();
}

void set(const Settings& settings)
{
    current() = settings;
}

} // namespace dongle_settings
