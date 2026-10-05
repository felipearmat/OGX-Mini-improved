#include <cstring>

#include "class/hid/hid_device.h"
#include "pico/time.h"

#include "Custom/KbmSettings.h"
#include "Descriptors/KBM.h"
#include "Gamepad/MotionImu.h"
#include "USBDevice/DeviceDriver/KBM/KBM.h"

static_assert(kbm::BTN_A == Gamepad::BUTTON_A && kbm::BTN_B == Gamepad::BUTTON_B &&
              kbm::BTN_X == Gamepad::BUTTON_X && kbm::BTN_Y == Gamepad::BUTTON_Y &&
              kbm::BTN_L3 == Gamepad::BUTTON_L3 && kbm::BTN_R3 == Gamepad::BUTTON_R3 &&
              kbm::BTN_BACK == Gamepad::BUTTON_BACK && kbm::BTN_START == Gamepad::BUTTON_START &&
              kbm::BTN_LB == Gamepad::BUTTON_LB && kbm::BTN_RB == Gamepad::BUTTON_RB &&
              kbm::BTN_SYS == Gamepad::BUTTON_SYS && kbm::BTN_MISC == Gamepad::BUTTON_MISC,
              "kbm button bits follow Gamepad");
static_assert(kbm::DPAD_UP == Gamepad::DPAD_UP && kbm::DPAD_DOWN == Gamepad::DPAD_DOWN &&
              kbm::DPAD_LEFT == Gamepad::DPAD_LEFT && kbm::DPAD_RIGHT == Gamepad::DPAD_RIGHT,
              "kbm D-pad bits follow Gamepad");

namespace {

/* Longest step taken into account: after a stall (flash write, USB suspend) the pointer should
 * not jump by everything that would have happened meanwhile. */
constexpr uint32_t MAX_STEP_US = 50000;

kbm::Input to_kbm_input(const Gamepad::PadIn& gp_in)
{
    kbm::Input in;
    in.buttons = gp_in.buttons;
    in.dpad = gp_in.dpad;
    in.trigger_l = gp_in.trigger_l;
    in.trigger_r = gp_in.trigger_r;
    in.lx = gp_in.joystick_lx;
    in.ly = gp_in.joystick_ly;
    in.rx = gp_in.joystick_rx;
    in.ry = gp_in.joystick_ry;
    in.has_gyro = gp_in.has_motion();
    if (in.has_gyro) {
        int32_t accel[3], gyro[3];  // aligned copies: PadIn is packed
        for (int i = 0; i < 3; ++i) {
            accel[i] = gp_in.accel[i];
            gyro[i] = gp_in.gyro[i];
        }
        MotionImu::remap_to_ds4_playing_frame(gp_in.motion_source, accel, gyro);
        for (int i = 0; i < 3; ++i)
            in.gyro[i] = gyro[i];
    }
    in.touch_valid = gp_in.touchpad_valid != 0;
    std::memcpy(in.touch_point, gp_in.touch_raw, sizeof(in.touch_point));
    in.touch_click = gp_in.touchpad_click != 0;
    return in;
}

} // namespace

void KBMDevice::initialize()
{
    class_driver_ = {
        .name = TUD_DRV_NAME("KBM"),
        .init = hidd_init,
        .deinit = hidd_deinit,
        .reset = hidd_reset,
        .open = hidd_open,
        .control_xfer_cb = hidd_control_xfer_cb,
        .xfer_cb = hidd_xfer_cb,
        .sof = NULL
    };
    mapper_.reset();
    sent_keyboard_ = kbm::KeyboardReport{};
    sent_media_ = 0;
    last_us_ = 0;
}

