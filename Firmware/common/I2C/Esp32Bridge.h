#ifndef _ESP32_BRIDGE_H_
#define _ESP32_BRIDGE_H_

#include <cstddef>
#include <cstdint>

#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"

/* I2C link between the RP2040 (USB side, I2C slave) and the ESP32 companion running
 * Bluepad32 (I2C master): board ESP32_BLUEPAD32_I2C. Both firmwares build from this one
 * header, so a layout change reaches both sides; tests/test_esp32_bridge.cpp pins the
 * wire layout byte by byte (a firmware flashed from an older tree must still talk to it). */
namespace Esp32Bridge
{
    // RP2040 slave address; with CONFIG_MULTI_SLAVE_MODE the ESP32 adds the pad index.
    static constexpr uint8_t SLAVE_ADDR = 0x01;

    enum class PacketID : uint8_t
    {
        UNKNOWN = 0,
        SET_PAD,    // ESP32 -> RP2040: pad state (PacketIn)
        GET_PAD,    // RP2040 -> ESP32: rumble (PacketOut, read after a PacketIn)
        SET_DRIVER  // ESP32 -> RP2040: mode change from a button combo (PacketIn.device_driver)
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
    #pragma pack(pop)

} // namespace Esp32Bridge

#endif // _ESP32_BRIDGE_H_
