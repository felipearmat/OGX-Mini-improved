#ifndef _ESP32_BRIDGE_H_
#define _ESP32_BRIDGE_H_

#include <cstddef>
#include <cstdint>

#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"
#include "UserSettings/DongleSettings.h"
#include "UserSettings/UserProfile.h"
#include "Gamepad/Ps5Core.h"

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
 *    only; an older RP2040 answers with a PacketOut (GET_PAD), which the ESP32 ignores.
 *  - SET_DIAG (ESP32 -> RP2040, PacketDiag): what the Pico W path records in
 *    Diagnostics/Diagnostics.h for its Bluetooth controllers, for the web app's report. SET_PAD
 *    then goes out on every input report with the Bluetooth slot in PacketIn::bt_slot, so the
 *    RP2040 measures the input timing.
 *  - The RP2040 keeps the settings (profiles, output mode, dongle options): PacketOutExt reports
 *    its mode, active profile and a checksum of the profiles; the ESP32 fetches them with
 *    GET_PROFILE and sends the ones saved over BLE with SET_PROFILE, so the USB and BLE web apps
 *    edit the same profiles. */
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
        GET_EXT,    // ESP32 -> RP2040 request, then the RP2040's PacketOutExt on the next read
        SET_DIAG,   // ESP32 -> RP2040: Bluetooth diagnostics for the web app's report (PacketDiag)
        GET_PROFILE,// ESP32 -> RP2040 request (PacketProfile), then that chunk on the next read
        SET_PROFILE,// ESP32 -> RP2040: a profile saved over BLE, in chunks (PacketProfile)
        GET_BLOB,   // ESP32 -> RP2040 request (PacketBlob), then that chunk on the next read
        SET_BLOB,   // ESP32 -> RP2040: settings saved over BLE, in chunks (PacketBlob)
        SET_PAD_RAW // ESP32 -> RP2040: STEAM mode, the pad's own input (PacketPadRaw), before SET_PAD
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
        uint8_t          bt_slot{0};     // version 2: Bluetooth slot + 1 of this report (0 = none)
        /* Version 2: the controller's own report counter in this report (diagnostics: reports
         * the controller sent that never arrived), when counter_bits > 0. */
        uint8_t          counter_value{0};
        uint8_t          counter_bits{0};
        uint8_t          counter_unit{0};  // step per report; 0 = learn it
        uint8_t          reserved[1]{0};
    };
    static_assert(sizeof(PacketIn) == 32, "Esp32Bridge::PacketIn must stay 32 bytes");
    static_assert(offsetof(PacketIn, dpad) == 4, "Esp32Bridge::PacketIn layout changed");
    static_assert(offsetof(PacketIn, joystick_ry) == 15, "Esp32Bridge::PacketIn layout changed");
    static_assert(offsetof(PacketIn, analog) == 17, "Esp32Bridge::PacketIn layout changed");
    static_assert(offsetof(PacketIn, bt_slot) == 27, "Esp32Bridge::PacketIn layout changed");

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
        static constexpr uint8_t WEB_APP_OPEN   = 0x04;  // the web app has the RP2040's serial port open

        uint8_t  packet_len{sizeof(PacketOutExt)};
        PacketID packet_id{PacketID::GET_EXT};
        uint8_t  index{0};
        uint8_t  flags{0};
        uint8_t  lightbar_r{0};
        uint8_t  lightbar_g{0};
        uint8_t  lightbar_b{0};
        dongle_settings::Settings settings{};  // version 0 = not sent
        DeviceDriverType driver{DeviceDriverType::NONE};  // the RP2040's output mode
        uint8_t  active_profile{0};   // active profile ID of this output (1..8)
        uint16_t profiles_crc{0};     // profiles_crc() of the RP2040's profiles; 0 = not sent
        /* The mode colour to show now (lightbar, status LED), as the RP2040 resolved it: the web
         * app's colour for the mode or its preview; 0, 0, 0 = the ESP32's own default. */
        uint8_t  mode_rgb[3]{};
    };
    static_assert(sizeof(PacketOutExt) == 37, "Esp32Bridge::PacketOutExt layout changed");
    static_assert(offsetof(PacketOutExt, settings) == 7, "Esp32Bridge::PacketOutExt layout changed");
    static_assert(offsetof(PacketOutExt, mode_rgb) == 34, "Esp32Bridge::PacketOutExt layout changed");
    struct PacketDiag
    {
        enum Kind : uint8_t
        {
            SLOT_NAME = 1,   // data: controller name, NUL-padded (sent before SLOT_READY)
            SLOT_READY,      // data: vid, pid (LE 16-bit), controller type, LE link, handle, address[6]
            SLOT_GONE,       // the slot's controller disconnected
            BATTERY,         // data[0]: level, Bluepad32 scale (0 = unknown, 1..255)
            EVENT,           // data[0]: 1 on the last part; data[1..]: text (an event in parts)
            SEARCHING,       // data[0]: new controllers accepted; data[1]: at the reduced rate
            ADAPTER_ADDRESS, // data[0..5]: the ESP32's Bluetooth address (DualShock 3 USB pairing)
            /* Link details (Bluepad32/LinkDiag), each Diagnostics.h call with its arguments in
             * order, 16-bit values little-endian; `slot` unused unless named. */
            SWITCH_FIRMWARE, // slot; major, minor
            LE_PARAMETERS,   // handle, interval, latency, timeout
            REMOTE_VERSION,  // handle, version (8), company, subversion
            DEVICE_INFO,     // handle, field (DeviceInfoField), value (NUL-padded)
            PNP_ID,          // handle, source (8), vid, pid, version
            RSSI,            // handle, dBm (signed 8)
            CHANNELS,        // handle, used (8), total (8)
            FAILED_CONTACTS, // handle, count
            TX_POWER,        // handle, maximum (8), dBm (signed 8)
            LINK_MODE,       // handle, mode (8), interval
            INQUIRY_COMPLETE,
            LE_ADV_REPORTS,  // count since the last one
            /* What the ESP32 measured itself (Diagnostics.h companion_*), every few seconds. */
            COMPANION_SLOT,  // slot; reports per s, lost valid (8), lost x10
            COMPANION_INFO,  // uptime s (32), high tx power (8), I2C write errors, read errors,
                             // queue drops (16 each, saturated), firmware (NUL-padded; empty
                             // since COMPANION_VERSION)
            COMPANION_VERSION, // the ESP32's OGX-Mini-improved version (NUL-padded)
        };
        enum DeviceInfoField : uint8_t { MANUFACTURER, MODEL, FIRMWARE, HARDWARE, SOFTWARE };
        static constexpr size_t kData = 28;
        static constexpr size_t kEventPart = kData - 1;

        uint8_t  packet_len{sizeof(PacketDiag)};
        PacketID packet_id{PacketID::SET_DIAG};
        uint8_t  kind{0};
        uint8_t  slot{0};
        uint8_t  data[kData]{};
    };
    static_assert(sizeof(PacketDiag) == 32, "Esp32Bridge::PacketDiag must stay 32 bytes");
    static_assert(offsetof(PacketDiag, data) == 4, "Esp32Bridge::PacketDiag layout changed");
    struct PacketProfile
    {
        static constexpr size_t kData = 26;
        static constexpr uint8_t kChunks = (sizeof(UserProfile) + kData - 1) / kData;

        uint8_t  packet_len{sizeof(PacketProfile)};
        PacketID packet_id{PacketID::GET_PROFILE};
        uint8_t  profile_id{0};  // 1..MAX_PROFILES
        uint8_t  chunk{0};       // 0..kChunks-1
        uint8_t  player_idx{0};  // SET_PROFILE: the output it is for (made active there)
        DeviceDriverType driver{DeviceDriverType::NONE};  // SET_PROFILE: mode saved with it, or NONE
        uint8_t  data[kData]{};
    };
    static_assert(sizeof(PacketProfile) == 32, "Esp32Bridge::PacketProfile must stay 32 bytes");
    static_assert(offsetof(PacketProfile, data) == 6, "Esp32Bridge::PacketProfile layout changed");
    /* Other settings the BLE web app reads and writes on the ESP32, kept by the RP2040. */
    struct PacketBlob
    {
        enum Kind : uint8_t
        {
            DONGLE_SETTINGS = 1,  // UserSettings/DongleSettings.h (stored, then the RP2040 restarts)
            KBM_SETTINGS = 2,     // mouse + keyboard mode mapping (applied without a restart)
        };
        static constexpr size_t kData = 26;
        static constexpr size_t kMaxLen = 64;

        uint8_t  packet_len{sizeof(PacketBlob)};
        PacketID packet_id{PacketID::GET_BLOB};
        uint8_t  kind{0};
        uint8_t  chunk{0};
        uint8_t  total_len{0};  // bytes of the whole blob (GET: in the reply; 0 = none)
        uint8_t  reserved{0};
        uint8_t  data[kData]{};
    };
    static_assert(sizeof(PacketBlob) == 32, "Esp32Bridge::PacketBlob must stay 32 bytes");
    static_assert(offsetof(PacketBlob, data) == 6, "Esp32Bridge::PacketBlob layout changed");
    /* STEAM mode only: the pad's input as it came (no profile) for the passthrough report, as the
     * Pico W path builds it (USBDevice/DeviceDriver/Steam/SteamBtReport.h). */
    struct PacketPadRaw
    {
        static constexpr uint8_t DUALSENSE = 0x01;  // the pad has the DualSense touchpad

        uint8_t  packet_len{sizeof(PacketPadRaw)};
        PacketID packet_id{PacketID::SET_PAD_RAW};
        uint8_t  index{0};
        uint8_t  flags{0};
        Ps5Core  core{};
        uint8_t  reserved[19]{};
    };
    static_assert(sizeof(PacketPadRaw) == 32, "Esp32Bridge::PacketPadRaw must stay 32 bytes");
    static_assert(offsetof(PacketPadRaw, core) == 4, "Esp32Bridge::PacketPadRaw layout changed");
    #pragma pack(pop)

    /* Fletcher-16 over the profiles (in ID order): tells the ESP32 when to fetch them again. Never 0,
     * so 0 stays "not sent". */
    inline uint16_t profiles_crc(const UserProfile* profiles, size_t count)
    {
        uint16_t a = 0, b = 0;
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(profiles);
        for (size_t i = 0; i < count * sizeof(UserProfile); ++i) {
            a = static_cast<uint16_t>((a + bytes[i]) % 255);
            b = static_cast<uint16_t>((b + a) % 255);
        }
        const uint16_t crc = static_cast<uint16_t>((b << 8) | a);
        return crc ? crc : 1;
    }

    inline int16_t saturate_i16(int32_t v)
    {
        return v > 32767 ? int16_t{32767} : (v < -32768 ? int16_t{-32768} : static_cast<int16_t>(v));
    }

} // namespace Esp32Bridge

#endif // _ESP32_BRIDGE_H_
