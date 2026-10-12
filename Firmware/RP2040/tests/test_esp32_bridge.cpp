// I2C link of the RP2040 + ESP32 hybrid (board ESP32_BLUEPAD32_I2C): both firmwares build
// the wire format from Firmware/common/I2C/Esp32Bridge.h. This pins it byte by byte, plus the
// values that cross the link or sit in flash on either side (mode IDs, button bits), so an edit
// that would break a dongle flashed from an older tree fails here instead of on hardware.
#include <cstddef>
#include <cstdint>

#include "I2C/Esp32Bridge.h"
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
    CHECK_EQ(offsetof(PacketIn, reserved), 27u);
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
    CHECK_EQ(sizeof(PacketOutExt), 25u);
    CHECK_EQ(ext.packet_len, sizeof(PacketOutExt));
    CHECK_EQ(static_cast<int>(ext.packet_id), static_cast<int>(PacketID::GET_EXT));
    CHECK_EQ(offsetof(PacketOutExt, flags), 3u);
    CHECK_EQ(offsetof(PacketOutExt, lightbar_r), 4u);
    CHECK_EQ(offsetof(PacketOutExt, settings), 7u);
    CHECK_EQ(ext.settings.version, 0);  // not sent: the ESP32 keeps its defaults
    CHECK_EQ(PacketOutExt::LIGHTBAR_VALID, 0x01);
    CHECK_EQ(PacketOutExt::MOTION_OUTPUT, 0x02);
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
    CHECK_EQ(static_cast<int>(DeviceDriverType::DINPUT), 6);
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

TEST_MAIN()
