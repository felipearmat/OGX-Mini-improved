#include "UserSettings/DongleSettings.h"
#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"

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
#ifndef OGXM_FULL_SEARCH_S
#define OGXM_FULL_SEARCH_S 60
#endif
#ifndef OGXM_REDUCED_SEARCH_S
#define OGXM_REDUCED_SEARCH_S 0xFFF  // no limit
#endif
#ifndef OGXM_COMBO_DISABLED_MODES
#define OGXM_COMBO_DISABLED_MODES 0
#endif
#ifndef OGXM_HIGH_TX_POWER
#define OGXM_HIGH_TX_POWER 0
#endif
#ifndef OGXM_STATUS_LED_PICO
#define OGXM_STATUS_LED_PICO 1
#endif
#ifndef OGXM_STATUS_LED_PICO_EXT
#define OGXM_STATUS_LED_PICO_EXT 1
#endif
#ifndef OGXM_STATUS_LED_ESP32
#define OGXM_STATUS_LED_ESP32 1
#endif
#ifndef OGXM_STATUS_LED_ESP32_EXT
#define OGXM_STATUS_LED_ESP32_EXT 1
#endif
#ifndef OGXM_EXT_RGB_BRIGHTNESS
#define OGXM_EXT_RGB_BRIGHTNESS 48
#endif
static_assert(OGXM_EXT_RGB_BRIGHTNESS >= 1 && OGXM_EXT_RGB_BRIGHTNESS <= 255, "OGXM_EXT_RGB_BRIGHTNESS: 1-255");
#ifndef OGXM_FLASH_LOG
#define OGXM_FLASH_LOG 0
#endif
#ifndef OGXM_DIAGNOSTICS_LINK
#define OGXM_DIAGNOSTICS_LINK 0
#endif
#ifndef OGXM_IDLE_OFF_MINUTES
#define OGXM_IDLE_OFF_MINUTES 15
#endif
static_assert(OGXM_IDLE_OFF_MINUTES >= 0 && OGXM_IDLE_OFF_MINUTES <= 255, "OGXM_IDLE_OFF_MINUTES: 0-255");

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
        set_search_times(s, OGXM_FULL_SEARCH_S, OGXM_REDUCED_SEARCH_S);
    s.combo_disabled_modes = static_cast<uint32_t>(OGXM_COMBO_DISABLED_MODES);
    s.idle_off_minutes = static_cast<uint8_t>(OGXM_IDLE_OFF_MINUTES);
    s.high_tx_power = flag(OGXM_HIGH_TX_POWER);
    s.status_led_pico = flag(OGXM_STATUS_LED_PICO);
    s.status_led_pico_ext = flag(OGXM_STATUS_LED_PICO_EXT);
    s.status_led_esp32 = flag(OGXM_STATUS_LED_ESP32);
    s.status_led_esp32_ext = flag(OGXM_STATUS_LED_ESP32_EXT);
    s.status_led_brightness = static_cast<uint8_t>(OGXM_EXT_RGB_BRIGHTNESS);
    s.keep_log_in_flash = flag(OGXM_FLASH_LOG);
    s.link_diagnostics = flag(OGXM_DIAGNOSTICS_LINK);
    return s;
}

bool decode(const uint8_t* data, size_t len, Settings& out)
{
    if (data == nullptr || len < 1)
        return false;
    const bool v8 = data[0] == kVersion && len >= sizeof(Settings);
    const bool v7 = (data[0] == 7 && len >= sizeof(Settings)) || v8;
    const bool v6 = (data[0] == 6 && len >= kV6Length) || v7;
    const bool v5 = (data[0] == 5 && len >= kV5Length) || v6;
    const bool v4 = (data[0] == 4 && len >= kV4Length) || v5;
    const bool v3 = (data[0] == 3 && len >= kV3Length) || v4;
    const bool v2 = data[0] == 2 && len >= kV3Length;
    const bool v1 = data[0] == 1 && len >= kV1Length;
    if (!v4 && !v3 && !v2 && !v1)
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
    }
    if (v4)
        s.idle_off_minutes = data[16];
    if (v5)
        s.high_tx_power = flag(data[17]);
    if (v6) {
        s.status_led_pico = flag(data[18]);
        s.status_led_pico_ext = flag(data[19]);
        s.status_led_esp32 = flag(data[20]);
        s.status_led_esp32_ext = flag(data[21]);
        if (data[22] != 0)
            s.status_led_brightness = data[22];
        s.link_diagnostics = flag(data[7]);  // version 8; zero in older version 6 / 7 records
    }
    if (v7)
        s.keep_log_in_flash = flag(data[23]);
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
    /* D-Input/Debug is the way back to the web app: its combos are always on. */
    if (driver == static_cast<uint8_t>(DeviceDriverType::DINPUT_DEBUG))
        return true;
    return driver >= 32 || (settings.combo_disabled_modes & (1u << driver)) == 0;
}

bool same_except_live(const Settings& a, const Settings& b)
{
    Settings x = a;
    Settings y = b;
    x.combo_disabled_modes = y.combo_disabled_modes = 0;
    for (int i = 0; i < 3; ++i) x.search_times[i] = y.search_times[i] = 0;
    x.idle_off_minutes = y.idle_off_minutes = 0;
    x.high_tx_power = y.high_tx_power = 0;
    /* The status LED options restart the adapter (2026-10-23): live, the hybrid's ESP32 took
     * seconds to get them over a busy I2C link; after a restart both boards start with them. */
    x.keep_log_in_flash = y.keep_log_in_flash = 0;
    x.link_diagnostics = y.link_diagnostics = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&x);
    const uint8_t* q = reinterpret_cast<const uint8_t*>(&y);
    for (size_t i = 0; i < sizeof(Settings); ++i)
        if (p[i] != q[i]) return false;
    return true;
}

void encode_v6(const Settings& settings, uint8_t* out)
{
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&settings);
    for (size_t i = 0; i < kV6Length; ++i)
        out[i] = p[i];
    out[0] = 6;
    out[7] = controller_diagnostics(settings) ? 1 : 0;  // the record has no byte 23 (log in flash)
}

bool controller_diagnostics(const Settings& settings)
{
    return settings.link_diagnostics != 0 && settings.keep_log_in_flash != 0;
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
