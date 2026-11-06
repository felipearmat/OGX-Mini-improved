#include "Board/Config.h"
#include "OGXMini/Board/Standard.h"
#include "OGXMini/Board/PicoW.h"
#include "OGXMini/Board/Four_Channel_I2C.h"
#include "OGXMini/Board/ESP32_Blueretro_I2C.h"
#include "OGXMini/Board/ESP32_Bluepad32_I2C.h"
#include "OGXMini/OGXMini.h"
#include "Diagnostics/DiagnosticsBoard.h"

namespace OGXMini {
    typedef void (*InitFunc)();
    typedef void (*RunFunc)();
    typedef void (*HostMountedFunc)(bool mounted);
    typedef void (*HostMountedWTypeFunc)(bool mounted, HostDriverType host_type);
    typedef void (*WirelessConnectedFunc)(bool connected, uint8_t idx);

    static constexpr InitFunc init_func[BOARDS_COUNT] = {
        standard::initialize,       // PI_PICO
        standard::initialize,       // RP2040_ZERO
        standard::initialize,       // ADAFRUIT_FEATHER
        pico_w::initialize,         // PI_PICOW
        esp32_bp32_i2c::initialize, // ESP32_BLUEPAD32_I2C
        esp32_br_i2c::initialize,   // ESP32_BLUERETRO_I2C
        four_ch_i2c::initialize,    // EXTERNAL_4CH_I2C
        four_ch_i2c::initialize,    // INTERNAL_4CH_I2C
        standard::initialize,       // RP2350_USB_A
        standard::initialize,       // RP2350_ZERO
        standard::initialize,       // RP2040_XIAO
        pico_w::initialize,         // RP2354 (CYW43439 / Pi Radio Module 2)
        standard::initialize,       // OGX_LIVE
    };

    static constexpr RunFunc run_func[BOARDS_COUNT] = {
        standard::run,          // PI_PICO
        standard::run,          // RP2040_ZERO
        standard::run,          // ADAFRUIT_FEATHER
        pico_w::run,            // PI_PICOW
        esp32_bp32_i2c::run,    // ESP32_BLUEPAD32_I2C
        esp32_br_i2c::run,      // ESP32_BLUERETRO_I2C
        four_ch_i2c::run,       // EXTERNAL_4CH_I2C
        four_ch_i2c::run,       // INTERNAL_4CH_I2C
        standard::run,          // RP2350_USB_A
        standard::run,          // RP2350_ZERO
        standard::run,          // RP2040_XIAO
        pico_w::run,            // RP2354
        standard::run,          // OGX_LIVE
    };

    static constexpr HostMountedFunc host_mount_func[BOARDS_COUNT] = {
        standard::host_mounted,     // PI_PICO
        standard::host_mounted,     // RP2040_ZERO
        standard::host_mounted,     // ADAFRUIT_FEATHER
        nullptr,                    // PI_PICOW
        nullptr,                    // ESP32_BLUEPAD32_I2C
        nullptr,                    // ESP32_BLUERETRO_I2C
        four_ch_i2c::host_mounted,  // EXTERNAL_4CH_I2C
        four_ch_i2c::host_mounted,  // INTERNAL_4CH_I2C
        standard::host_mounted,     // RP2350_USB_A
        standard::host_mounted,     // RP2350_ZERO
        standard::host_mounted,     // RP2040_XIAO
        nullptr,                    // RP2354
        standard::host_mounted,     // OGX_LIVE
    };

    static constexpr HostMountedWTypeFunc host_mount_w_type_func[BOARDS_COUNT] = {
        nullptr,                            // PI_PICO
        nullptr,                            // RP2040_ZERO
        nullptr,                            // ADAFRUIT_FEATHER
        nullptr,                            // PI_PICOW
        nullptr,                            // ESP32_BLUEPAD32_I2C
        nullptr,                            // ESP32_BLUERETRO_I2C
        four_ch_i2c::host_mounted_w_type,   // EXTERNAL_4CH_I2C
        four_ch_i2c::host_mounted_w_type,   // INTERNAL_4CH_I2C
        nullptr,                            // RP2350_USB_A
        nullptr,                            // RP2350_ZERO
        nullptr,                            // RP2040_XIAO
        nullptr,                            // RP2354
        nullptr,                            // OGX_LIVE
    };

    static constexpr WirelessConnectedFunc wl_conn_func[BOARDS_COUNT] = {
        nullptr,                            // PI_PICO
        nullptr,                            // RP2040_ZERO
        nullptr,                            // ADAFRUIT_FEATHER
        nullptr,                            // PI_PICOW
        nullptr,                            // ESP32_BLUEPAD32_I2C
        nullptr,                            // ESP32_BLUERETRO_I2C
        four_ch_i2c::wireless_connected,    // EXTERNAL_4CH_I2C
        four_ch_i2c::wireless_connected,    // INTERNAL_4CH_I2C
        nullptr,                            // RP2350_USB_A
        nullptr,                            // RP2350_ZERO
        nullptr,                            // RP2040_XIAO
        nullptr,                            // RP2354
        nullptr,                            // OGX_LIVE
    };

    void initialize() {
        if (auto f = init_func[OGXM_BOARD]) {
            f();
        }
        diag::board_boot();  // Custom: diagnostics for the web app's log report
    }

    void run() {
        if (auto f = run_func[OGXM_BOARD]) {
            f();
        }
    }

    void host_mounted(bool mounted, HostDriverType host_type) {
        if (auto f = host_mount_w_type_func[OGXM_BOARD]) {
            f(mounted, host_type);
        } else if (auto g = host_mount_func[OGXM_BOARD]) {
            g(mounted);
        }
    }

    void host_mounted(bool mounted) {
        if (auto f = host_mount_func[OGXM_BOARD]) {
            f(mounted);
        }
    }

    void wireless_connected(bool connected, uint8_t idx) {
        if (auto f = wl_conn_func[OGXM_BOARD]) {
            f(connected, idx);
        }
    }

} // namespace OGXMini