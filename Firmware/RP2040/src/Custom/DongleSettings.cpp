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
    s.joycon_pair_rumble_per_side = flag(OGXM_JOYCON_PAIR_RUMBLE_PER_SIDE);
    if (OGXM_SINGLE_CONTROLLER)
        set_search_times(s, 0, 0);
    else
        set_search_times(s, kDefaultFullSearchS, kSearchNoLimit);
    return s;
}

bool decode(const uint8_t* data, size_t len, Settings& out)
{
    if (data == nullptr || len < 1)
        return false;
    const bool v3 = data[0] == kVersion && len >= sizeof(Settings);
    const bool v2 = data[0] == 2 && len >= sizeof(Settings);
    const bool v1 = data[0] == 1 && len >= kV1Length;
    if (!v3 && !v2 && !v1)
        return false;
    Settings s = defaults();  // older records leave the newer options at their defaults
    s.disconnect_pads_on_mode_change = flag(data[1]);
    s.joycon_pair_imu_right = flag(data[2]);
    s.joycon_pair_horizontal = flag(data[3]);
    s.joycon_solo_horizontal = flag(data[4]);
    s.mac_per_controller = flag(data[5]);
    s.ps4_legacy_motion_scale = flag(data[6]);
    if (v3 || v2) {
        s.joycon_pair_rumble_per_side = flag(data[8]);
        s.combo_disabled_modes = static_cast<uint32_t>(data[12]) | (static_cast<uint32_t>(data[13]) << 8) |
                                 (static_cast<uint32_t>(data[14]) << 16) | (static_cast<uint32_t>(data[15]) << 24);
    }
    if (v3) {
        const uint16_t full = static_cast<uint16_t>(data[9] | ((data[10] & 0x0F) << 8));
        const uint16_t reduced = static_cast<uint16_t>((data[10] >> 4) | (data[11] << 4));
        set_search_times(s, full, reduced);
    } else if (data[7]) {
        set_search_times(s, 0, 0);  // the single controller option: no search with a pad
    } else {
        set_search_times(s, kDefaultFullSearchS, kSearchNoLimit);
    }
    out = s;
    return true;
}

uint16_t full_search_s(const Settings& settings)
{
    return static_cast<uint16_t>(settings.search_times[0] | ((settings.search_times[1] & 0x0F) << 8));
}

uint16_t reduced_search_s(const Settings& settings)
{
    return static_cast<uint16_t>((settings.search_times[1] >> 4) | (settings.search_times[2] << 4));
}

void set_search_times(Settings& settings, uint16_t full_s, uint16_t reduced_s)
{
    if (full_s > kMaxSearchS) full_s = kMaxSearchS;
    if (reduced_s != kSearchNoLimit && reduced_s > kMaxSearchS) reduced_s = kMaxSearchS;
    settings.search_times[0] = static_cast<uint8_t>(full_s & 0xFF);
    settings.search_times[1] = static_cast<uint8_t>(((full_s >> 8) & 0x0F) | ((reduced_s & 0x0F) << 4));
    settings.search_times[2] = static_cast<uint8_t>(reduced_s >> 4);
}

bool mode_combo_enabled(const Settings& settings, uint8_t driver)
{
    return driver >= 32 || (settings.combo_disabled_modes & (1u << driver)) == 0;
}

bool same_except_live(const Settings& a, const Settings& b)
{
    Settings x = a;
    Settings y = b;
    x.combo_disabled_modes = y.combo_disabled_modes = 0;
    for (int i = 0; i < 3; ++i) x.search_times[i] = y.search_times[i] = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&x);
    const uint8_t* q = reinterpret_cast<const uint8_t*>(&y);
    for (size_t i = 0; i < sizeof(Settings); ++i)
        if (p[i] != q[i]) return false;
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