void KBMDevice::process(const uint8_t idx, Gamepad& gamepad)
{
    if (idx != 0)  // one keyboard / mouse, driven by the first pad
        return;

    const uint64_t now = time_us_64();
    uint64_t dt = last_us_ == 0 ? 0 : now - last_us_;
    last_us_ = now;
    if (dt > MAX_STEP_US)
        dt = MAX_STEP_US;

    mapper_.update(to_kbm_input(gamepad.get_pad_in()), kbm_settings::get(), static_cast<uint32_t>(dt));

    if (tud_suspended()) {
        // Any key or click wakes a sleeping host (remote wakeup enabled in the descriptor).
        if (mapper_.keyboard() != kbm::KeyboardReport{} || mapper_.mouse_pending())
            tud_remote_wakeup();
        return;
    }

    if (mapper_.keyboard() != sent_keyboard_ && tud_hid_n_ready(KBM::ITF_KEYBOARD)) {
        const kbm::KeyboardReport& kb = mapper_.keyboard();
        if (tud_hid_n_keyboard_report(KBM::ITF_KEYBOARD, 0, kb.modifiers, kb.keys))
            sent_keyboard_ = kb;
    }

    if (mapper_.mouse_pending() && tud_hid_n_ready(KBM::ITF_MOUSE)) {
        const kbm::MouseReport m = mapper_.take_mouse();
        tud_hid_n_mouse_report(KBM::ITF_MOUSE, 0, m.buttons, m.x, m.y, m.wheel, m.pan);
    }

    if (mapper_.media() != sent_media_ && tud_hid_n_ready(KBM::ITF_MEDIA)) {
        const uint16_t usage = mapper_.media();
        if (tud_hid_n_report(KBM::ITF_MEDIA, 0, &usage, sizeof(usage)))
            sent_media_ = usage;
    }
}

uint16_t KBMDevice::get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)report_id;
    if (report_type != HID_REPORT_TYPE_INPUT)
        return 0;
    switch (itf) {
        case KBM::ITF_KEYBOARD: {
            hid_keyboard_report_t r{};
            r.modifier = sent_keyboard_.modifiers;
            std::memcpy(r.keycode, sent_keyboard_.keys, sizeof(r.keycode));
            const uint16_t n = reqlen < sizeof(r) ? reqlen : sizeof(r);
            std::memcpy(buffer, &r, n);
            return n;
        }
        case KBM::ITF_MOUSE: {
            hid_mouse_report_t r{};  // no pending movement in a polled report
            const uint16_t n = reqlen < sizeof(r) ? reqlen : sizeof(r);
            std::memcpy(buffer, &r, n);
            return n;
        }
        case KBM::ITF_MEDIA: {
            const uint16_t n = reqlen < sizeof(sent_media_) ? reqlen : sizeof(sent_media_);
            std::memcpy(buffer, &sent_media_, n);
            return n;
        }
        default:
            return 0;
    }
}

void KBMDevice::set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    // Keyboard LED output report (Caps / Num Lock): nothing to show it on.
    (void)itf;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

bool KBMDevice::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    (void)rhport;
    (void)stage;
    (void)request;
    return false;
}

const uint16_t* KBMDevice::get_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    if (index >= KBM::STRING_COUNT)
        return nullptr;
    const char *value = reinterpret_cast<const char*>(KBM::STRING_DESCRIPTORS[index]);
    return get_string_descriptor(value, index);
}

const uint8_t* KBMDevice::get_descriptor_device_cb()
{
    return KBM::DEVICE_DESCRIPTORS;
}

const uint8_t* KBMDevice::get_hid_descriptor_report_cb(uint8_t itf)
{
    switch (itf) {
        case KBM::ITF_MOUSE: return KBM::MOUSE_REPORT_DESCRIPTORS;
        case KBM::ITF_MEDIA: return KBM::MEDIA_REPORT_DESCRIPTORS;
        default:             return KBM::KEYBOARD_REPORT_DESCRIPTORS;
    }
}

const uint8_t* KBMDevice::get_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return KBM::CONFIGURATION_DESCRIPTORS;
}

const uint8_t* KBMDevice::get_descriptor_device_qualifier_cb()
{
    return nullptr;
}
