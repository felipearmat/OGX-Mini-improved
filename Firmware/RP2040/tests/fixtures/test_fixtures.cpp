// End-to-end tests from real controller input (Firmware/RP2040/tests/fixtures/README.md): each
// recorded raw Bluetooth report goes through Bluepad32's own parser, the firmware's PadIn path
// (Bluepad32/BtPadIn.h: the profile mapping, sticks, triggers, touchpad, motion) and the real USB
// output driver of a mode; the host's reports are summarised per recorded action and compared
// with golden files (fixtures/golden/<pad>.<mode>.txt), and checked against what each action
// must give (A held -> the mode's A / Cross, a full trigger -> 255...).
//
//   test_fixtures            compare with the golden files
//   test_fixtures --write    rewrite them (after reviewing the change)
//
// Built 32-bit like test_switch_parser (Bluepad32's structs as on the RP2350).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "tusb.h"
#include "fixture_replay.h"
#include "Descriptors/DInput.h"
#include "Descriptors/PS4.h"
#include "Descriptors/PS3.h"
#include "Descriptors/PS5.h"
#include "Descriptors/WiiU.h"
#include "Descriptors/XboxOG.h"
#include "USBDevice/DeviceDriver/DInput/DInput.h"
#include "USBDevice/DeviceDriver/PS3/PS3.h"
#include "USBDevice/DeviceDriver/PS4/PS4.h"
#include "USBDevice/DeviceDriver/Sony/SonyReports.h"
#include "USBDevice/DeviceDriver/Steam/Steam.h"
#include "USBDevice/DeviceDriver/Steam/SteamBtReport.h"
#include "USBDevice/DeviceDriver/Steam/SteamPassthrough.h"
#include "USBDevice/DeviceDriver/Switch/Switch.h"
#include "USBDevice/DeviceDriver/Switch/SwitchSpi.h"
#include "USBDevice/DeviceDriver/XInput/XInput.h"
#include "USBDevice/DeviceDriver/XInput/tud_xinput/tud_xinput.h"
#include "USBDevice/DeviceDriver/WiiU/WiiU.h"
#include "USBDevice/DeviceDriver/XboxOG/XboxOG_GP.h"
#include "USBDevice/DeviceDriver/XboxOG/XboxOG_SB.h"
#include "USBDevice/DeviceDriver/XboxOG/XboxOG_XR.h"
#include "USBDevice/DeviceDriver/XboxOG/tud_xid/tud_xid.h"
#include "USBDevice/DeviceDriver/PSClassic/PSClassic.h"
#include "USBDevice/DeviceDriver/KBM/KBM.h"
#include "USBDevice/DeviceDriver/GameCube/GameCube.h"
#include "USBDevice/DeviceDriver/N64/N64.h"
#include "USBDevice/DeviceDriver/PS1PS2/PS1PS2.h"
#include "USBDevice/DeviceDriver/Dreamcast/Dreamcast.h"
#include "Descriptors/KBM.h"
#include "Board/board_api.h"
#include "UserSettings/UserProfile.h"
#include "console_outputs.h"
extern "C" {
#include "Wii/wiimote-lib/wiimote.h"
}

/* Wii/WiiReportConverter.h declares it only for the boards with a USB host port (built so here). */
namespace Wii {
void gamepad_to_wiimote_report(Gamepad& gamepad, void* out_wiimote_report);
}

/* ---- TinyUSB stubs (as test_device_reports) ----------------------------------------------- */

uint64_t mock_time_us = 0;
namespace {
std::vector<uint8_t> g_last_report;
/* Mouse + Keyboard mode: the keys held (last keyboard report), the media key, and the mouse
 * reports sent since the last frame. */
struct Kbm {
    uint8_t modifiers = 0;
    uint8_t keys[6]{};
    uint16_t media = 0;
    uint8_t mouse_buttons = 0;
    long dx = 0, dy = 0, wheel = 0, pan = 0;
} g_kbm;
bool g_kbm_mode = false;
bool g_xremote_rom = false;  // Xbox OG remote mode: the remote's ROM is there (else no reports)
}
extern "C" {
bool tud_hid_n_ready(uint8_t) { return true; }
bool tud_hid_n_report(uint8_t itf, uint8_t report_id, void const* report, uint16_t len) {
    if (g_kbm_mode && itf == KBM::ITF_MEDIA) {
        std::memcpy(&g_kbm.media, report, 2);
        return true;
    }
    g_last_report.clear();
    if (report_id != 0)
        g_last_report.push_back(report_id);
    const uint8_t* p = static_cast<const uint8_t*>(report);
    g_last_report.insert(g_last_report.end(), p, p + len);
    return true;
}
bool tud_hid_n_keyboard_report(uint8_t, uint8_t, uint8_t modifiers, const uint8_t keys[6]) {
    g_kbm.modifiers = modifiers;
    std::memcpy(g_kbm.keys, keys, 6);
    return true;
}
bool tud_hid_n_mouse_report(uint8_t, uint8_t, uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan) {
    g_kbm.mouse_buttons = buttons;
    g_kbm.dx += x;
    g_kbm.dy += y;
    g_kbm.wheel += wheel;
    g_kbm.pan += pan;
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
namespace tud_xinput {
bool send_report_ready() { return true; }
bool send_report(const uint8_t* report, uint16_t len) {
    g_last_report.assign(report, report + len);
    return true;
}
bool receive_report(uint8_t*, uint16_t) { return false; }
const usbd_class_driver_t* class_driver() {
    static const usbd_class_driver_t driver{};
    return &driver;
}
}  // namespace tud_xinput
namespace tud_xid {
void initialize(Type) {}
const usbd_class_driver_t* class_driver() {
    static const usbd_class_driver_t driver{};
    return &driver;
}
uint8_t get_index_by_type(uint8_t, Type) { return 0; }
bool receive_report(uint8_t, uint8_t*, uint16_t) { return false; }
bool send_report(uint8_t, const uint8_t* report, uint16_t len) {
    g_last_report.assign(report, report + len);
    return true;
}
bool send_report_ready(uint8_t) { return true; }
bool xremote_rom_available() { return g_xremote_rom; }
}  // namespace tud_xid
namespace board_api {
uint32_t ms_since_boot() { return static_cast<uint32_t>(mock_time_us / 1000); }
void set_led(bool) {}
}  // namespace board_api
uint16_t* DeviceDriver::get_string_descriptor(const char*, uint8_t) {
    static uint16_t desc[32]{};
    return desc;
}

namespace {

int g_failures = 0;
void fail(const std::string& what) {
    std::printf("  FAIL %s\n", what.c_str());
    ++g_failures;
}

/* ---- fixture files ------------------------------------------------------------------------ */


struct Fixture {
    std::string name;
    int controller_type = 0;
    uint16_t vid = 0, pid = 0;
    std::vector<FixtureRow> rows;
};

std::string field(const std::string& line, const std::string& key) {
    const std::string k = "\"" + key + "\": ";
    const size_t at = line.find(k);
    if (at == std::string::npos)
        return "";
    size_t i = at + k.size();
    if (line[i] == '"') {
        const size_t end = line.find('"', i + 1);
        return line.substr(i + 1, end - i - 1);
    }
    size_t end = line.find_first_of(",}", i);
    return line.substr(i, end - i);
}

bool load(const std::string& path, Fixture& fx) {
    FILE* p = popen(("gzip -dc '" + path + "'").c_str(), "r");
    if (!p)
        return false;
    std::string text;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0)
        text.append(buf, n);
    pclose(p);
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line))
        return false;
    fx.name = field(line, "name");
    const std::string ct = field(line, "controller_type");
    fx.controller_type = (ct.empty() || ct == "null") ? 0 : std::stoi(ct);
    fx.vid = static_cast<uint16_t>(std::stoul(field(line, "vid"), nullptr, 16));
    fx.pid = static_cast<uint16_t>(std::stoul(field(line, "pid"), nullptr, 16));
    while (std::getline(in, line)) {
        FixtureRow r;
        r.t = static_cast<uint32_t>(std::stoul(field(line, "t")));
        r.slot = std::stoi(field(line, "slot"));
        const std::string l = field(line, "label");
        r.label = (l == "null") ? "" : l;
        r.feature = field(line, "kind") == "feature";
        const std::string hex = field(line, "r");
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
            r.r.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        fx.rows.push_back(std::move(r));
    }
    return true;
}

