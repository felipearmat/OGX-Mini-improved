// I2C link of the RP2040 + ESP32 hybrid (board ESP32_BLUEPAD32_I2C): both firmwares build
// the wire format from Firmware/common/I2C/Esp32Bridge.h. This pins it byte by byte, plus the
// values that cross the link or sit in flash on either side (mode IDs, button bits), so an edit
// that would break a dongle flashed from an older tree fails here instead of on hardware.
#include <cstddef>
#include <cstdint>

#include "I2C/Esp32Bridge.h"
#include "I2C/ModeSync.h"
#include "Gamepad/GamepadBits.h"
#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"
#include "UserSettings/ModeCombos.h"
#include "Gamepad/MotionSource.h"
#include "test.h"

using namespace Esp32Bridge;

TEST(packet_in_layout) {
    CHECK_EQ(sizeof(PacketIn), 32u);
    CHECK_EQ(offsetof(PacketIn, packet_len), 0u);
    CHECK_EQ(offsetof(PacketIn, packet_id), 1u);
    CHECK_EQ(offsetof(PacketIn, index), 2u);
    CHECK_EQ(offsetof(PacketIn, device_driver), 3u);
    CHECK_EQ(offsetof(PacketIn, dpad), 4u);
    CHECK_EQ(offsetof(PacketIn, buttons), 5u);
    CHECK_EQ(offsetof(PacketIn, trigger_l), 7u);
    CHECK_EQ(offsetof(PacketIn, trigger_r), 8u);
    CHECK_EQ(offsetof(PacketIn, joystick_lx), 9u);
    CHECK_EQ(offsetof(PacketIn, joystick_ly), 11u);
    CHECK_EQ(offsetof(PacketIn, joystick_rx), 13u);
    CHECK_EQ(offsetof(PacketIn, joystick_ry), 15u);
    CHECK_EQ(offsetof(PacketIn, analog), 17u);
    CHECK_EQ(offsetof(PacketIn, bt_slot), 27u);
    CHECK_EQ(offsetof(PacketIn, counter_value), 28u);
    CHECK_EQ(offsetof(PacketIn, counter_bits), 29u);
    CHECK_EQ(offsetof(PacketIn, counter_unit), 30u);
}

TEST(packet_out_layout) {
    CHECK_EQ(sizeof(PacketOut), 8u);
    CHECK_EQ(offsetof(PacketOut, packet_len), 0u);
    CHECK_EQ(offsetof(PacketOut, packet_id), 1u);
    CHECK_EQ(offsetof(PacketOut, index), 2u);
    CHECK_EQ(offsetof(PacketOut, rumble_l), 3u);
    CHECK_EQ(offsetof(PacketOut, rumble_r), 4u);
}

TEST(packet_defaults) {
    // The RP2040 answers a read with packet_len bytes: it must be the full packet.
    const PacketIn in;
    const PacketOut out;
    CHECK_EQ(in.packet_len, sizeof(PacketIn));
    CHECK_EQ(static_cast<int>(in.packet_id), static_cast<int>(PacketID::SET_PAD));
    CHECK_EQ(out.packet_len, sizeof(PacketOut));
    CHECK_EQ(static_cast<int>(out.packet_id), static_cast<int>(PacketID::GET_PAD));
    CHECK_EQ(SLAVE_ADDR, 0x01);
}

TEST(packet_ids_are_stable) {
    CHECK_EQ(static_cast<int>(PacketID::UNKNOWN), 0);
    CHECK_EQ(static_cast<int>(PacketID::SET_PAD), 1);
    CHECK_EQ(static_cast<int>(PacketID::GET_PAD), 2);
    CHECK_EQ(static_cast<int>(PacketID::SET_DRIVER), 3);
    CHECK_EQ(static_cast<int>(PacketID::SET_PAD_EXT), 4);
    CHECK_EQ(static_cast<int>(PacketID::GET_EXT), 5);
    CHECK_EQ(static_cast<int>(PacketID::SET_DIAG), 6);
    CHECK_EQ(static_cast<int>(PacketID::GET_PROFILE), 7);
    CHECK_EQ(static_cast<int>(PacketID::SET_PROFILE), 8);
    CHECK_EQ(static_cast<int>(PacketID::GET_BLOB), 9);
    CHECK_EQ(static_cast<int>(PacketID::SET_BLOB), 10);
    CHECK_EQ(static_cast<int>(PacketID::SET_PAD_RAW), 11);
    CHECK_EQ(sizeof(PacketPadRaw), 32u);
    CHECK_EQ(offsetof(PacketPadRaw, flags), 3u);
    CHECK_EQ(offsetof(PacketPadRaw, core), 4u);
}

