#ifndef _OGXM_WEBAPP_PROTOCOL_H_
#define _OGXM_WEBAPP_PROTOCOL_H_

#include <cstddef>
#include <cstdint>

/*  The USB web app's packet protocol (custom addition to the upstream one), shared by the RP2040
 *  firmware (USBDevice/DeviceDriver/WebApp) and the host tests: 64-byte packets, a 9-byte header
 *  (length, packet ID, output mode, gamepads, player, profile, chunks total, chunk index, chunk
 *  length), then the data. The web app's copy of these values is checked against
 *  Firmware/common/contract/webapp_contract.json (Firmware/RP2040/tests/test_webapp_contract.cpp). */
namespace webapp_protocol {

    constexpr size_t kPacketLength = 64;
    constexpr size_t kHeaderLength = 9;

    enum class PacketID : uint8_t
    {
        NONE = 0,
        GET_PROFILE_BY_ID = 0x50,
        GET_PROFILE_BY_IDX = 0x55,
        SET_PROFILE_START = 0x60,
        SET_PROFILE = 0x61,
        /* Custom (OGX-Mini-improved): dongle options (UserSettings/DongleSettings, 17 bytes; 16 = versions 2-3, 8 = version 1). */
        GET_DONGLE_SETTINGS = 0x70,
        SET_DONGLE_SETTINGS = 0x71,
        /* Custom: mouse + keyboard mode mapping (USBDevice/DeviceDriver/KBM/KbmSettings, 48 bytes). SET answers with
         * GET_KBM_SETTINGS once stored. */
        GET_KBM_SETTINGS = 0x72,
        SET_KBM_SETTINGS = 0x73,
        /* Custom: diagnostics report (Diagnostics/Diagnostics), JSON text over as many chunks as needed. */
        GET_DIAGNOSTICS = 0x74,
        /* Custom: adapter maintenance, so the adapter's own USB port does everything. data[0]:
         * SYSTEM_BOOTSEL (RP2040 / RP2350 USB bootloader, for a UF2 or picotool), SYSTEM_REBOOT,
         * and on the RP2040 + ESP32 hybrid SYSTEM_ESP32_FLASH (restart as a USB serial to the
         * ESP32's ROM bootloader, for esptool) and SYSTEM_ESP32_MONITOR (restart as a USB serial
         * with the kept log and the ESP32's live output). Answered with SYSTEM before acting, or
         * RESP_ERROR when the board can't. */
        SYSTEM = 0x75,
        /* Custom: what this hardware supports (UserSettings/Capabilities.h), 4 bytes little-endian. */
        GET_CAPABILITIES = 0x76,
        /* Custom: versions, NUL-separated text: this firmware's OGX-Mini-improved version, the
         * board (OGXM_BOARD, as in the release file names), then the hybrid's ESP32 version ("" if
         * none or not heard yet). The web app compares them with the latest release. */
        GET_VERSION = 0x77,
        /* Custom: the colour per output mode (Board/ModeIndicator.h ModeColors, 55 bytes, one
         * packet). SET stores and applies it and answers with GET_LED_COLORS. */
        GET_LED_COLORS = 0x78,
        SET_LED_COLORS = 0x79,
        /* Custom: a colour shown on the lightbar and the status LED in place of the mode's own while
         * picking one. Data: active (0 / 1), r, g, b. Ends after kPreviewMs without a new one, or
         * when the web app disconnects. Answered with an empty LED_PREVIEW. */
        LED_PREVIEW = 0x7A,
        /* Custom: the kept log (RP2040 Board/LogRing.h: diagnostics events, the hybrid's ESP32
         * output), read without leaving the mode. Data: position to read from (uint32,
         * little-endian; 0 = the oldest kept byte). Answer, in chunks: the position of the first
         * byte sent (later than asked when bytes were dropped), the position after the newest
         * byte (both uint32), then up to kLogBytes of text. */
        GET_LOG = 0x7B,
        SET_GP_IN = 0x80,
        /* Custom: rumble test from the web app. Data: left (strong) motor 0-255, right (weak)
         * motor 0-255, duration in ms (uint16, little-endian). Answered with an empty SET_GP_OUT. */
        SET_GP_OUT = 0x81,
        /* Custom: DS4 / DualSense touchpad, sent after SET_GP_IN while the pad reports it. Data:
         * the two touch points as the pads send them (4 bytes each), then the touchpad click. */
        GP_TOUCH = 0x82,
        RESP_ERROR = 0xFF
    };

    constexpr size_t kLogBytes = 2048;  // GET_LOG text per answer

    enum SystemCommand : uint8_t
    {
        SYSTEM_BOOTSEL = 1,
        SYSTEM_REBOOT = 2,
        SYSTEM_ESP32_FLASH = 3,
        SYSTEM_ESP32_MONITOR = 4,
    };

} // namespace webapp_protocol

#endif // _OGXM_WEBAPP_PROTOCOL_H_