/* ---- what the host got, per recorded action ---------------------------------------------- */

struct Summary {
    std::set<std::string> buttons;
    std::set<int> dpad;
    int lx[2]{255, 0}, ly[2]{255, 0}, rx[2]{255, 0}, ry[2]{255, 0};
    int l2 = 0, r2 = 0;
    double gyro[3]{};
    bool has_gyro = false;
    std::map<std::string, std::array<int, 2>> extra;  // other axes an output has, min..max
    std::map<std::string, long> total;                 // relative ones (mouse), summed
    uint32_t last_t = 0;
    int reports = 0;
};

void axis(int v, int (&mm)[2]) {
    mm[0] = std::min(mm[0], v);
    mm[1] = std::max(mm[1], v);
}

void extra_axis(Summary& s, const std::string& name, int v) {
    auto it = s.extra.try_emplace(name, std::array<int, 2>{v, v}).first;
    it->second[0] = std::min(it->second[0], v);
    it->second[1] = std::max(it->second[1], v);
}

/* A struct handed to a console's bus loop, as the report bytes. */
template <typename T>
void as_report(const T& state) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&state);
    g_last_report.assign(p, p + sizeof(T));
}

template <typename T>
bool from_report(const std::vector<uint8_t>& r, T& out) {
    if (r.size() < sizeof(T))
        return false;
    std::memcpy(&out, r.data(), sizeof(T));
    return true;
}

int16_t le16(const std::vector<uint8_t>& r, size_t at) {
    return static_cast<int16_t>(r[at] | (r[at + 1] << 8));
}

/* DS4 USB report 0x01 (PS4 mode) or DualSense USB report 0x01 (STEAM mode). */
void decode_sony(const std::vector<uint8_t>& r, bool dualsense, Summary& s, uint32_t t) {
    if (r.size() < 30)
        return;
    const size_t b0 = dualsense ? 8 : 5;
    axis(r[1], s.lx);
    axis(r[2], s.ly);
    axis(r[3], s.rx);
    axis(r[4], s.ry);
    s.l2 = std::max<int>(s.l2, dualsense ? r[5] : r[8]);
    s.r2 = std::max<int>(s.r2, dualsense ? r[6] : r[9]);
    if ((r[b0] & 0x0F) != 8)
        s.dpad.insert(r[b0] & 0x0F);
    static const char* n0[] = {"square", "cross", "circle", "triangle"};
    for (int i = 0; i < 4; ++i)
        if (r[b0] & (0x10 << i)) s.buttons.insert(n0[i]);
    static const char* n1[] = {"L1", "R1", "L2", "R2", "share", "options", "L3", "R3"};
    for (int i = 0; i < 8; ++i)
        if (r[b0 + 1] & (1 << i)) s.buttons.insert(n1[i]);
    if (r[b0 + 2] & 0x01) s.buttons.insert("PS");
    if (r[b0 + 2] & 0x02) s.buttons.insert("touchpad");
    if (dualsense && (r[b0 + 2] & 0x04)) s.buttons.insert("mute");
    const size_t g = dualsense ? sony_reports::ds5::kGyro : sony_reports::ds4::kGyro;
    if (s.reports > 0 && r.size() >= g + 6) {
        const double dt = (t - s.last_t) / 1000.0;
        for (int i = 0; i < 3; ++i)
            s.gyro[i] += le16(r, g + 2 * i) * dt;
        s.has_gyro = true;
    }
}

void decode_dinput(const std::vector<uint8_t>& r, Summary& s) {
    if (r.size() < sizeof(DInput::InReport))
        return;
    static const char* n0[] = {"square", "cross", "circle", "triangle", "L1", "R1", "L2", "R2"};
    for (int i = 0; i < 8; ++i)
        if (r[0] & (1 << i)) s.buttons.insert(n0[i]);
    static const char* n1[] = {"select", "start", "L3", "R3", "PS", "touchpad"};
    for (int i = 0; i < 6; ++i)
        if (r[1] & (1 << i)) s.buttons.insert(n1[i]);
    if ((r[2] & DInput::DPAD_MASK) != DInput::DPad::CENTER)
        s.dpad.insert(r[2] & DInput::DPAD_MASK);
    axis(r[3], s.lx);
    axis(r[4], s.ly);
    axis(r[5], s.rx);
    axis(r[6], s.ry);
    s.l2 = std::max<int>(s.l2, r[17]);
    s.r2 = std::max<int>(s.r2, r[18]);
}

/* The 0..7 hat (0 up, clockwise; -1 none) of four direction bits. */
int hat_of(bool up, bool right, bool down, bool left) {
    static const int hat[3][3] = {{7, 0, 1}, {6, -1, 2}, {5, 4, 3}};  // [down - up + 1][right - left + 1]
    return hat[1 + down - up][1 + right - left];
}

/* Switch Pro report 0x30 (Switch mode), sticks as 0..255 with up = 0 through the factory
 * calibration the driver declares (as the Switch reads them); the gyro reordered to
 * pitch, yaw, roll as the Sony reports (the Switch's x runs along the pad, z points up). */
void decode_switch(const std::vector<uint8_t>& r, Summary& s, uint32_t t) {
    if (r.size() < 25 || r[0] != 0x30)
        return;
    static const char* nr[] = {"Y", "X", "B", "A", "SR", "SL", "R", "ZR"};
    static const char* ns[] = {"minus", "plus", "RS", "LS", "home", "capture"};
    static const char* nl[] = {"down", "up", "right", "left", "SR", "SL", "L", "ZL"};
    for (int i = 0; i < 8; ++i)
        if (r[3] & (1 << i)) s.buttons.insert(nr[i]);
    for (int i = 0; i < 6; ++i)
        if (r[4] & (1 << i)) s.buttons.insert(ns[i]);
    for (int i = 4; i < 8; ++i)
        if (r[5] & (1 << i)) s.buttons.insert(nl[i]);
    const int h = hat_of(r[5] & 0x02, r[5] & 0x04, r[5] & 0x01, r[5] & 0x08);
    if (h >= 0) s.dpad.insert(h);
    auto calibrated = [](int v12) {
        const double v = 127.5 + (v12 - switch_spi::kStickCenter) * 127.5 / switch_spi::kStickCalRange;
        return static_cast<int>(std::lround(std::clamp(v, 0.0, 255.0)));
    };
    auto stick = [&](size_t at, int (&x)[2], int (&y)[2]) {
        axis(calibrated(r[at] | ((r[at + 1] & 0x0F) << 8)), x);
        axis(255 - calibrated((r[at + 1] >> 4) | (r[at + 2] << 4)), y);
    };
    stick(6, s.lx, s.ly);
    stick(9, s.rx, s.ry);
    if (r[5] & 0x80) s.l2 = 255;
    if (r[3] & 0x80) s.r2 = 255;
    if (s.reports > 0) {
        const double dt = (t - s.last_t) / 1000.0;
        const int order[3] = {1, 2, 0};
        for (int i = 0; i < 3; ++i)
            s.gyro[i] += le16(r, 19 + 2 * order[i]) * dt;
        s.has_gyro = true;
    }
}

/* XInput report (XInput mode), sticks as 0..255 with up = 0. */
void decode_xinput(const std::vector<uint8_t>& r, Summary& s) {
    if (r.size() < sizeof(XInput::InReport))
        return;
    static const char* n0[] = {nullptr, nullptr, nullptr, nullptr, "start", "back", "LS", "RS"};
    static const char* n1[] = {"LB", "RB", "guide", nullptr, "A", "B", "X", "Y"};
    for (int i = 0; i < 8; ++i) {
        if (n0[i] && (r[2] & (1 << i))) s.buttons.insert(n0[i]);
        if (n1[i] && (r[3] & (1 << i))) s.buttons.insert(n1[i]);
    }
    const int h = hat_of(r[2] & 0x01, r[2] & 0x08, r[2] & 0x02, r[2] & 0x04);
    if (h >= 0) s.dpad.insert(h);
    s.l2 = std::max<int>(s.l2, r[4]);
    s.r2 = std::max<int>(s.r2, r[5]);
    auto x8 = [](int16_t v) { return (v + 32768) >> 8; };
    axis(x8(le16(r, 6)), s.lx);
    axis(255 - x8(le16(r, 8)), s.ly);
    axis(x8(le16(r, 10)), s.rx);
    axis(255 - x8(le16(r, 12)), s.ry);
}