TEST(packet_in_ext_layout) {
    // Version 2: motion / touchpad / battery; fits the RP2040's 32-byte receive buffer.
    const PacketInExt ext;
    CHECK_EQ(sizeof(PacketInExt), sizeof(PacketIn));
    CHECK_EQ(ext.packet_len, sizeof(PacketInExt));
    CHECK_EQ(static_cast<int>(ext.packet_id), static_cast<int>(PacketID::SET_PAD_EXT));
    CHECK_EQ(offsetof(PacketInExt, index), 2u);
    CHECK_EQ(offsetof(PacketInExt, motion_source), 3u);
    CHECK_EQ(offsetof(PacketInExt, accel), 4u);
    CHECK_EQ(offsetof(PacketInExt, gyro), 10u);
    CHECK_EQ(offsetof(PacketInExt, touch_raw), 16u);
    CHECK_EQ(offsetof(PacketInExt, flags), 24u);
    CHECK_EQ(offsetof(PacketInExt, battery), 25u);
    CHECK_EQ(PacketInExt::TOUCHPAD_VALID, 0x01);
    CHECK_EQ(PacketInExt::TOUCHPAD_CLICK, 0x02);
    CHECK_EQ(PacketInExt::JOYCON_LEFT, 0x04);
    CHECK_EQ(PacketInExt::JOYCON_RIGHT, 0x08);
    CHECK_EQ(PacketInExt::JOYCON_PAIRED, 0x10);
}

TEST(packet_out_ext_layout) {
    const PacketOutExt ext;
    CHECK_EQ(sizeof(PacketOutExt), 37u);
    CHECK_EQ(offsetof(PacketOutExt, driver), 30u);
    CHECK_EQ(offsetof(PacketOutExt, active_profile), 31u);
    CHECK_EQ(offsetof(PacketOutExt, profiles_crc), 32u);
    CHECK_EQ(offsetof(PacketOutExt, mode_rgb), 34u);
    CHECK_EQ(ext.packet_len, sizeof(PacketOutExt));
    CHECK_EQ(static_cast<int>(ext.packet_id), static_cast<int>(PacketID::GET_EXT));
    CHECK_EQ(offsetof(PacketOutExt, flags), 3u);
    CHECK_EQ(offsetof(PacketOutExt, lightbar_r), 4u);
    CHECK_EQ(offsetof(PacketOutExt, settings), 7u);
    CHECK_EQ(ext.settings[0], 0);  // not sent: the ESP32 keeps its defaults
    CHECK_EQ(PacketOutExt::LIGHTBAR_VALID, 0x01);
    CHECK_EQ(PacketOutExt::MOTION_OUTPUT, 0x02);
}

TEST(packet_diag_layout) {
    const PacketDiag diag;
    CHECK_EQ(sizeof(PacketDiag), sizeof(PacketIn));
    CHECK_EQ(diag.packet_len, sizeof(PacketDiag));
    CHECK_EQ(static_cast<int>(diag.packet_id), static_cast<int>(PacketID::SET_DIAG));
    CHECK_EQ(offsetof(PacketDiag, kind), 2u);
    CHECK_EQ(offsetof(PacketDiag, slot), 3u);
    CHECK_EQ(offsetof(PacketDiag, data), 4u);
    CHECK_EQ(PacketDiag::SLOT_NAME, 1);
    CHECK_EQ(PacketDiag::SLOT_READY, 2);
    CHECK_EQ(PacketDiag::SLOT_GONE, 3);
    CHECK_EQ(PacketDiag::BATTERY, 4);
    CHECK_EQ(PacketDiag::EVENT, 5);
    CHECK_EQ(PacketDiag::SEARCHING, 6);
    CHECK_EQ(PacketDiag::ADAPTER_ADDRESS, 7);
    CHECK_EQ(PacketDiag::SWITCH_FIRMWARE, 8);
    CHECK_EQ(PacketDiag::LE_ADV_REPORTS, 19);
    CHECK_EQ(PacketDiag::COMPANION_SLOT, 20);
    CHECK_EQ(PacketDiag::COMPANION_INFO, 21);
    CHECK_EQ(PacketDiag::COMPANION_VERSION, 22);
}

TEST(packet_profile_layout) {
    // A whole profile fits the chunks; each chunk is one 32-byte transfer.
    CHECK_EQ(sizeof(PacketProfile), 32u);
    CHECK_EQ(offsetof(PacketProfile, profile_id), 2u);
    CHECK_EQ(offsetof(PacketProfile, chunk), 3u);
    CHECK_EQ(offsetof(PacketProfile, player_idx), 4u);
    CHECK_EQ(offsetof(PacketProfile, driver), 5u);
    CHECK_EQ(offsetof(PacketProfile, data), 6u);
    CHECK(PacketProfile::kChunks * PacketProfile::kData >= sizeof(UserProfile));
    CHECK((PacketProfile::kChunks - 1) * PacketProfile::kData < sizeof(UserProfile));
}

