// USB device drivers end to end: the real driver code, built on the host with the TinyUSB
// calls stubbed. Output reports go in through set_report_cb() the way TinyUSB hands them over
// (report ID stripped from SET_REPORT), input reports are captured from tud_hid_n_report().
// Regressions covered:
//  - DInput: analog button pressure for circle / cross / square came from X / B / A.
//  - PS3: hid-sony's report 0x01 arrives one byte short; rumble never worked on Linux.
//  - PS4 / STEAM: every USB output report was dropped (length check counted the ID twice).
//  - PS4 / STEAM: a lightbar-only update stopped a running rumble; SDL's stop (every valid flag
//    clear) must stop it.
//  - PS4 / STEAM: input GET_REPORT with a report ID repeated the ID TinyUSB already adds.
//  - KBM (mouse + keyboard mode): pad buttons / sticks reach the keyboard, mouse and media-key
//    interfaces; reports only go out on change; the descriptors match TinyUSB's boot layouts.
#include <cstring>
#include <vector>

#include "tusb.h"
#include "USBDevice/DeviceDriver/DInput/DInput.h"
#include "USBDevice/DeviceDriver/PS3/PS3.h"
#include "USBDevice/DeviceDriver/PS4/PS4.h"
#include "USBDevice/DeviceDriver/Steam/Steam.h"
#include "USBDevice/DeviceDriver/KBM/KBM.h"
#include "Descriptors/KBM.h"
#include "test.h"

/* ---- TinyUSB stubs ------------------------------------------------------------------------ */

uint64_t mock_time_us = 0;

namespace {
std::vector<uint8_t> g_last_report;
uint8_t g_last_report_itf = 0xFF;
int g_keyboard_reports = 0;
uint8_t g_keyboard_mods = 0;
uint8_t g_keyboard_keys[6]{};
int g_mouse_reports = 0;
int g_mouse_x = 0, g_mouse_y = 0, g_mouse_wheel = 0;
uint8_t g_mouse_buttons = 0;
}

extern "C" {
bool tud_hid_n_ready(uint8_t) { return true; }
bool tud_hid_n_report(uint8_t instance, uint8_t report_id, void const* report, uint16_t len) {
    g_last_report_itf = instance;
    g_last_report.clear();
    if (report_id != 0)
        g_last_report.push_back(report_id);
    const uint8_t* p = static_cast<const uint8_t*>(report);
    g_last_report.insert(g_last_report.end(), p, p + len);
    return true;
}
bool tud_hid_n_keyboard_report(uint8_t, uint8_t, uint8_t modifier, const uint8_t keycode[6]) {
    ++g_keyboard_reports;
    g_keyboard_mods = modifier;
    std::memcpy(g_keyboard_keys, keycode, 6);
    return true;
}
bool tud_hid_n_mouse_report(uint8_t, uint8_t, uint8_t buttons, int8_t x, int8_t y, int8_t v, int8_t) {
    ++g_mouse_reports;
    g_mouse_buttons = buttons;
    g_mouse_x += x;
    g_mouse_y += y;
    g_mouse_wheel += v;
    return true;
}
bool tud_suspended(void) { return false; }
bool tud_remote_wakeup(void) { return true; }
bool tud_mounted(void) { return true; }
bool tud_control_xfer(uint8_t, tusb_control_request_t const*, void*, uint16_t) { return true; }
bool hidd_control_xfer_cb(uint8_t, uint8_t, tusb_control_request_t const*) { return true; }
void hidd_init(void) {}
bool hidd_deinit(void) { return true; }
void hidd_reset(uint8_t) {}
uint16_t hidd_open(uint8_t, tusb_desc_interface_t const*, uint16_t) { return 0; }
bool hidd_xfer_cb(uint8_t, uint8_t, xfer_result_t, uint32_t) { return true; }
}

// String descriptors are not under test (DeviceDriver.cpp needs TinyUSB's BSP).
uint16_t* DeviceDriver::get_string_descriptor(const char*, uint8_t) {
    static uint16_t desc[32]{};
    return desc;
}