/* DualShock 3 report 0x01 (PS3 mode). */
void decode_ps3(const std::vector<uint8_t>& r, Summary& s) {
    if (r.size() < sizeof(PS3::InReport))
        return;
    const size_t b = offsetof(PS3::InReport, buttons);
    static const char* n0[] = {"select", "L3", "R3", "start", nullptr, nullptr, nullptr, nullptr};
    static const char* n1[] = {"L2", "R2", "L1", "R1", "triangle", "circle", "cross", "square"};
    for (int i = 0; i < 8; ++i) {
        if (n0[i] && (r[b] & (1 << i))) s.buttons.insert(n0[i]);
        if (r[b + 1] & (1 << i)) s.buttons.insert(n1[i]);
    }
    if (r[b + 2] & PS3::Buttons2::SYS) s.buttons.insert("PS");
    if (r[b + 2] & PS3::Buttons2::TP) s.buttons.insert("touchpad");
    const int h = hat_of(r[b] & PS3::Buttons0::DPAD_UP, r[b] & PS3::Buttons0::DPAD_RIGHT,
                         r[b] & PS3::Buttons0::DPAD_DOWN, r[b] & PS3::Buttons0::DPAD_LEFT);
    if (h >= 0) s.dpad.insert(h);
    axis(r[offsetof(PS3::InReport, joystick_lx)], s.lx);
    axis(r[offsetof(PS3::InReport, joystick_ly)], s.ly);
    axis(r[offsetof(PS3::InReport, joystick_rx)], s.rx);
    axis(r[offsetof(PS3::InReport, joystick_ry)], s.ry);
    s.l2 = std::max<int>(s.l2, r[offsetof(PS3::InReport, l2_axis)]);
    s.r2 = std::max<int>(s.r2, r[offsetof(PS3::InReport, r2_axis)]);
}

/* Original Xbox (Duke) report (Xbox OG mode): the face buttons and black / white are analog
 * (pressed past half), sticks as 0..255 with up = 0. */
void decode_xboxog(const std::vector<uint8_t>& r, Summary& s) {
    using XboxOG::GP::InReport;
    namespace B = XboxOG::GP::Buttons;
    if (r.size() < sizeof(InReport))
        return;
    const uint8_t d = r[offsetof(InReport, buttons)];
    if (d & B::START) s.buttons.insert("start");
    if (d & B::BACK) s.buttons.insert("back");
    if (d & B::L3) s.buttons.insert("LS");
    if (d & B::R3) s.buttons.insert("RS");
    const std::pair<size_t, const char*> analog[] = {
        {offsetof(InReport, a), "A"}, {offsetof(InReport, b), "B"}, {offsetof(InReport, x), "X"},
        {offsetof(InReport, y), "Y"}, {offsetof(InReport, white), "white"}, {offsetof(InReport, black), "black"}};
    for (const auto& [at, name] : analog)
        if (r[at] >= 0x80) s.buttons.insert(name);
    const int h = hat_of(d & B::DPAD_UP, d & B::DPAD_RIGHT, d & B::DPAD_DOWN, d & B::DPAD_LEFT);
    if (h >= 0) s.dpad.insert(h);
    s.l2 = std::max<int>(s.l2, r[offsetof(InReport, trigger_l)]);
    s.r2 = std::max<int>(s.r2, r[offsetof(InReport, trigger_r)]);
    auto x8 = [](int16_t v) { return (v + 32768) >> 8; };
    axis(x8(le16(r, offsetof(InReport, joystick_lx))), s.lx);
    axis(255 - x8(le16(r, offsetof(InReport, joystick_ly))), s.ly);
    axis(x8(le16(r, offsetof(InReport, joystick_rx))), s.rx);
    axis(255 - x8(le16(r, offsetof(InReport, joystick_ry))), s.ry);
}

/* Wii U GameCube adapter report 0x21 (Wii U mode), port 1; the GameCube's Y grows upward, shown
 * here as 0..255 with up = 0. */
void decode_wiiu(const std::vector<uint8_t>& r, Summary& s) {
    namespace B = WiiU::GCButtons;
    if (r.size() < 10 || r[0] != WiiU::HID_REPORT_ID)
        return;
    const uint8_t* p = &r[1];
    const std::pair<uint8_t, const char*> b1[] = {{B::A, "A"}, {B::B, "B"}, {B::X, "X"}, {B::Y, "Y"}};
    for (const auto& [bit, name] : b1)
        if (p[1] & bit) s.buttons.insert(name);
    const std::pair<uint8_t, const char*> b2[] = {{B::START, "start"}, {B::Z, "Z"}, {B::L, "L"}, {B::R, "R"}};
    for (const auto& [bit, name] : b2)
        if (p[2] & bit) s.buttons.insert(name);
    const int h = hat_of(p[1] & B::UP, p[1] & B::RIGHT, p[1] & B::DOWN, p[1] & B::LEFT);
    if (h >= 0) s.dpad.insert(h);
    axis(p[3], s.lx);
    axis(255 - p[4], s.ly);
    axis(p[5], s.rx);
    axis(255 - p[6], s.ry);
    s.l2 = std::max<int>(s.l2, p[7]);
    s.r2 = std::max<int>(s.r2, p[8]);
}

/* PlayStation Classic report (PS Classic mode): buttons and a D-pad code only; the triggers are
 * the L2 / R2 buttons (shown as a full trigger). */
void decode_psclassic(const std::vector<uint8_t>& r, Summary& s) {
    namespace B = PSClassic::Buttons;
    PSClassic::InReport in;
    if (!from_report(r, in))
        return;
    const std::pair<uint16_t, const char*> names[] = {
        {B::CROSS, "cross"}, {B::CIRCLE, "circle"}, {B::SQUARE, "square"}, {B::TRIANGLE, "triangle"},
        {B::L1, "L1"}, {B::R1, "R1"}, {B::L2, "L2"}, {B::R2, "R2"}, {B::SELECT, "select"}, {B::START, "start"}};
    for (const auto& [bit, name] : names)
        if (in.buttons & bit) s.buttons.insert(name);
    const std::pair<uint16_t, int> hats[] = {{B::UP, 0}, {B::UP_RIGHT, 1}, {B::RIGHT, 2}, {B::DOWN_RIGHT, 3},
                                             {B::DOWN, 4}, {B::DOWN_LEFT, 5}, {B::LEFT, 6}, {B::UP_LEFT, 7}};
    for (const auto& [code, hat] : hats)
        if ((in.buttons & PSClassic::DPAD_MASK) == code) s.dpad.insert(hat);
    if (in.buttons & B::L2) s.l2 = 255;
    if (in.buttons & B::R2) s.r2 = 255;
}

/* Xbox DVD remote report (Xbox OG remote mode): one button code at a time; the D-pad codes as the
 * D-pad, reverse / forward as full triggers (what the pad's triggers give). */
