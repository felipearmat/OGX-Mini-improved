#ifndef _OGXM_BOARD_API_H_
#define _OGXM_BOARD_API_H_

#include <cstdint>
#include <string>

namespace board_api {
    void init_board();
    void init_bluetooth();
    void reboot();
    void set_led(bool state);
    /** Custom: colour set_led(true) shows on an RGB LED; set_led(false) then turns it off.
     *  Boards that never call it keep the default (green on, red off). */
    void set_led_color(uint8_t r, uint8_t g, uint8_t b);
    uint32_t ms_since_boot();

    namespace usb {
        bool host_connected();
        void disconnect_all();
        /** True if TinyUSB host has at least one mounted gamepad driver (wired controller active). */
        bool host_any_pad_mounted();
    }
}

#endif // _OGXM_BOARD_API_H_