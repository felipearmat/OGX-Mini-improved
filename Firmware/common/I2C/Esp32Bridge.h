#ifndef _ESP32_BRIDGE_H_
#define _ESP32_BRIDGE_H_

#include <cstddef>
#include <cstdint>

#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"
#include "UserSettings/DongleSettings.h"

/* I2C link between the RP2040 (USB side, I2C slave) and the ESP32 companion running
 * Bluepad32 (I2C master): board ESP32_BLUEPAD32_I2C. Both firmwares build from this one
 * header, so a layout change reaches both sides; tests/test_esp32_bridge.cpp pins the
 * wire layout byte by byte (a firmware flashed from an older tree must still talk to it).
 *
 * Version 2 adds two packets, both ignored by a side that predates them:
 *  - SET_PAD_EXT (ESP32 -> RP2040, PacketInExt): motion, touchpad, battery, Joy-Con side. An
 *    older RP2040 drops the unknown ID.
 *  - GET_EXT: the ESP32 writes a PacketIn with this ID, then reads a PacketOutExt (lightbar asked
 *    by the host, the dongle settings kept by the RP2040). The RP2040 answers it on the next read
 *    only; an older RP2040 answers with a PacketOut (GET_PAD), which the ESP32 ignores. */
namespace Esp32Bridge
{
    // RP2040 slave address; with CONFIG_MULTI_SLAVE_MODE the ESP32 adds the pad index.
    static constexpr uint8_t SLAVE_ADDR = 0x01;

    enum class PacketID : uint8_t
    {
        UNKNOWN = 0,
        SET_PAD,    // ESP32 -> RP2040: pad state (PacketIn)
        GET_PAD,    // RP2040 -> ESP32: rumble (PacketOut, read after a PacketIn)
        SET_DRIVER, // ESP32 -> RP2040: mode change from a button combo (PacketIn.device_driver)
        SET_PAD_EXT,// ESP32 -> RP2040: motion / touchpad / battery (PacketInExt), after a SET_PAD
        GET_EXT     // ESP32 -> RP2040 request, then the RP2040's PacketOutExt on the next read
    };

    #pragma pack(push, 1)
    struct PacketIn
    {
        uint8_t          packet_len{sizeof(PacketIn)};
        PacketID         packet_id{PacketID::SET_PAD};
        uint8_t          index{0};
        DeviceDriverType device_driver{DeviceDriverType::NONE};
        uint8_t          dpad{0};
        uint16_t         buttons{0};
        uint8_t          trigger_l{0};
        uint8_t          trigger_r{0};
        int16_t          joystick_lx{0};
        int16_t          joystick_ly{0};
        int16_t          joystick_rx{0};
        int16_t          joystick_ry{0};
        uint8_t          analog[10]{0};  // Gamepad ANALOG_OFF_* order; the ESP32 sends zeros
        uint8_t          reserved[5]{0};
    };
    static_assert(sizeof(PacketIn) == 32, "Esp32Bridge::PacketIn must stay 32 bytes");
    static_assert(offsetof(PacketIn, dpad) == 4, "Esp32Bridge::PacketIn layout changed");
    static_assert(offsetof(PacketIn, joystick_ry) == 15, "Esp32Bridge::PacketIn layout changed");
    static_assert(offsetof(PacketIn, analog) == 17, "Esp32Bridge::PacketIn layout changed");

    struct PacketOut
    {
        uint8_t  packet_len{sizeof(PacketOut)};
        PacketID packet_id{PacketID::GET_PAD};
        uint8_t  index{0};
        uint8_t  rumble_l{0};
        uint8_t  rumble_r{0};
        uint8_t  reserved[3]{0};
    };
    static_assert(sizeof(PacketOut) == 8, "Esp32Bridge::PacketOut must stay 8 bytes");
    static_assert(offsetof(PacketOut, rumble_l) == 3, "Esp32Bridge::PacketOut layout changed");

    struct PacketInExt
    {
        static constexpr uint8_t TOUCHPAD_VALID = 0x01;  // touch_raw / click from this report
        static constexpr uint8_t TOUCHPAD_CLICK = 0x02;
        static constexpr uint8_t JOYCON_LEFT    = 0x04;  // motion from a left Joy-Con
        static constexpr uint8_t JOYCON_RIGHT   = 0x08;  // motion from a right Joy-Con
        static constexpr uint8_t JOYCON_PAIRED  = 0x10;  // ... merged with its other half

        uint8_t  packet_len{sizeof(PacketInExt)};
        PacketID packet_id{PacketID::SET_PAD_EXT};
        uint8_t  index{0};
        uint8_t  motion_source{0};  // Gamepad::PadIn::MOTION_SRC_* (0 = none)
        int16_t  accel[3]{};        // Bluepad32 units, saturated to 16 bits
        int16_t  gyro[3]{};
        uint8_t  touch_raw[8]{};    // DS4 / DualSense touch points, as Bluepad32 reports them
        uint8_t  flags{0};
        uint8_t  battery{0};        // Bluepad32 scale (0 = unknown, 1..255)
        uint8_t  reserved[6]{};
    };
    static_assert(sizeof(PacketInExt) == 32, "Esp32Bridge::PacketInExt must stay 32 bytes");
    static_assert(offsetof(PacketInExt, accel) == 4, "Esp32Bridge::PacketInExt layout changed");
    static_assert(offsetof(PacketInExt, touch_raw) == 16, "Esp32Bridge::PacketInExt layout changed");
    static_assert(offsetof(PacketInExt, flags) == 24, "Esp32Bridge::PacketInExt layout changed");

    struct PacketOutExt
    {
        static constexpr uint8_t LIGHTBAR_VALID = 0x01;  // the host asked for this colour
        static constexpr uint8_t MOTION_OUTPUT  = 0x02;  // the output mode uses motion

        uint8_t  packet_len{sizeof(PacketOutExt)};
        PacketID packet_id{PacketID::GET_EXT};
        uint8_t  index{0};
        uint8_t  flags{0};
        uint8_t  lightbar_r{0};
        uint8_t  lightbar_g{0};
        uint8_t  lightbar_b{0};
        dongle_settings::Settings settings{};  // version 0 = not sent
        uint8_t  reserved[1]{};
    };
    static_assert(sizeof(PacketOutExt) == 25, "Esp32Bridge::PacketOutExt layout changed");
    static_assert(offsetof(PacketOutExt, settings) == 7, "Esp32Bridge::PacketOutExt layout changed");
    #pragma pack(pop)

    inline int16_t saturate_i16(int32_t v)
    {
        return v > 32767 ? int16_t{32767} : (v < -32768 ? int16_t{-32768} : static_cast<int16_t>(v));
    }

} // namespace Esp32Bridge

#endif // _ESP32_BRIDGE_H_