void decode_xremote(const std::vector<uint8_t>& r, Summary& s) {
    namespace C = XboxOG::XR::ButtonCode;
    XboxOG::XR::InReport in;
    if (!from_report(r, in))
        return;
    const std::pair<uint16_t, const char*> names[] = {
        {C::SELECT, "select"}, {C::INFO, "info"}, {C::DISPLAY, "display"}, {C::BACK, "back"},
        {C::SKIP_MINUS, "skip-"}, {C::SKIP_PLUS, "skip+"}, {C::STOP, "stop"}, {C::REVERSE, "reverse"},
        {C::FORWARD, "forward"}, {C::TITLE, "title"}, {C::PAUSE, "pause"}, {C::PLAY, "play"}, {C::MENU, "menu"}};
    const std::pair<uint16_t, int> hats[] = {{C::UP, 0}, {C::RIGHT, 2}, {C::DOWN, 4}, {C::LEFT, 6}};
    for (const auto& [code, hat] : hats)
        if (in.buttonCode == code) { s.dpad.insert(hat); return; }
    for (const auto& [code, name] : names)
        if (in.buttonCode == code) {
            s.buttons.insert(name);
            if (code == C::REVERSE) s.l2 = 255;
            if (code == C::FORWARD) s.r2 = 255;
            return;
        }
    char unknown[24];
    std::snprintf(unknown, sizeof(unknown), "code 0x%04X", in.buttonCode);
    s.buttons.insert(unknown);
}

/* Steel Battalion controller report (Xbox OG Steel Battalion mode): the buttons the pad drives,
 * the sight change stick as the left stick, the aiming (moved by the right stick) and the
 * rotation lever (D-pad left / right) as extra axes, the side / gas pedals as the triggers. */
void decode_steel_battalion(const std::vector<uint8_t>& r, Summary& s) {
    using XboxOG::SB::InReport;
    namespace B0 = XboxOG::SB::Buttons0;
    namespace B1 = XboxOG::SB::Buttons1;
    namespace B2 = XboxOG::SB::Buttons2;
    InReport in;
    if (!from_report(r, in))
        return;
    const std::tuple<int, uint16_t, const char*> names[] = {
        {0, B0::RIGHTJOYMAINWEAPON, "main weapon"}, {0, B0::RIGHTJOYFIRE, "fire"}, {0, B0::RIGHTJOYLOCKON, "lock-on"},
        {0, B0::EJECT, "eject"}, {0, B0::START, "start"}, {1, B1::CHAFF, "chaff"}, {1, B1::EXTINGUISHER, "extinguisher"},
        {1, B1::WEAPONCONMAGAZINE, "magazine"}, {1, B1::WASHING, "washing"}, {1, B1::COMM1, "comm1"},
        {2, B2::LEFTJOYSIGHTCHANGE, "sight change"}};
    for (const auto& [word, bit, name] : names)
        if (in.dButtons[word] & bit) s.buttons.insert(name);
    auto x8 = [](int16_t v) { return (v + 32768) >> 8; };
    axis(x8(in.sightChangeX), s.lx);
    axis(x8(in.sightChangeY), s.ly);
    extra_axis(s, "aimX", in.aimingX >> 8);
    extra_axis(s, "aimY", in.aimingY >> 8);
    extra_axis(s, "lever", x8(in.rotationLever));
    s.l2 = std::max<int>(s.l2, in.leftPedal >> 8);
    s.r2 = std::max<int>(s.r2, in.rightPedal >> 8);
}

/* GameCube report handed to the JoyBus loop (GameCube mode); its Y grows upward, shown with up = 0. */
void decode_gamecube(const std::vector<uint8_t>& r, Summary& s) {
    GCReport in;
    if (!from_report(r, in))
        return;
    const std::pair<uint8_t, const char*> b1[] = {{0x01, "A"}, {0x02, "B"}, {0x04, "X"}, {0x08, "Y"}, {0x10, "start"}};
    for (const auto& [bit, name] : b1)
        if (in.buttons1 & bit) s.buttons.insert(name);
    const std::pair<uint8_t, const char*> b2[] = {{0x10, "Z"}, {0x20, "R"}, {0x40, "L"}};
    for (const auto& [bit, name] : b2)
        if (in.buttons2 & bit) s.buttons.insert(name);
    const int h = hat_of(in.buttons2 & 0x08, in.buttons2 & 0x02, in.buttons2 & 0x04, in.buttons2 & 0x01);
    if (h >= 0) s.dpad.insert(h);
    axis(in.lx, s.lx);
    axis(255 - in.ly, s.ly);
    axis(in.rx, s.rx);
    axis(255 - in.ry, s.ry);
    s.l2 = std::max<int>(s.l2, in.l);
    s.r2 = std::max<int>(s.r2, in.r);
}

/* N64 report handed to the bus loop (N64 mode): no right stick (the C buttons) and no analog
 * triggers; the stick (-128..127, up positive) shown as 0..255 with up = 0. */
void decode_n64(const std::vector<uint8_t>& r, Summary& s) {
    N64Report in;
    if (!from_report(r, in))
        return;
    const std::pair<uint8_t, const char*> b1[] = {{0x80, "A"}, {0x40, "B"}, {0x20, "Z"}, {0x10, "start"}};
    for (const auto& [bit, name] : b1)
        if (in.buttons1 & bit) s.buttons.insert(name);
    const std::pair<uint8_t, const char*> b2[] = {{0x20, "L"}, {0x10, "R"}, {0x08, "C-up"}, {0x04, "C-down"},
                                                  {0x02, "C-left"}, {0x01, "C-right"}};
    for (const auto& [bit, name] : b2)
        if (in.buttons2 & bit) s.buttons.insert(name);
    const int h = hat_of(in.buttons1 & 0x08, in.buttons1 & 0x01, in.buttons1 & 0x04, in.buttons1 & 0x02);
    if (h >= 0) s.dpad.insert(h);
    axis(in.joy_x + 128, s.lx);
    axis(128 - in.joy_y, s.ly);
}

/* PS1 / PS2 pad state handed to the SPI loop (PS1/PS2 mode): buttons active low, sticks 0..255
 * with up = 0 as the DualShock 2 sends them. */
void decode_psx(const std::vector<uint8_t>& r, Summary& s) {
    PSXInputState in;
    if (!from_report(r, in))
        return;
    const uint8_t b1 = ~in.buttons1, b2 = ~in.buttons2;
    const std::pair<uint8_t, const char*> n1[] = {{0x01, "select"}, {0x02, "L3"}, {0x04, "R3"}, {0x08, "start"}};
    for (const auto& [bit, name] : n1)
        if (b1 & bit) s.buttons.insert(name);
    const std::pair<uint8_t, const char*> n2[] = {{0x01, "L2"}, {0x02, "R2"}, {0x04, "L1"}, {0x08, "R1"},
                                                  {0x10, "triangle"}, {0x20, "circle"}, {0x40, "cross"}, {0x80, "square"}};
    for (const auto& [bit, name] : n2)
        if (b2 & bit) s.buttons.insert(name);
    const int h = hat_of(b1 & 0x10, b1 & 0x20, b1 & 0x40, b1 & 0x80);
    if (h >= 0) s.dpad.insert(h);
    axis(in.lx, s.lx);
    axis(in.ly, s.ly);
    axis(in.rx, s.rx);
    axis(in.ry, s.ry);
    s.l2 = std::max<int>(s.l2, in.l2);
    s.r2 = std::max<int>(s.r2, in.r2);
}

/* Dreamcast controller condition handed to the Maple Bus loop (Dreamcast mode): buttons active
 * low, sticks 0..255 with up = 0. */
void decode_dreamcast(const std::vector<uint8_t>& r, Summary& s) {
    controller_condition_t in;
    if (!from_report(r, in))
        return;
    const std::pair<bool, const char*> names[] = {{!in.a, "A"}, {!in.b, "B"}, {!in.x, "X"}, {!in.y, "Y"},
                                                  {!in.c, "C"}, {!in.z, "Z"}, {!in.d, "D"}, {!in.start, "start"}};
    for (const auto& [pressed, name] : names)
        if (pressed) s.buttons.insert(name);
    const int h = hat_of(!in.up, !in.right, !in.down, !in.left);
    if (h >= 0) s.dpad.insert(h);
    axis(in.lAnalogLR, s.lx);
    axis(in.lAnalogUD, s.ly);
    axis(in.rAnalogLR, s.rx);
    axis(in.rAnalogUD, s.ry);
    s.l2 = std::max<int>(s.l2, in.l);
    s.r2 = std::max<int>(s.r2, in.r);
}

/* The Wiimote state the Bluetooth emulation sends (Wii mode), with its extension: the IR pointer
 * (-128..127, up positive), the Nunchuk stick (0..255, up high), the Classic Controller's sticks
 * (left 0..63, right 0..30, up high), each shown in its own units with up low. */