TEST(packet_blob_layout) {
    CHECK_EQ(sizeof(PacketBlob), 32u);
    CHECK_EQ(offsetof(PacketBlob, kind), 2u);
    CHECK_EQ(offsetof(PacketBlob, chunk), 3u);
    CHECK_EQ(offsetof(PacketBlob, total_len), 4u);
    CHECK_EQ(offsetof(PacketBlob, data), 6u);
    CHECK_EQ(PacketBlob::DONGLE_SETTINGS, 1);
    CHECK_EQ(PacketBlob::KBM_SETTINGS, 2);
    CHECK(sizeof(dongle_settings::Settings) <= PacketBlob::kMaxLen);
}

TEST(profiles_crc_changes_with_a_profile) {
    UserProfile profiles[2];
    profiles[0].id = 1;
    profiles[1].id = 2;
    const uint16_t before = profiles_crc(profiles, 2);
    profiles[1].button_a = GamepadBits::BUTTON_B;
    CHECK(before != 0);
    CHECK(profiles_crc(profiles, 2) != before);
}

TEST(motion_sources_are_stable) {
    CHECK_EQ(motion_source::NONE, 0);
    CHECK_EQ(motion_source::DS4, 1);
    CHECK_EQ(motion_source::DS5, 2);
    CHECK_EQ(motion_source::SWITCH_PRO, 3);
    CHECK_EQ(motion_source::WII_BT, 7);
}

TEST(motion_saturates_to_16_bits) {
    CHECK_EQ(saturate_i16(40000), 32767);
    CHECK_EQ(saturate_i16(-40000), -32768);
    CHECK_EQ(saturate_i16(-1234), -1234);
}

TEST(mode_ids_are_stable) {
    // Sent by the ESP32 in SET_DRIVER and stored in flash / NVS: values must never move.
    CHECK_EQ(static_cast<int>(DeviceDriverType::NONE), 0);
    CHECK_EQ(static_cast<int>(DeviceDriverType::XBOXOG), 1);
    CHECK_EQ(static_cast<int>(DeviceDriverType::XBOXOG_SB), 2);
    CHECK_EQ(static_cast<int>(DeviceDriverType::XBOXOG_XR), 3);
    CHECK_EQ(static_cast<int>(DeviceDriverType::XINPUT), 4);
    CHECK_EQ(static_cast<int>(DeviceDriverType::PS3), 5);
    CHECK_EQ(static_cast<int>(DeviceDriverType::DINPUT_DEBUG), 6);
    CHECK_EQ(static_cast<int>(DeviceDriverType::PSCLASSIC), 7);
    CHECK_EQ(static_cast<int>(DeviceDriverType::SWITCH), 8);
    CHECK_EQ(static_cast<int>(DeviceDriverType::WIIU), 9);
    CHECK_EQ(static_cast<int>(DeviceDriverType::WII), 10);
    CHECK_EQ(static_cast<int>(DeviceDriverType::PS1PS2), 11);
    CHECK_EQ(static_cast<int>(DeviceDriverType::GAMECUBE), 12);
    CHECK_EQ(static_cast<int>(DeviceDriverType::DREAMCAST), 13);
    CHECK_EQ(static_cast<int>(DeviceDriverType::N64), 14);
    CHECK_EQ(static_cast<int>(DeviceDriverType::PS4), 15);
    CHECK_EQ(static_cast<int>(DeviceDriverType::STEAM), 16);
    CHECK_EQ(static_cast<int>(DeviceDriverType::KBM), 17);
    CHECK_EQ(static_cast<int>(DeviceDriverType::WEBAPP), 100);
    CHECK_EQ(static_cast<int>(DeviceDriverType::UART_BRIDGE), 101);
}