namespace {

/* Output report as TinyUSB passes it to set_report_cb: SET_REPORT strips the ID. */
template <typename Driver>
void set_output(Driver& dev, uint8_t report_id, const std::vector<uint8_t>& body) {
    dev.set_report_cb(0, report_id, HID_REPORT_TYPE_OUTPUT, body.data(),
                      static_cast<uint16_t>(body.size()));
}

Gamepad::PadOut out_of(Gamepad& gp) { return gp.get_pad_out(); }

}  // namespace

/* ---- DInput ------------------------------------------------------------------------------ */

TEST(dinput_pressure_follows_the_digital_buttons) {
    Gamepad gp;
    UserProfile profile{};
    profile.analog_enabled = 1;
    gp.set_profile(profile);
    gp.set_analog_host(true);
    gp.set_analog_device(true);
    CHECK(gp.analog_enabled());

    Gamepad::PadIn in{};
    in.buttons = Gamepad::BUTTON_A | Gamepad::BUTTON_B | Gamepad::BUTTON_X | Gamepad::BUTTON_Y;
    in.analog[Gamepad::ANALOG_OFF_A] = 10;
    in.analog[Gamepad::ANALOG_OFF_B] = 20;
    in.analog[Gamepad::ANALOG_OFF_X] = 30;
    in.analog[Gamepad::ANALOG_OFF_Y] = 40;
    gp.set_pad_in(in);

    DInputDevice dev;
    dev.initialize();
    dev.process(0, gp);

    DInput::InReport report{};
    CHECK_EQ(g_last_report.size(), sizeof(report));
    std::memcpy(&report, g_last_report.data(), sizeof(report));
    CHECK_EQ(report.cross_axis, 10);     // A
    CHECK_EQ(report.circle_axis, 20);    // B
    CHECK_EQ(report.square_axis, 30);    // X
    CHECK_EQ(report.triangle_axis, 40);  // Y
}

/* ---- PS3 --------------------------------------------------------------------------------- */

TEST(ps3_linux_rumble_reaches_the_pad) {
    // hid-sony: 35 bytes without the ID, starting with the 0x01 padding byte, which TinyUSB
    // takes for the report ID and strips. Right motor on, left force 0xFF.
    std::vector<uint8_t> body(34, 0);
    body[0] = 0xFF;  // right duration
    body[1] = 0x01;  // right motor on
    body[2] = 0xFF;  // left duration
    body[3] = 0xFF;  // left force
    Gamepad gp;
    PS3Device dev;
    dev.initialize();
    set_output(dev, 0x01, body);
    dev.process(0, gp);
    CHECK(out_of(gp).rumble_l > 0);
    CHECK(out_of(gp).rumble_r > 0);
}

/* ---- PS4 --------------------------------------------------------------------------------- */

namespace {
// DS4 USB output report 0x05 body (31 bytes after the ID): flags, flags2, reserved, right motor,
// left motor, red, green, blue, ...
std::vector<uint8_t> ds4_output(uint8_t flags, uint8_t right, uint8_t left) {
    std::vector<uint8_t> body(31, 0);
    body[0] = flags;
    body[3] = right;
    body[4] = left;
    body[5] = 0x10;
    body[6] = 0x20;
    body[7] = 0x30;
    return body;
}
}  // namespace

TEST(ps4_output_report_reaches_the_pad) {
    Gamepad gp;
    PS4Device dev;
    dev.initialize();
    set_output(dev, 0x05, ds4_output(0x01 | 0x02, 0x40, 0x80));
    dev.process(0, gp);
    CHECK_EQ(out_of(gp).rumble_r, 0x40);
    CHECK_EQ(out_of(gp).rumble_l, 0x80);

    // Lightbar-only update: rumble flag clear, motor bytes zero. Rumble keeps going.
    set_output(dev, 0x05, ds4_output(0x02, 0, 0));
    dev.process(0, gp);
    CHECK_EQ(out_of(gp).rumble_l, 0x80);

    // Stop (SDL sets the rumble flag on every change, including the stop).
    set_output(dev, 0x05, ds4_output(0x01, 0, 0));
    dev.process(0, gp);
    CHECK_EQ(out_of(gp).rumble_l, 0);
    CHECK_EQ(out_of(gp).rumble_r, 0);
}