void decode_wii(const std::vector<uint8_t>& r, Summary& s) {
    WiimoteReport in;
    if (!from_report(r, in))
        return;
    auto stick = [&](int x, int y, int y_top, int (&sx)[2], int (&sy)[2]) {
        axis(x, sx);
        axis(y_top - y, sy);
    };
    if (in.mode == CLASSIC_CONTROLLER) {
        const wiimote_classic& c = in.classic;
        const std::pair<bool, const char*> names[] = {
            {c.a, "a"}, {c.b, "b"}, {c.x, "x"}, {c.y, "y"}, {c.minus, "minus"}, {c.plus, "plus"},
            {c.home, "home"}, {c.lz, "ZL"}, {c.rz, "ZR"}, {c.ltrigger, "L"}, {c.rtrigger, "R"}};
        for (const auto& [pressed, name] : names)
            if (pressed) s.buttons.insert(name);
        const int h = hat_of(c.up, c.right, c.down, c.left);
        if (h >= 0) s.dpad.insert(h);
        stick(c.ls_x, c.ls_y, 63, s.lx, s.ly);
        stick(c.rs_x, c.rs_y, 30, s.rx, s.ry);
        s.l2 = std::max<int>(s.l2, c.lt);
        s.r2 = std::max<int>(s.r2, c.rt);
        return;
    }
    const wiimote_buttons& w = in.wiimote;
    const std::pair<bool, const char*> names[] = {{w.a, "A"}, {w.b, "B"}, {w.one, "1"}, {w.two, "2"},
                                                  {w.minus, "minus"}, {w.plus, "plus"}, {w.home, "home"}};
    for (const auto& [pressed, name] : names)
        if (pressed) s.buttons.insert(name);
    const int h = hat_of(w.up, w.right, w.down, w.left);
    if (h >= 0) s.dpad.insert(h);
    if (in.mode == WIIMOTE_AND_NUNCHUCK) {
        if (in.nunchuk.c) s.buttons.insert("C");
        if (in.nunchuk.z) s.buttons.insert("Z");
        stick(in.nunchuk.x, in.nunchuk.y, 255, s.lx, s.ly);
        stick(w.ir_x + 128, w.ir_y + 128, 255, s.rx, s.ry);
    } else {
        stick(w.ir_x + 128, w.ir_y + 128, 255, s.lx, s.ly);
    }
}

/* The keyboard, mouse and media reports (Mouse + Keyboard mode): keys held, mouse buttons and
 * the media key as buttons, the pointer / wheel movement summed. */
void decode_kbm(Summary& s) {
    static const std::map<uint8_t, const char*> keys = {
        {0x28, "Enter"}, {0x29, "Escape"}, {0x2A, "Backspace"}, {0x2B, "Tab"}, {0x2C, "Space"},
        {0x4F, "Right"}, {0x50, "Left"}, {0x51, "Down"}, {0x52, "Up"}, {0x65, "Menu"}};
    static const char* mods[] = {"LCtrl", "LShift", "LAlt", "LGUI", "RCtrl", "RShift", "RAlt", "RGUI"};
    static const char* mouse[] = {"mouse left", "mouse right", "mouse middle", "mouse back", "mouse forward"};
    for (int i = 0; i < 8; ++i)
        if (g_kbm.modifiers & (1 << i)) s.buttons.insert(mods[i]);
    for (uint8_t k : g_kbm.keys) {
        if (k == 0) continue;
        const auto it = keys.find(k);
        char name[16];
        std::snprintf(name, sizeof(name), "key 0x%02X", k);
        s.buttons.insert(it != keys.end() ? it->second : name);
    }
    for (int i = 0; i < 5; ++i)
        if (g_kbm.mouse_buttons & (1 << i)) s.buttons.insert(mouse[i]);
    if (g_kbm.media) {
        char name[20];
        std::snprintf(name, sizeof(name), "media 0x%04X", g_kbm.media);
        s.buttons.insert(name);
    }
    s.total["mouseX"] += g_kbm.dx;
    s.total["mouseY"] += g_kbm.dy;
    s.total["wheel"] += g_kbm.wheel;
    s.total["pan"] += g_kbm.pan;
    g_kbm.dx = g_kbm.dy = g_kbm.wheel = g_kbm.pan = 0;
}

std::string text_of(const std::map<std::string, Summary>& by_label, const std::vector<std::string>& order) {
    std::ostringstream o;
    for (const std::string& label : order) {
        const auto it = by_label.find(label);
        if (it == by_label.end())
            continue;
        const Summary& s = it->second;
        o << label << ": buttons=";
        bool first = true;
        for (const auto& b : s.buttons) { o << (first ? "" : ",") << b; first = false; }
        o << " dpad=";
        first = true;
        for (int d : s.dpad) { o << (first ? "" : ",") << d; first = false; }
        if (s.lx[0] <= s.lx[1])
            o << " LX=" << s.lx[0] << ".." << s.lx[1] << " LY=" << s.ly[0] << ".." << s.ly[1];
        if (s.rx[0] <= s.rx[1])
            o << " RX=" << s.rx[0] << ".." << s.rx[1] << " RY=" << s.ry[0] << ".." << s.ry[1];
        o << " L2=" << s.l2 << " R2=" << s.r2;
        for (const auto& [name, mm] : s.extra)
            o << " " << name << "=" << mm[0] << ".." << mm[1];
        for (const auto& [name, v] : s.total)
            o << " " << name << "=" << v;
        if (s.has_gyro) {
            char g[96];
            std::snprintf(g, sizeof(g), " gyro=%.0f,%.0f,%.0f", s.gyro[0], s.gyro[1], s.gyro[2]);
            o << g;
        }
        o << "\n";
    }
    return o.str();
}

/* ---- what each action must give ----------------------------------------------------------- */

/* A stick's full range in the units the summary shows it (up low). An edge may stop one step
 * short of 0: Bluepad32 centres a DS4 stick on 127 ((v - 127) * 4, so -508..512) and a full push
 * up or left comes out one step short. */
struct StickRange {
    int lo = 1, hi = 255, y_lo = 1, y_hi = 255;
};
/* The GPIO consoles scale a stick to +-127 around 128 (1..255), so the DS4's step short is 2. */
constexpr StickRange kPlus127{2, 255, 2, 255};

/* What each mode calls the buttons of the script's Xbox-layout labels (by position: "a" is the
 * bottom face button); nullptr: the mode has no such button, the action gives none. `touchpad`:
 * its touchpad click, or none. `misc`: what the Misc button (Capture; a touchpad press where the
 * output has no touchpad) gives. `home_chord`: Home goes out as these buttons with both triggers
 * full (the in-game reset of the Xbox OG and PS2). The flags: which of the pad's sticks, D-pad and
 * analog triggers the output carries as such. */
struct Names {
    const char *a, *b, *x, *y, *lb, *rb, *home, *l3, *r3, *back, *start, *touchpad, *misc;
    std::vector<std::string> home_chord{};
    StickRange ls{}, rs{};
    bool l_stick = true, r_stick = true, dpad = true, triggers = true;
    bool sticks_as_dpad = false;  // PS Classic: either stick pushed is the D-pad
    bool rs_as_c = false;         // N64: the right stick is the C buttons
    const char* lt_button = nullptr;  // a trigger pressed is this button (the Nunchuk's C)
};
const Names kPlayStation{"cross", "circle", "square", "triangle", "L1", "R1", "PS", "L3", "R3", "share", "options", "touchpad", "touchpad"};
const Names kSteam{"cross", "circle", "square", "triangle", "L1", "R1", "PS", "L3", "R3", "share", "options", "touchpad", "mute"};
const Names kDInput{"cross", "circle", "square", "triangle", "L1", "R1", "PS", "L3", "R3", "select", "start", nullptr, "touchpad"};
const Names kSwitch{"B", "A", "Y", "X", "L", "R", "home", "LS", "RS", "minus", "plus", nullptr, "capture"};
const Names kXInput{"A", "B", "X", "Y", "LB", "RB", "guide", "LS", "RS", "back", "start", nullptr, nullptr};
const Names kPS3{"cross", "circle", "square", "triangle", "L1", "R1", "PS", "L3", "R3", "select", "start", "touchpad", "touchpad"};
const Names kXboxOG{"A", "B", "X", "Y", "white", "black", nullptr, "LS", "RS", "back", "start", nullptr, nullptr, {"start", "back"}};
const Names kWiiU{.a = "B", .b = "A", .x = "Y", .y = "X", .lb = nullptr, .rb = "Z", .home = nullptr, .l3 = nullptr,
                  .r3 = nullptr, .back = nullptr, .start = "start", .touchpad = nullptr, .misc = nullptr,
                  .ls = {1, 255, 1, 254}, .rs = {1, 255, 1, 254}};  // the GameCube Y never sends 0 / 255