TEST(button_bits_are_stable) {
    // Cross the link as-is (PacketIn.dpad / buttons) and are stored in profiles.
    CHECK_EQ(GamepadBits::DPAD_UP, 0x01);
    CHECK_EQ(GamepadBits::DPAD_DOWN, 0x02);
    CHECK_EQ(GamepadBits::DPAD_LEFT, 0x04);
    CHECK_EQ(GamepadBits::DPAD_RIGHT, 0x08);
    CHECK_EQ(GamepadBits::BUTTON_A, 0x0001);
    CHECK_EQ(GamepadBits::BUTTON_B, 0x0002);
    CHECK_EQ(GamepadBits::BUTTON_X, 0x0004);
    CHECK_EQ(GamepadBits::BUTTON_Y, 0x0008);
    CHECK_EQ(GamepadBits::BUTTON_L3, 0x0010);
    CHECK_EQ(GamepadBits::BUTTON_R3, 0x0020);
    CHECK_EQ(GamepadBits::BUTTON_BACK, 0x0040);
    CHECK_EQ(GamepadBits::BUTTON_START, 0x0080);
    CHECK_EQ(GamepadBits::BUTTON_LB, 0x0100);
    CHECK_EQ(GamepadBits::BUTTON_RB, 0x0200);
    CHECK_EQ(GamepadBits::BUTTON_SYS, 0x0400);
    CHECK_EQ(GamepadBits::BUTTON_MISC, 0x0800);
    CHECK_EQ(GamepadBits::ANALOG_OFF_UP, 0);
    CHECK_EQ(GamepadBits::ANALOG_OFF_RB, 9);
}

TEST(mode_combos_are_unique) {
    // Both firmwares read the combos from this table; two modes on one combo would be ambiguous.
    for (size_t i = 0; i < mode_combos::MODE_COMBOS.size(); ++i)
        for (size_t j = i + 1; j < mode_combos::MODE_COMBOS.size(); ++j)
            CHECK(mode_combos::MODE_COMBOS[i].combo != mode_combos::MODE_COMBOS[j].combo);
    for (const auto& entry : mode_combos::MODE_COMBOS)
        CHECK(entry.combo & (static_cast<uint32_t>(GamepadBits::BUTTON_START) << 16));  // every combo holds Start
}

TEST(hybrid_modes_are_reachable_and_usb_only) {
    // The ESP32 only switches to a mode it can reach by combo, and the RP2040 side of the hybrid
    // must not enter a GPIO mode (its pins carry the I2C link) or Wii (needs the Pico W radio).
    CHECK_EQ(static_cast<int>(mode_combos::HYBRID_MODES[0]), static_cast<int>(DeviceDriverType::XBOXOG));  // default
    for (DeviceDriverType mode : mode_combos::HYBRID_MODES) {
        bool has_combo = false;
        for (const auto& entry : mode_combos::MODE_COMBOS)
            has_combo |= (entry.driver == mode);
        CHECK(has_combo);
        CHECK(mode != DeviceDriverType::PS1PS2 && mode != DeviceDriverType::GAMECUBE &&
              mode != DeviceDriverType::DREAMCAST && mode != DeviceDriverType::N64 &&
              mode != DeviceDriverType::WII);
    }
}

// ---- Who decides the mode at start-up (I2C/ModeSync.h) ----

namespace {

using D = DeviceDriverType;

/* The RP2040 side: its stored mode, how many PacketOutExt it has sent, its restarts. */
struct Rp2040 {
    D mode;
    bool keeps_settings{true};
    uint8_t replies{0};
    int restarts{0};
    void restart_into(D d) { mode = normalized(d); replies = 0; ++restarts; }
    bool answer_get_ext() {  // false: an older RP2040 that never answers it
        if (!keeps_settings) return false;
        if (replies < mode_sync::kRepliesBeforeRequests) ++replies;
        return true;
    }
    void set_driver(D d, bool from_combo) {
        if (keeps_settings ? mode_sync::rp2040_accepts(replies, d, mode, from_combo) : normalized(d) != mode)
            restart_into(d);
    }
};

/* The ESP32 side, freshly started with `flash` stored: one tick every 50 ms, each asking GET_EXT
 * (answered after `ext_delay_ms`) and, per the rule, sending its own mode. `lost_ms`: the RP2040
 * answers, but the ESP32 does not get the answers until then (reads cut on a lossy link). */
void run_esp32(Rp2040& rp, D flash, uint32_t ms, uint32_t ext_delay_ms = 50, uint32_t lost_ms = 0) {
    bool remote = false;
    for (uint32_t t = 0; t < ms; t += 50) {
        if (t >= ext_delay_ms && rp.answer_get_ext() && t >= lost_ms) remote = true;
        if (mode_sync::esp32_sends_own_mode(remote, t)) rp.set_driver(flash, false);
    }
}

}  // namespace

