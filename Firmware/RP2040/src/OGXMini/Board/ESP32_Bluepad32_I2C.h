#pragma once

#include <cstdint>

namespace esp32_bp32_i2c {
    void initialize();
    void run();
    /* Custom: before a restart the RP2040 asks for (a mode or settings change from the web app or
     * the probe; a combo's comes from the ESP32, which does it itself): the ESP32 asks its
     * controllers to sleep, as the Pico W does (bluepad32::disconnect_pads_before_reboot), instead
     * of losing their links when the restart resets it. Blocks about 700 ms. */
    void sleep_pads_before_reboot();
} // namespace esp32_bp32_i2c