const Names kPSClassic{.a = "cross", .b = "circle", .x = "square", .y = "triangle", .lb = "L1", .rb = "R1",
                       .home = nullptr, .l3 = nullptr, .r3 = nullptr, .back = "select", .start = "start",
                       .touchpad = nullptr, .misc = nullptr, .l_stick = false, .r_stick = false, .sticks_as_dpad = true};
const Names kXRemote{.a = "select", .b = "back", .x = "display", .y = "pause", .lb = "skip-", .rb = "skip+",
                     .home = "display", .l3 = "title", .r3 = "menu", .back = "stop", .start = "play",
                     .touchpad = nullptr, .misc = nullptr, .l_stick = false, .r_stick = false};
const Names kSteelBattalion{.a = "main weapon", .b = "lock-on", .x = nullptr, .y = "chaff", .lb = "fire",
                            .rb = "main weapon", .home = "eject", .l3 = "sight change", .r3 = "lock-on", .back = nullptr,
                            .start = "start", .touchpad = nullptr, .misc = nullptr,
                            .r_stick = false, .dpad = false};  // the right stick moves the aim, the D-pad the lever
const Names kGameCube{.a = "A", .b = "B", .x = "X", .y = "Y", .lb = "L", .rb = "R", .home = nullptr, .l3 = nullptr,
                      .r3 = nullptr, .back = nullptr, .start = "start", .touchpad = nullptr, .misc = "Z",
                      .ls = {2, 255, 1, 254}, .rs = {2, 255, 1, 254}};  // +-127 around 128, Y grows upward
const Names kN64{.a = "A", .b = "B", .x = nullptr, .y = nullptr, .lb = "L", .rb = "R", .home = nullptr, .l3 = nullptr,
                 .r3 = nullptr, .back = nullptr, .start = "start", .touchpad = nullptr, .misc = "Z",
                 .ls = kPlus127, .r_stick = false, .triggers = false, .rs_as_c = true};
const Names kPSX{"cross", "circle", "square", "triangle", "L1", "R1", nullptr, "L3", "R3", "select", "start", nullptr, nullptr,
                 {"L1", "R1", "L2", "R2", "start", "select"}, kPlus127, kPlus127};
const Names kDreamcast{.a = "A", .b = "B", .x = "X", .y = "Y", .lb = "Z", .rb = "C", .home = nullptr, .l3 = nullptr,
                       .r3 = nullptr, .back = nullptr, .start = "start", .touchpad = nullptr, .misc = "D",
                       .ls = kPlus127, .rs = kPlus127};
const Names kWiimote{.a = "A", .b = "B", .x = "1", .y = "2", .lb = nullptr, .rb = nullptr, .home = "home", .l3 = nullptr,
                     .r3 = nullptr, .back = "minus", .start = "plus", .touchpad = nullptr, .misc = nullptr,
                     .r_stick = false, .triggers = false};  // the left stick is the IR pointer
const Names kNunchuk{.a = "A", .b = "B", .x = "1", .y = "2", .lb = nullptr, .rb = "Z", .home = "home", .l3 = nullptr,
                     .r3 = nullptr, .back = "minus", .start = "plus", .touchpad = nullptr, .misc = nullptr,
                     .triggers = false, .lt_button = "C"};  // the right stick is the IR pointer
const Names kClassic{.a = "a", .b = "b", .x = "y", .y = "x", .lb = "ZL", .rb = "ZR", .home = "home", .l3 = nullptr,
                     .r3 = nullptr, .back = "minus", .start = "plus", .touchpad = nullptr, .misc = nullptr,
                     .ls = {1, 62, 1, 62}, .rs = {0, 29, 1, 30}};  // 6-bit left, 5-bit right stick, as the converter scales them

/* `touchpad` / `capture`: the pad has a touchpad / a Capture button. */
void check(const std::string& where, const std::map<std::string, Summary>& by, const Names& n, bool touchpad,
           bool capture) {
    auto has = [&](const char* label) { return by.count(label) != 0; };
    const bool chord = !n.home_chord.empty();
    auto only = [&](const char* label, const char* button) {  // the action gives that button and no other
        const Summary& s = by.at(label);
        if (button && !s.buttons.count(button))
            fail(where + " " + label + ": " + button + " not pressed");
        for (const auto& b : s.buttons) {
            /* Clicking a stick moves it a little: on the N64 the right one past the C buttons' threshold. */
            if (n.rs_as_c && std::strcmp(label, "r3") == 0 && b.rfind("C-", 0) == 0) continue;
            if (!button || b != button) fail(where + " " + label + ": also " + b);
        }
    };
    const std::pair<const char*, const char*> buttons[] = {
        {"a", n.a}, {"b", n.b}, {"x", n.x}, {"y", n.y}, {"lb", n.lb}, {"rb", n.rb},
        {"home", n.home}, {"l3", n.l3}, {"r3", n.r3}, {"back", n.back}, {"start", n.start},
    };
    for (const auto& [label, button] : buttons)
        if (has(label) && !(chord && std::strcmp(label, "home") == 0))
            only(label, button);
    if (chord && has("home")) {  // the reset chord: both triggers full and these buttons
        const Summary& s = by.at("home");
        const std::set<std::string> want(n.home_chord.begin(), n.home_chord.end());
        if (s.l2 != 255 || s.r2 != 255 || s.buttons != want)
            fail(where + " home: not the in-game reset chord");
    }
    if (has("capture"))
        only("capture", capture ? n.misc : nullptr);
    if (n.dpad) {
        const std::pair<const char*, int> dpads[] = {{"dpad_up", 0}, {"dpad_right", 2}, {"dpad_down", 4}, {"dpad_left", 6}};
        for (const auto& [label, dir] : dpads)
            if (has(label) && !by.at(label).dpad.count(dir)) fail(where + " " + label + ": D-pad " + std::to_string(dir));
    }
    if (n.triggers) {
        if (has("lt_full") && by.at("lt_full").l2 != 255) fail(where + " lt_full: L2 " + std::to_string(by.at("lt_full").l2));
        if (has("rt_full") && by.at("rt_full").r2 != 255) fail(where + " rt_full: R2 " + std::to_string(by.at("rt_full").r2));
    }
    if (n.lt_button && has("lt_full") && !by.at("lt_full").buttons.count(n.lt_button))
        fail(where + " lt_full: " + n.lt_button + " not pressed");
    /* A stick: pushed right / up it moves that way; over its steps (right, up, circle) it reaches
     * every edge. */
    for (const bool left : {true, false}) {
        if (!(left ? n.l_stick : n.r_stick))
            continue;
        const StickRange& range = left ? n.ls : n.rs;
        const std::string side = left ? "ls" : "rs";
        auto toward = [&](const std::string& label, const char* axis, int v, int lo, int hi, bool high) {
            const int span = hi - lo;
            if (has(label.c_str()) && (high ? v < lo + span * 7 / 8 : v > lo + span / 8))
                fail(where + " " + label + ": " + axis + (high ? " max " : " min ") + std::to_string(v));
        };
        if (has((side + "_right").c_str())) {
            const Summary& st = by.at(side + "_right");
            toward(side + "_right", left ? "LX" : "RX", (left ? st.lx : st.rx)[1], range.lo, range.hi, true);
        }
        if (has((side + "_up").c_str())) {
            const Summary& st = by.at(side + "_up");
            toward(side + "_up", left ? "LY" : "RY", (left ? st.ly : st.ry)[0], range.y_lo, range.y_hi, false);
        }
        int x[2]{1 << 20, -(1 << 20)}, y[2]{1 << 20, -(1 << 20)};
        bool any = false;
        for (const char* step : {"_right", "_up", "_circle"}) {
            const std::string label = side + step;
            if (!has(label.c_str())) continue;
            const Summary& st = by.at(label);
            const int* sx = left ? st.lx : st.rx;
            const int* sy = left ? st.ly : st.ry;
            x[0] = std::min(x[0], sx[0]); x[1] = std::max(x[1], sx[1]);
            y[0] = std::min(y[0], sy[0]); y[1] = std::max(y[1], sy[1]);
            any = true;
        }
        if (any && (x[0] > range.lo || x[1] < range.hi || y[0] > range.y_lo || y[1] < range.y_hi))
            fail(where + (left ? " left" : " right") + " stick range " + std::to_string(x[0]) + ".." +
                 std::to_string(x[1]) + " / " + std::to_string(y[0]) + ".." + std::to_string(y[1]));
    }
    if (n.sticks_as_dpad)
        for (const char* side : {"ls", "rs"}) {
            const std::string right = std::string(side) + "_right", up = std::string(side) + "_up";
            if (has(right.c_str()) && !by.at(right).dpad.count(2)) fail(where + " " + right + ": not D-pad right");
            if (has(up.c_str()) && !by.at(up).dpad.count(0)) fail(where + " " + up + ": not D-pad up");
        }
    if (n.rs_as_c) {
        if (has("rs_right") && !by.at("rs_right").buttons.count("C-right")) fail(where + " rs_right: C-right not pressed");
        if (has("rs_up") && !by.at("rs_up").buttons.count("C-up")) fail(where + " rs_up: C-up not pressed");
    }
    if (touchpad && has("touch_press")) {
        const char* want = n.touchpad ? n.touchpad : n.misc;
        if (want && !by.at("touch_press").buttons.count(want))
            fail(where + " touch_press: " + want + " not pressed");
    }
    if (has("idle") && !by.at("idle").buttons.empty()) fail(where + " idle: a button pressed");
    /* A turn: the motion integrated over it is largest on the turn's own axis. */
    const std::pair<const char*, int> turns[] = {{"pitch_up_90", 0}, {"yaw_right_90", 1}, {"roll_right_90", 2}};
    for (const auto& [label, ax] : turns) {
        if (!has(label) || !by.at(label).has_gyro) continue;
        const double* g = by.at(label).gyro;
        for (int i = 0; i < 3; ++i)
            if (i != ax && std::fabs(g[i]) > std::fabs(g[ax]))
                fail(where + " " + label + ": the largest turn is on axis " + std::to_string(i));
    }
}