TEST(a_fresh_esp32_does_not_undo_the_mode_the_rp2040_restarted_in) {
    // The bug: the mode combo restarted the RP2040 in D-Input/Debug; the ESP32, still holding
    // Switch in its flash, sent it before reading the RP2040's answer and the RP2040 went back.
    Rp2040 rp{D::DINPUT_DEBUG};
    run_esp32(rp, D::SWITCH, 10000);
    CHECK(rp.mode == D::DINPUT_DEBUG);
    CHECK_EQ(rp.restarts, 0);
    // Even when the RP2040's first answer is late (it is still starting USB).
    Rp2040 slow{D::DINPUT_DEBUG};
    run_esp32(slow, D::SWITCH, 10000, 1500);
    CHECK(slow.mode == D::DINPUT_DEBUG);
}

TEST(an_esp32_that_misses_the_first_answers_does_not_change_the_mode) {
    // 2026-10-22: the ESP32 got the RP2040's first answer only after about 10 s (reads cut on the
    // I2C link); at 3 s it sent the mode in its own flash (STEAM) and the adapter switched to it.
    Rp2040 rp{D::DINPUT_DEBUG};
    run_esp32(rp, D::STEAM, 15000, 50, 10000);
    CHECK(rp.mode == D::DINPUT_DEBUG);
    CHECK_EQ(rp.restarts, 0);
}

TEST(an_rp2040_that_keeps_no_settings_takes_the_esp32s_mode) {
    Rp2040 old{D::XBOXOG};
    old.keeps_settings = false;
    run_esp32(old, D::SWITCH, 2000);
    CHECK(old.mode == D::XBOXOG);  // not before the wait
    run_esp32(old, D::SWITCH, 10000);
    CHECK(old.mode == D::SWITCH);
}

TEST(a_combo_on_the_esp32_changes_the_mode_once_in_sync) {
    Rp2040 rp{D::PS4};
    run_esp32(rp, D::PS4, 1000);
    rp.set_driver(D::DINPUT_DEBUG, true);  // Start + RB + Left
    CHECK(rp.mode == D::DINPUT_DEBUG);
    CHECK_EQ(rp.restarts, 1);
    // An older ESP32 asking for the Web App mode means DInput: no restart loop.
    rp.answer_get_ext();
    rp.answer_get_ext();
    rp.set_driver(D::WEBAPP, true);
    CHECK_EQ(rp.restarts, 1);
}

TEST(mode_requests_wait_for_the_rp2040s_answers) {
    CHECK(!mode_sync::rp2040_accepts(0, D::SWITCH, D::DINPUT_DEBUG, true));
    CHECK(!mode_sync::rp2040_accepts(1, D::SWITCH, D::DINPUT_DEBUG, true));
    CHECK(mode_sync::rp2040_accepts(2, D::SWITCH, D::DINPUT_DEBUG, true));
    CHECK(!mode_sync::rp2040_accepts(2, D::NONE, D::DINPUT_DEBUG, true));
    CHECK(!mode_sync::rp2040_accepts(2, D::SWITCH, D::DINPUT_DEBUG, false));  // not a combo
    CHECK(!mode_sync::esp32_sends_own_mode(false, 100));
    CHECK(!mode_sync::esp32_sends_own_mode(true, 60000));
    CHECK(mode_sync::esp32_sends_own_mode(false, mode_sync::kNoSettingsRp2040Ms));
}

TEST(gyro_crosses_the_link_without_saturating) {
    // A Joy-Con turning at 300 deg/s (Bluepad32: raw counts * 1000, raw = deg/s * 13371 / 936) and a
    // DS4 at 500 deg/s (1024 per deg/s) used to saturate at 32767 (about 2.3 and 32 deg/s).
    const int32_t joycon = static_cast<int32_t>(300.0 * 13371.0 / 936.0 * 1000.0);
    const int16_t jw = Esp32Bridge::gyro_to_wire(joycon, motion_source::SWITCH_PRO);
    CHECK(jw > 4200 && jw < 4400);
    const int32_t jback = Esp32Bridge::gyro_from_wire(jw, motion_source::SWITCH_PRO, true);
    CHECK(jback > joycon - 1000 && jback < joycon + 1000);  // within one raw count
    const int32_t ds4 = 500 * 1024;
    const int16_t dw = Esp32Bridge::gyro_to_wire(-ds4, motion_source::DS4);
    CHECK_EQ(dw, -8000);
    CHECK_EQ(Esp32Bridge::gyro_from_wire(dw, motion_source::DS4, true), -ds4);
    // An older ESP32 (no GYRO_SCALED): read as it sent it.
    CHECK_EQ(Esp32Bridge::gyro_from_wire(1234, motion_source::SWITCH_PRO, false), 1234);
    // Sources whose gyro already fits: unchanged.
    CHECK_EQ(Esp32Bridge::gyro_to_wire(77, motion_source::WII_BT), 77);
}

TEST_MAIN()
