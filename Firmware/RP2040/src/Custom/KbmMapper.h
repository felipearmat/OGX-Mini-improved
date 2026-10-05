#ifndef _OGXM_CUSTOM_KBM_MAPPER_H_
#define _OGXM_CUSTOM_KBM_MAPPER_H_

#include <cstdint>

#include "Custom/KbmSettings.h"

/*  Mouse + keyboard output mode (custom addition): turns pad state into keyboard, mouse and
 *  media-key reports following kbm_settings. Hardware-free so it runs in host tests; the USB
 *  driver (USBDevice/DeviceDriver/KBM) fills KbmInput from Gamepad::PadIn and sends the reports.
 */
namespace kbm {

    // Button bits, same values as Gamepad::BUTTON_* / DPAD_* (checked in the driver).
    constexpr uint16_t BTN_A = 0x0001, BTN_B = 0x0002, BTN_X = 0x0004, BTN_Y = 0x0008;
    constexpr uint16_t BTN_L3 = 0x0010, BTN_R3 = 0x0020, BTN_BACK = 0x0040, BTN_START = 0x0080;
    constexpr uint16_t BTN_LB = 0x0100, BTN_RB = 0x0200, BTN_SYS = 0x0400, BTN_MISC = 0x0800;
    constexpr uint8_t DPAD_UP = 0x01, DPAD_DOWN = 0x02, DPAD_LEFT = 0x04, DPAD_RIGHT = 0x08;

    struct Input {
        uint16_t buttons{0};
        uint8_t dpad{0};
        uint8_t trigger_l{0};
        uint8_t trigger_r{0};
        int16_t lx{0}, ly{0}, rx{0}, ry{0};  // Y negative = up (Gamepad::PadIn convention)
        bool touch_valid{false};
        uint8_t touch_point[4]{};             // first touch point, DS4 / DualSense wire format
        bool touch_click{false};
    };

    struct KeyboardReport {
        uint8_t modifiers{0};
        uint8_t keys[6]{};
        bool operator==(const KeyboardReport& o) const;
        bool operator!=(const KeyboardReport& o) const { return !(*this == o); }
    };

    struct MouseReport {
        uint8_t buttons{0};
        int8_t x{0}, y{0}, wheel{0}, pan{0};
    };

    class Mapper {
    public:
        void reset();
        // dt_us: time since the previous update (pointer / scroll speed are per second).
        void update(const Input& in, const kbm_settings::Settings& s, uint32_t dt_us);

        const KeyboardReport& keyboard() const { return keyboard_; }
        uint16_t media() const { return media_; }
        // True when there is movement to send or the buttons changed since the last take.
        bool mouse_pending() const;
        // Whole pixels / scroll steps gathered so far (int8 range); the remainder is kept.
        MouseReport take_mouse();

    private:
        KeyboardReport keyboard_{};
        uint16_t media_{0};
        uint8_t mouse_buttons_{0};
        uint8_t sent_buttons_{0};
        float acc_x_{0}, acc_y_{0}, acc_wheel_{0}, acc_pan_{0};
        bool touching_{false};
        int touch_x_{0}, touch_y_{0};
    };

} // namespace kbm

#endif // _OGXM_CUSTOM_KBM_MAPPER_H_