/* Mouse + Keyboard mode with its default settings (KbmSettings.cpp: Steam's desktop layout). */
void check_kbm(const std::string& where, const std::map<std::string, Summary>& by, bool touchpad) {
    auto has = [&](const char* label) { return by.count(label) != 0; };
    const std::pair<const char*, std::set<std::string>> keys[] = {
        {"a", {"Enter"}}, {"b", {"Escape"}}, {"x", {"Backspace"}}, {"y", {"Space"}}, {"lb", {"LShift", "Tab"}},
        {"rb", {"Tab"}}, {"home", {"media 0x0223"}}, {"l3", {"LCtrl"}}, {"r3", {"LShift"}}, {"back", {"LGUI"}},
        {"start", {"Menu"}}, {"dpad_up", {"Up"}}, {"dpad_down", {"Down"}}, {"dpad_left", {"Left"}},
        {"dpad_right", {"Right"}}, {"lt_full", {"mouse right"}}, {"rt_full", {"mouse left"}}, {"idle", {}},
        {"capture", {}}};
    for (const auto& [label, want] : keys)
        if (has(label) && by.at(label).buttons != want) {
            std::string got;
            for (const auto& b : by.at(label).buttons) got += (got.empty() ? "" : ",") + b;
            fail(where + " " + label + ": " + (got.empty() ? "nothing" : got));
        }
    auto total = [&](const char* label, const char* name) { return by.at(label).total.at(name); };
    if (has("rs_right") && total("rs_right", "mouseX") <= 0) fail(where + " rs_right: the pointer did not move right");
    if (has("rs_up") && total("rs_up", "mouseY") >= 0) fail(where + " rs_up: the pointer did not move up");
    if (has("ls_up") && total("ls_up", "wheel") <= 0) fail(where + " ls_up: no scroll up");
    if (has("ls_right") && total("ls_right", "pan") <= 0) fail(where + " ls_right: no scroll right");
    if (has("idle") && (total("idle", "mouseX") || total("idle", "mouseY") || total("idle", "wheel")))
        fail(where + " idle: the pointer or the wheel moved");
    if (touchpad && has("touch_press") && !by.at("touch_press").buttons.count("mouse left"))
        fail(where + " touch_press: no left click");
    if (touchpad && has("touch_drag") && !total("touch_drag", "mouseX") && !total("touch_drag", "mouseY"))
        fail(where + " touch_drag: the pointer did not move");
}

/* ---- replay ------------------------------------------------------------------------------- */

enum class Mode {
    PS4, STEAM, DINPUT, SWITCH, XINPUT, PS3, XBOXOG, WIIU, PSCLASSIC, XBOXOG_XR, XBOXOG_SB, KBM,
    GAMECUBE, N64, PS1PS2, DREAMCAST, WII, WII_NUNCHUK, WII_CLASSIC
};
struct ModeInfo {
    Mode mode;
    const char* name;  // the golden files' suffix
    DeviceDriverType driver;
    const Names* names;  // nullptr: its own check (Mouse + Keyboard)
};
const ModeInfo kModes[] = {
    {Mode::PS4, "ps4", DeviceDriverType::PS4, &kPlayStation},
    {Mode::STEAM, "steam", DeviceDriverType::STEAM, &kSteam},
    {Mode::DINPUT, "dinput", DeviceDriverType::DINPUT_DEBUG, &kDInput},
    {Mode::SWITCH, "switch", DeviceDriverType::SWITCH, &kSwitch},
    {Mode::XINPUT, "xinput", DeviceDriverType::XINPUT, &kXInput},
    {Mode::PS3, "ps3", DeviceDriverType::PS3, &kPS3},
    {Mode::XBOXOG, "xboxog", DeviceDriverType::XBOXOG, &kXboxOG},
    {Mode::WIIU, "wiiu", DeviceDriverType::WIIU, &kWiiU},
    {Mode::PSCLASSIC, "psclassic", DeviceDriverType::PSCLASSIC, &kPSClassic},
    {Mode::XBOXOG_XR, "xboxog_xr", DeviceDriverType::XBOXOG_XR, &kXRemote},
    {Mode::XBOXOG_SB, "xboxog_sb", DeviceDriverType::XBOXOG_SB, &kSteelBattalion},
    {Mode::KBM, "kbm", DeviceDriverType::KBM, nullptr},
    {Mode::GAMECUBE, "gamecube", DeviceDriverType::GAMECUBE, &kGameCube},
    {Mode::N64, "n64", DeviceDriverType::N64, &kN64},
    {Mode::PS1PS2, "ps1ps2", DeviceDriverType::PS1PS2, &kPSX},
    {Mode::DREAMCAST, "dreamcast", DeviceDriverType::DREAMCAST, &kDreamcast},
    {Mode::WII, "wii", DeviceDriverType::WII, &kWiimote},
    {Mode::WII_NUNCHUK, "wii_nunchuk", DeviceDriverType::WII, &kNunchuk},
    {Mode::WII_CLASSIC, "wii_classic", DeviceDriverType::WII, &kClassic},
};

