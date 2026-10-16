#ifndef _OGXM_CAPABILITIES_H_
#define _OGXM_CAPABILITIES_H_

#include <cstdint>

/* What the adapter's hardware supports, for the web app (which turns the options it can't use
 * off, with a note): USB Web App mode GET_CAPABILITIES (0x76, 32 bits little-endian), and over
 * BLE 4 bytes after the dongle settings record (same encoding). Bits never move; an older firmware
 * sends neither, and the web app then shows every option as before. */
namespace capabilities {

    constexpr uint32_t BLUETOOTH                 = 1u << 0;  // Bluetooth controllers
    constexpr uint32_t HYBRID                    = 1u << 1;  // RP2040 + ESP32 (Bluetooth on the ESP32)
    constexpr uint32_t HIGH_TX_POWER             = 1u << 2;  // adapter option "higher transmit power"
    constexpr uint32_t JOYCON                    = 1u << 3;  // Joy-Con options (pairs, orientation, rumble)
    constexpr uint32_t MAC_PER_CONTROLLER        = 1u << 4;  // adapter option "MAC address per controller"
    constexpr uint32_t DISCONNECT_ON_MODE_CHANGE = 1u << 5;  // adapter option "turn off before mode change"
    constexpr uint32_t SEARCH_TIMES              = 1u << 6;  // search for new controllers panel
    constexpr uint32_t IDLE_OFF                  = 1u << 7;  // idle turn-off time
    constexpr uint32_t WIRED_INPUT               = 1u << 8;  // USB host port for wired controllers
    constexpr uint32_t BLE_WEBAPP                = 1u << 9;  // the web app over Bluetooth
    constexpr uint32_t STATUS_LED_PICO           = 1u << 10; // the Pico board's own status LED
    constexpr uint32_t STATUS_LED_ESP32          = 1u << 11; // the hybrid ESP32's status LED
    constexpr uint32_t STATUS_LED_PICO_EXT       = 1u << 12; // an external RGB LED on the Pico (GP6)
    constexpr uint32_t MODE_COLORS               = 1u << 13; // colour per mode (USB GET / SET_LED_COLORS)
    constexpr uint32_t STATUS_LED_ESP32_EXT      = 1u << 14; // an external RGB LED on the hybrid ESP32 (GPIO 4)

    constexpr uint32_t kBluetooth = BLUETOOTH | JOYCON | MAC_PER_CONTROLLER | DISCONNECT_ON_MODE_CHANGE |
                                    SEARCH_TIMES | IDLE_OFF | BLE_WEBAPP;
    /* Pico W / Pico 2 W: the CYW43's transmit power is not settable here. */
    constexpr uint32_t kPicoW = kBluetooth;
    /* RP2040 + ESP32 hybrid: the ESP32's LED and its external RGB LED pin (GPIO 4). */
    constexpr uint32_t kHybrid = kBluetooth | HYBRID | HIGH_TX_POWER | STATUS_LED_ESP32 | STATUS_LED_ESP32_EXT;

} // namespace capabilities

#endif // _OGXM_CAPABILITIES_H_