TEST(ps4_input_get_report_does_not_repeat_the_id) {
    Gamepad gp;
    PS4Device dev;
    dev.initialize();
    dev.process(0, gp);
    uint8_t buf[64]{};
    // TinyUSB writes the ID itself and hands the driver the space after it.
    const uint16_t n = dev.get_report_cb(0, 0x01, HID_REPORT_TYPE_INPUT, buf, sizeof(buf) - 1);
    CHECK(n > 0);
    // Same bytes as the interrupt report the driver sent, minus its leading ID.
    CHECK_EQ(g_last_report[0], 0x01);
    CHECK(n <= g_last_report.size() - 1);
    CHECK(std::memcmp(buf, g_last_report.data() + 1, n) == 0);
}

/* ---- STEAM (DualSense) ------------------------------------------------------------------- */

namespace {
// DualSense USB output report 0x02 body (47 bytes after the ID): valid_flag0, valid_flag1,
// right motor, left motor, ..., valid_flag2 at body offset 38.
std::vector<uint8_t> ds5_output(uint8_t flag0, uint8_t flag1, uint8_t flag2, uint8_t right,
                                uint8_t left) {
    std::vector<uint8_t> body(47, 0);
    body[0] = flag0;
    body[1] = flag1;
    body[2] = right;
    body[3] = left;
    body[38] = flag2;
    return body;
}
}  // namespace

TEST(steam_output_report_reaches_the_pad) {
    Gamepad gp;
    SteamDevice dev;
    dev.initialize();
    set_output(dev, 0x02, ds5_output(0x01, 0, 0, 0x40, 0x80));
    dev.process(0, gp);
    CHECK_EQ(out_of(gp).rumble_r, 0x40);
    CHECK_EQ(out_of(gp).rumble_l, 0x80);

    // Lightbar-only update (valid_flag1 0x04): rumble keeps going.
    set_output(dev, 0x02, ds5_output(0, 0x04, 0, 0, 0));
    dev.process(0, gp);
    CHECK_EQ(out_of(gp).rumble_l, 0x80);

    // SDL's stop: every valid flag clear.
    set_output(dev, 0x02, ds5_output(0, 0, 0, 0, 0));
    dev.process(0, gp);
    CHECK_EQ(out_of(gp).rumble_l, 0);
    CHECK_EQ(out_of(gp).rumble_r, 0);

    // "Improved rumble" (valid_flag2 0x04), used by SDL on firmware 2.24+.
    set_output(dev, 0x02, ds5_output(0, 0, 0x04, 0x11, 0x22));
    dev.process(0, gp);
    CHECK_EQ(out_of(gp).rumble_r, 0x11);
    CHECK_EQ(out_of(gp).rumble_l, 0x22);
}

TEST(steam_input_get_report_does_not_repeat_the_id) {
    Gamepad gp;
    SteamDevice dev;
    dev.initialize();
    dev.process(0, gp);
    uint8_t buf[64]{};
    const uint16_t n = dev.get_report_cb(0, 0x01, HID_REPORT_TYPE_INPUT, buf, sizeof(buf) - 1);
    CHECK(n > 0);
    // Same bytes as the interrupt report the driver sent, minus its leading ID.
    CHECK_EQ(g_last_report[0], 0x01);
    CHECK(n <= g_last_report.size() - 1);
    CHECK(std::memcmp(buf, g_last_report.data() + 1, n) == 0);
}

/* ---- KBM (mouse + keyboard) ---------------------------------------------------------------- */