std::map<std::string, Summary> replay(const Fixture& fx, const ModeInfo& info, bool& known) {
    const Mode mode = info.mode;
    Gamepad gamepad;
    gamepad.set_profile(UserProfile(), info.driver);
    std::vector<ReplayFrame> frames;
    known = bt_replay(fx.rows, fx.controller_type, fx.vid, fx.pid, gamepad, frames);

    /* Value-initialised, as DeviceManager's make_unique: members initialize() leaves alone are 0
     * (the Steel Battalion's lamp state from the game, for one). */
    PS4Device ps4{};
    SteamDevice steam{};
    DInputDevice dinput{};
    SwitchDevice sw{};
    XInputDevice xinput{};
    PS3Device ps3{};
    XboxOGDevice xboxog{};
    WiiUDevice wiiu{};
    PSClassicDevice psclassic{};
    XboxOGXRDevice xremote{};
    XboxOGSBDevice steel_battalion{};
    KBMDevice kbm{};
    GameCubeDevice gamecube{};
    N64Device n64{};
    PS1PS2Device psx{};
    DreamcastDevice dreamcast{};
    DeviceDriver* dev = nullptr;  // none in Wii mode: the Wiimote emulation reads the converter's state
    switch (mode) {
        case Mode::PS4: dev = &ps4; break;
        case Mode::STEAM: dev = &steam; break;
        case Mode::DINPUT: dev = &dinput; break;
        case Mode::SWITCH: dev = &sw; break;
        case Mode::XINPUT: dev = &xinput; break;
        case Mode::PS3: dev = &ps3; break;
        case Mode::XBOXOG: dev = &xboxog; break;
        case Mode::WIIU: dev = &wiiu; break;
        case Mode::PSCLASSIC: dev = &psclassic; break;
        case Mode::XBOXOG_XR: dev = &xremote; break;
        case Mode::XBOXOG_SB: dev = &steel_battalion; break;
        case Mode::KBM: dev = &kbm; break;
        case Mode::GAMECUBE: dev = &gamecube; break;
        case Mode::N64: dev = &n64; break;
        case Mode::PS1PS2: dev = &psx; break;
        case Mode::DREAMCAST: dev = &dreamcast; break;
        default: break;
    }
    g_kbm = Kbm{};
    g_kbm_mode = mode == Mode::KBM;
    g_xremote_rom = mode == Mode::XBOXOG_XR;
    mock_time_us = 0;
    if (dev)
        dev->initialize();
    if (mode == Mode::SWITCH) {  // the host turns the IMU on (subcommand 0x40), as a Switch does
        const uint8_t enable_imu[12] = {0x01, 0, 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40, 0x40, 0x01};
        sw.set_report_cb(0, 0, HID_REPORT_TYPE_OUTPUT, enable_imu, sizeof(enable_imu));
    }
    WiimoteReport wiimote{};
    wiimote.mode = mode == Mode::WII_NUNCHUK ? WIIMOTE_AND_NUNCHUCK
                   : mode == Mode::WII_CLASSIC ? CLASSIC_CONTROLLER
                                               : NO_EXTENSION;
    SteamPassthrough::clear();
    std::map<std::string, Summary> by_label;
    for (const ReplayFrame& f : frames) {
        mock_time_us = static_cast<uint64_t>(f.t) * 1000;
        if (mode == Mode::STEAM)
            SteamBtReport::store_ps5_core(f.core);
        gamepad.set_pad_in_from_bluetooth(f.in);
        g_last_report.clear();
        if (dev)
            dev->process(0, gamepad);
        switch (mode) {  // the outputs without a USB report: the state their bus loop sends
            case Mode::GAMECUBE: as_report(gamecube.get_gc_report()); break;
            case Mode::N64: as_report(n64_report()); break;
            case Mode::PS1PS2: as_report(*psx_state()); break;
            case Mode::DREAMCAST: as_report(dreamcast_condition()); break;
            case Mode::WII: case Mode::WII_NUNCHUK: case Mode::WII_CLASSIC:
                Wii::gamepad_to_wiimote_report(gamepad, &wiimote);
                as_report(wiimote);
                break;
            default: break;
        }
        if (f.label->empty() || (g_last_report.empty() && mode != Mode::KBM))
            continue;
        Summary& s = by_label[*f.label];
        switch (mode) {
            case Mode::DINPUT: decode_dinput(g_last_report, s); break;
            case Mode::SWITCH: decode_switch(g_last_report, s, f.t); break;
            case Mode::XINPUT: decode_xinput(g_last_report, s); break;
            case Mode::PS3: decode_ps3(g_last_report, s); break;
            case Mode::XBOXOG: decode_xboxog(g_last_report, s); break;
            case Mode::WIIU: decode_wiiu(g_last_report, s); break;
            case Mode::PSCLASSIC: decode_psclassic(g_last_report, s); break;
            case Mode::XBOXOG_XR: decode_xremote(g_last_report, s); break;
            case Mode::XBOXOG_SB: decode_steel_battalion(g_last_report, s); break;
            case Mode::KBM: decode_kbm(s); break;
            case Mode::GAMECUBE: decode_gamecube(g_last_report, s); break;
            case Mode::N64: decode_n64(g_last_report, s); break;
            case Mode::PS1PS2: decode_psx(g_last_report, s); break;
            case Mode::DREAMCAST: decode_dreamcast(g_last_report, s); break;
            case Mode::WII: case Mode::WII_NUNCHUK: case Mode::WII_CLASSIC: decode_wii(g_last_report, s); break;
            default: decode_sony(g_last_report, mode == Mode::STEAM, s, f.t); break;
        }
        s.last_t = f.t;
        ++s.reports;
    }
    SteamPassthrough::clear();
    g_kbm_mode = g_xremote_rom = false;
    return by_label;
}

std::vector<std::string> label_order(const Fixture& fx) {
    std::vector<std::string> order;
    std::set<std::string> seen;
    for (const FixtureRow& r : fx.rows)
        if (!r.label.empty() && seen.insert(r.label).second)
            order.push_back(r.label);
    return order;
}

}  // namespace

int main(int argc, char** argv) {
    const bool write = argc > 1 && std::strcmp(argv[1], "--write") == 0;
    const std::string dir = OGXM_FIXTURES_DIR;
    // Pads whose Bluepad32 parser is wired in (replay_bt.cpp); *_synthetic: Tools/bench/synth_pad.py.
    const char* pads[] = {"ds4", "joycon_pair", "dualsense_synthetic", "switch_pro_synthetic"};
    for (const char* pad : pads) {
        Fixture fx;
        if (!load(dir + "/" + pad + ".jsonl.gz", fx)) {
            fail(std::string(pad) + ": fixture not readable");
            continue;
        }
        const std::vector<std::string> order = label_order(fx);
        for (const ModeInfo& info : kModes) {
            const std::string where = std::string(pad) + "." + info.name;
            bool known = false;
            const auto by = replay(fx, info, known);
            if (!known) {
                fail(where + ": no parser for controller type " + std::to_string(fx.controller_type));
                continue;
            }
            const bool touchpad = bt_has_touchpad(fx.controller_type);
            if (info.names)
                check(where, by, *info.names, touchpad, bt_has_capture(fx.controller_type));
            else
                check_kbm(where, by, touchpad);
            const std::string got = text_of(by, order);
            const std::string golden = dir + "/golden/" + where + ".txt";
            if (write) {
                std::ofstream(golden) << got;
                std::printf("wrote %s\n", golden.c_str());
                continue;
            }
            std::ifstream in(golden);
            std::stringstream want;
            want << in.rdbuf();
            if (!in) {
                fail(where + ": no golden file (run test_fixtures --write after checking the output)");
            } else if (want.str() != got) {
                std::vector<std::string> a, b;
                std::string line;
                for (std::istringstream w(want.str()); std::getline(w, line);) a.push_back(line);
                for (std::istringstream g(got); std::getline(g, line);) b.push_back(line);
                for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
                    const std::string la = i < a.size() ? a[i] : "(none)", lb = i < b.size() ? b[i] : "(none)";
                    if (la != lb)
                        fail(where + "\n    golden: " + la + "\n    now:    " + lb);
                }
            }
            std::printf("[ %s ] %s (%zu actions)\n", g_failures ? "..." : "OK", where.c_str(), by.size());
        }
    }
    if (g_failures)
        std::printf("%d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
