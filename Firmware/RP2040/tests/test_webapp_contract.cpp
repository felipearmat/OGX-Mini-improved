// What the web app must agree on with the firmware, written from the firmware's own code into
// Firmware/common/contract/webapp_contract.json: packet IDs, output mode values, capability
// bits, the dongle settings record (length, field offsets, defaults, how each older version
// decodes) and the colour per mode. This test fails when the committed file no longer matches
// the code; `test_webapp_contract --write` rewrites it. The web app's tests check their own
// constants against the same file (OGX-Mini-improved-WebApp, tests/contract.test.js).
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Board/ModeIndicator.h"
#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"
#include "UserSettings/Capabilities.h"
#include "UserSettings/DongleSettings.h"
#include "UserSettings/UserProfile.h"
#include "USBDevice/DeviceDriver/KBM/KbmSettings.h"
#include "UserSettings/WebAppProtocol.h"
#include "test.h"

#ifndef OGXM_CONTRACT_PATH
#error "OGXM_CONTRACT_PATH: the committed contract file"
#endif

namespace {

using webapp_protocol::PacketID;

struct Named {
    const char* name;
    unsigned value;
};

std::string bytes_json(const uint8_t* data, size_t len) {
    std::string out = "[";
    for (size_t i = 0; i < len; ++i) {
        out += std::to_string(data[i]);
        if (i + 1 < len) out += ", ";
    }
    return out + "]";
}

std::string object_json(const std::vector<Named>& items, const char* indent) {
    std::string out = "{\n";
    for (size_t i = 0; i < items.size(); ++i) {
        out += std::string(indent) + "  \"" + items[i].name + "\": " + std::to_string(items[i].value);
        out += i + 1 < items.size() ? ",\n" : "\n";
    }
    return out + indent + "}";
}

#define N(ns, x) Named{#x, static_cast<unsigned>(ns::x)}

const std::vector<Named>& modes() {
    using D = DeviceDriverType;
    static const std::vector<Named> list = {
        N(D, XBOXOG), N(D, XBOXOG_SB), N(D, XBOXOG_XR), N(D, XINPUT), N(D, PS3), N(D, DINPUT_DEBUG),
        N(D, PSCLASSIC), N(D, SWITCH), N(D, WIIU), N(D, WII), N(D, PS1PS2), N(D, GAMECUBE),
        N(D, DREAMCAST), N(D, N64), N(D, PS4), N(D, STEAM), N(D, KBM), N(D, WEBAPP),
    };
    static_assert(static_cast<int>(DeviceDriverType::KBM) == 17, "a new mode: add it to the contract");
    return list;
}

std::string contract_json() {
    using dongle_settings::Settings;
    std::ostringstream o;
    o << "{\n";

    // Packets
    const std::vector<Named> ids = {
        N(PacketID, GET_PROFILE_BY_ID), N(PacketID, GET_PROFILE_BY_IDX), N(PacketID, SET_PROFILE_START),
        N(PacketID, SET_PROFILE), N(PacketID, GET_DONGLE_SETTINGS), N(PacketID, SET_DONGLE_SETTINGS),
        N(PacketID, GET_KBM_SETTINGS), N(PacketID, SET_KBM_SETTINGS), N(PacketID, GET_DIAGNOSTICS),
        N(PacketID, SYSTEM), N(PacketID, GET_CAPABILITIES), N(PacketID, GET_VERSION),
        N(PacketID, GET_LED_COLORS), N(PacketID, SET_LED_COLORS), N(PacketID, LED_PREVIEW), N(PacketID, GET_LOG), N(PacketID, GET_FLASH_LOG),
        N(PacketID, SET_GP_IN), N(PacketID, SET_GP_OUT), N(PacketID, GP_TOUCH), N(PacketID, GP_MOTION), N(PacketID, RESP_ERROR),
    };
    const std::vector<Named> system = {
        N(webapp_protocol, SYSTEM_BOOTSEL), N(webapp_protocol, SYSTEM_REBOOT),
        N(webapp_protocol, SYSTEM_ESP32_FLASH), N(webapp_protocol, SYSTEM_ESP32_MONITOR),
    };
    o << "  \"packet\": {\n"
      << "    \"length\": " << webapp_protocol::kPacketLength << ",\n"
      << "    \"header_length\": " << webapp_protocol::kHeaderLength << ",\n"
      << "    \"log_bytes\": " << webapp_protocol::kLogBytes << ",\n"
      << "    \"ids\": " << object_json(ids, "    ") << ",\n"
      << "    \"system\": " << object_json(system, "    ") << "\n"
      << "  },\n";

    // Output modes (WEBAPP: the former Web App mode, read as DINPUT)
    o << "  \"modes\": " << object_json(modes(), "  ") << ",\n";
    o << "  \"mode_aliases\": { \"WEBAPP\": " << static_cast<unsigned>(normalized(DeviceDriverType::WEBAPP)) << " },\n";

    // Capability bits
    namespace c = capabilities;
    const std::vector<Named> caps = {
        N(c, BLUETOOTH), N(c, HYBRID), N(c, HIGH_TX_POWER), N(c, JOYCON), N(c, MAC_PER_CONTROLLER),
        N(c, DISCONNECT_ON_MODE_CHANGE), N(c, SEARCH_TIMES), N(c, IDLE_OFF), N(c, WIRED_INPUT),
        N(c, BLE_WEBAPP), N(c, STATUS_LED_PICO), N(c, STATUS_LED_ESP32), N(c, STATUS_LED_PICO_EXT),
        N(c, MODE_COLORS), N(c, STATUS_LED_ESP32_EXT), N(c, FLASH_LOG), N(c, MOTION_STREAM), N(c, kPicoW), N(c, kHybrid),
    };
    o << "  \"capabilities\": " << object_json(caps, "  ") << ",\n";

    // Dongle settings record
#define F(field) Named{#field, static_cast<unsigned>(offsetof(Settings, field))}
    const std::vector<Named> offsets = {
        F(version), F(disconnect_pads_on_mode_change), F(joycon_pair_imu_right), F(joycon_pair_horizontal),
        F(joycon_solo_horizontal), F(mac_per_controller), F(ps4_legacy_motion_scale), F(link_diagnostics),
        F(joycon_pair_rumble_per_side), F(search_times), F(combo_disabled_modes), F(idle_off_minutes),
        F(high_tx_power), F(status_led_pico), F(status_led_pico_ext), F(status_led_esp32),
        F(status_led_esp32_ext), F(status_led_brightness), F(keep_log_in_flash),
    };
#undef F
    const Settings defaults = dongle_settings::defaults();
    o << "  \"dongle_settings\": {\n"
      << "    \"version\": " << unsigned(dongle_settings::kVersion) << ",\n"
      << "    \"length\": " << sizeof(Settings) << ",\n"
      << "    \"lengths\": { \"1\": " << dongle_settings::kV1Length << ", \"2\": " << dongle_settings::kV3Length
      << ", \"3\": " << dongle_settings::kV3Length << ", \"4\": " << dongle_settings::kV4Length
      << ", \"5\": " << dongle_settings::kV5Length << ", \"6\": " << dongle_settings::kV6Length
      << ", \"7\": " << sizeof(Settings) << ", \"8\": " << sizeof(Settings) << " },\n"
      << "    \"offsets\": " << object_json(offsets, "    ") << ",\n"
      << "    \"defaults\": " << bytes_json(reinterpret_cast<const uint8_t*>(&defaults), sizeof(defaults)) << ",\n";

    // How a record of each version decodes (the current record it becomes).
    std::vector<std::vector<uint8_t>> inputs = {
        {1, 1, 0, 1, 0, 1, 0, 1},                                          // v1, single controller on
        {2, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0x20, 0, 0, 0},               // v2: rumble both, PS3 combo off
        {3, 1, 1, 0, 1, 0, 0, 0, 1, 30, 0x40, 0x01, 0, 0, 0, 0},           // v3: search 30 s / 20 s
        {4, 0, 1, 0, 1, 0, 0, 0, 1, 60, 0xF0, 0xFF, 0, 0, 0, 0, 0},        // v4: idle off never
        {5, 0, 1, 0, 1, 0, 0, 0, 1, 60, 0xF0, 0xFF, 0, 0, 0, 0, 15, 1},    // v5: high transmit power
        {6, 0, 1, 0, 1, 0, 0, 0, 1, 60, 0xF0, 0xFF, 0, 0, 0, 0, 15, 0, 0, 1, 1, 0, 120},  // v6
        {6, 0, 1, 0, 1, 0, 0, 0, 1, 60, 0xF0, 0xFF, 0, 0, 0, 0, 15, 0, 1, 1, 1, 1, 0},    // v6, brightness 0
        {7, 0, 1, 0, 1, 0, 0, 0, 1, 60, 0xF0, 0xFF, 0, 0, 0, 0, 15, 0, 1, 1, 1, 1, 48, 1}, // v7: log in flash
        {8, 0, 1, 0, 1, 0, 0, 1, 1, 60, 0xF0, 0xFF, 0, 0, 0, 0, 15, 0, 1, 1, 1, 1, 48, 0}, // v8: detailed controller diagnostics
    };
    o << "    \"decode\": [\n";
    for (size_t i = 0; i < inputs.size(); ++i) {
        Settings out{};
        const bool ok = dongle_settings::decode(inputs[i].data(), inputs[i].size(), out);
        o << "      { \"input\": " << bytes_json(inputs[i].data(), inputs[i].size()) << ",\n"
          << "        \"output\": " << (ok ? bytes_json(reinterpret_cast<const uint8_t*>(&out), sizeof(out)) : "null")
          << " }" << (i + 1 < inputs.size() ? ",\n" : "\n");
    }
    o << "    ]\n  },\n";

    // Profile (GET / SET_PROFILE): what the web app edits, with the stick and trigger blocks
#define P(field) Named{#field, static_cast<unsigned>(offsetof(UserProfile, field))}
    const std::vector<Named> profile_offsets = {
        P(id), P(joystick_settings_l), P(joystick_settings_r), P(trigger_settings_l), P(trigger_settings_r),
        P(dpad_up), P(dpad_down), P(dpad_left), P(dpad_right), P(button_a), P(button_b), P(button_x),
        P(button_y), P(button_l3), P(button_r3), P(button_back), P(button_start), P(button_lb), P(button_rb),
        P(button_sys), P(button_misc), P(analog_enabled), P(analog_off_up), P(analog_off_down),
        P(analog_off_left), P(analog_off_right), P(analog_off_a), P(analog_off_b), P(analog_off_x),
        P(analog_off_y), P(analog_off_lb), P(analog_off_rb),
    };
#undef P
#define J(field) Named{#field, static_cast<unsigned>(offsetof(JoystickSettingsRaw, field))}
    const std::vector<Named> joystick_offsets = {
        J(dz_inner), J(dz_outer), J(anti_dz_circle), J(anti_dz_circle_y_scale), J(anti_dz_square),
        J(anti_dz_square_y_scale), J(anti_dz_angular), J(anti_dz_outer), J(axis_restrict), J(angle_restrict),
        J(diag_scale_min), J(diag_scale_max), J(curve), J(uncap_radius), J(invert_y), J(invert_x),
    };
#undef J
#define T(field) Named{#field, static_cast<unsigned>(offsetof(TriggerSettingsRaw, field))}
    const std::vector<Named> trigger_offsets = {T(dz_inner), T(dz_outer), T(anti_dz_inner), T(anti_dz_outer), T(curve)};
#undef T
    const UserProfile profile{};
    o << "  \"profile\": {\n"
      << "    \"length\": " << sizeof(UserProfile) << ",\n"
      << "    \"joystick_length\": " << sizeof(JoystickSettingsRaw) << ",\n"
      << "    \"trigger_length\": " << sizeof(TriggerSettingsRaw) << ",\n"
      << "    \"offsets\": " << object_json(profile_offsets, "    ") << ",\n"
      << "    \"joystick_offsets\": " << object_json(joystick_offsets, "    ") << ",\n"
      << "    \"trigger_offsets\": " << object_json(trigger_offsets, "    ") << ",\n"
      << "    \"defaults\": " << bytes_json(reinterpret_cast<const uint8_t*>(&profile), sizeof(profile)) << "\n"
      << "  },\n";

    // Mouse + keyboard mapping (GET / SET_KBM_SETTINGS)
    const kbm_settings::Settings kbm = kbm_settings::defaults();
    o << "  \"kbm_settings\": {\n"
      << "    \"version\": " << unsigned(kbm.version) << ",\n"
      << "    \"length\": " << sizeof(kbm) << ",\n"
      << "    \"defaults\": " << bytes_json(reinterpret_cast<const uint8_t*>(&kbm), sizeof(kbm)) << "\n"
      << "  },\n";

    // Colour per mode
    using mode_indicator::ModeColors;
    o << "  \"mode_colors\": {\n"
      << "    \"version\": " << unsigned(ModeColors::kVersion) << ",\n"
      << "    \"slots\": " << ModeColors::kColorSlots << ",\n"
      << "    \"length\": " << sizeof(ModeColors) << ",\n"
      << "    \"defaults\": {\n";
    const auto& list = modes();
    for (size_t i = 0; i < list.size(); ++i) {
        uint8_t rgb[3];
        mode_indicator::lightbar_color(static_cast<DeviceDriverType>(list[i].value), rgb[0], rgb[1], rgb[2]);
        o << "      \"" << list[i].name << "\": " << bytes_json(rgb, 3) << (i + 1 < list.size() ? ",\n" : "\n");
    }
    o << "    }\n  }\n}\n";
    return o.str();
}

#undef N

std::string read_file(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

TEST(committed_contract_matches_the_firmware) {
    const std::string committed = read_file(OGXM_CONTRACT_PATH);
    const std::string current = contract_json();
    if (committed != current) {
        std::printf("  %s is out of date: run test_webapp_contract --write, then update the web app's copy\n",
                    OGXM_CONTRACT_PATH);
    }
    CHECK(committed == current);
}

TEST(old_web_app_mode_reads_as_dinput) {
    CHECK(normalized(DeviceDriverType::WEBAPP) == DeviceDriverType::DINPUT_DEBUG);
    CHECK(normalized(DeviceDriverType::PS4) == DeviceDriverType::PS4);
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--write") == 0) {
        std::ofstream out(OGXM_CONTRACT_PATH, std::ios::binary);
        out << contract_json();
        std::printf("wrote %s\n", OGXM_CONTRACT_PATH);
        return out ? 0 : 1;
    }
    return test::run_all();
}