TEST(kbm_pad_drives_keyboard_mouse_and_media_keys) {
    kbm_settings::set(kbm_settings::defaults());
    Gamepad gp;
    KBMDevice dev;
    dev.initialize();
    g_keyboard_reports = g_mouse_reports = 0;
    g_mouse_x = g_mouse_y = g_mouse_wheel = 0;
    mock_time_us = 1000000;

    Gamepad::PadIn in;
    in.buttons = Gamepad::BUTTON_A;  // Enter
    gp.set_pad_in(in);
    dev.process(0, gp);
    CHECK_EQ(g_keyboard_reports, 1);
    CHECK_EQ(g_keyboard_keys[0], 0x28);
    dev.process(0, gp);  // unchanged: no new report
    CHECK_EQ(g_keyboard_reports, 1);

    in.buttons = Gamepad::BUTTON_SYS;  // Home media key
    gp.set_pad_in(in);
    g_last_report_itf = 0xFF;
    dev.process(0, gp);
    CHECK_EQ(g_keyboard_reports, 2);  // Enter released
    CHECK_EQ(g_keyboard_keys[0], 0);
    CHECK_EQ(g_last_report_itf, KBM::ITF_MEDIA);
    CHECK_EQ(g_last_report[0] | (g_last_report[1] << 8), 0x0223);

    in.buttons = 0;
    in.joystick_rx = 32767;  // pointer right
    in.trigger_r = 255;      // left click
    gp.set_pad_in(in);
    for (int i = 0; i < 20; ++i) {
        mock_time_us += 1000;
        dev.process(0, gp);
    }
    CHECK(g_mouse_x > 0);
    CHECK_EQ(g_mouse_buttons, 0x01);

    in.joystick_rx = 0;
    in.trigger_r = 0;
    in.joystick_ly = -32767;  // scroll up
    gp.set_pad_in(in);
    for (int i = 0; i < 200; ++i) {
        mock_time_us += 1000;
        dev.process(0, gp);
    }
    CHECK(g_mouse_wheel > 0);
    CHECK_EQ(g_mouse_buttons, 0);

    // A stall (e.g. a flash write) does not make the pointer jump.
    in.joystick_ly = 0;
    in.joystick_rx = 32767;
    gp.set_pad_in(in);
    const int before = g_mouse_x;
    mock_time_us += 5000000;
    dev.process(0, gp);
    for (int i = 0; i < 5; ++i)
        dev.process(0, gp);  // drain what is left of the step (int8 per report)
    CHECK(g_mouse_x - before <= 100);
}

TEST(kbm_descriptors) {
    KBMDevice dev;
    dev.initialize();
    const uint8_t* cfg = dev.get_descriptor_configuration_cb(0);
    CHECK_EQ(cfg[2] | (cfg[3] << 8), sizeof(KBM::CONFIGURATION_DESCRIPTORS));
    CHECK_EQ(cfg[4], 3);  // interfaces
    // Interface descriptors: keyboard and mouse are boot devices, media keys are not.
    const uint8_t* itf = cfg + TUD_CONFIG_DESC_LEN;
    CHECK_EQ(itf[6], HID_SUBCLASS_BOOT);
    CHECK_EQ(itf[7], HID_ITF_PROTOCOL_KEYBOARD);
    itf += TUD_HID_DESC_LEN;
    CHECK_EQ(itf[7], HID_ITF_PROTOCOL_MOUSE);
    itf += TUD_HID_DESC_LEN;
    CHECK_EQ(itf[6], HID_SUBCLASS_NONE);
    CHECK(std::memcmp(dev.get_hid_descriptor_report_cb(KBM::ITF_MOUSE), KBM::MOUSE_REPORT_DESCRIPTORS,
                      sizeof(KBM::MOUSE_REPORT_DESCRIPTORS)) == 0);
    CHECK(std::memcmp(dev.get_hid_descriptor_report_cb(KBM::ITF_MEDIA), KBM::MEDIA_REPORT_DESCRIPTORS,
                      sizeof(KBM::MEDIA_REPORT_DESCRIPTORS)) == 0);
    CHECK(dev.get_descriptor_string_cb(9, 0x0409) == nullptr);  // out of range
}

TEST_MAIN()
