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
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "tusb.h"
#include "fixture_replay.h"
#include "Descriptors/DInput.h"
#include "Descriptors/PS4.h"
#include "Descriptors/PS5.h"
#include "USBDevice/DeviceDriver/DInput/DInput.h"
#include "USBDevice/DeviceDriver/PS4/PS4.h"
#include "USBDevice/DeviceDriver/Sony/SonyReports.h"
#include "USBDevice/DeviceDriver/Steam/Steam.h"
#include "USBDevice/DeviceDriver/Steam/SteamBtReport.h"
#include "USBDevice/DeviceDriver/Steam/SteamPassthrough.h"
#include "UserSettings/UserProfile.h"

/* ---- TinyUSB stubs (as test_device_reports) ----------------------------------------------- */

uint64_t mock_time_us = 0;
namespace {
std::vector<uint8_t> g_last_report;
}
extern "C" {
bool tud_hid_n_ready(uint8_t) { return true; }
bool tud_hid_n_report(uint8_t, uint8_t report_id, void const* report, uint16_t len) {
    g_last_report.clear();
    if (report_id != 0)
        g_last_report.push_back(report_id);
    const uint8_t* p = static_cast<const uint8_t*>(report);
    g_last_report.insert(g_last_report.end(), p, p + len);
    return true;
}
bool tud_hid_n_keyboard_report(uint8_t, uint8_t, uint8_t, const uint8_t[6]) { return true; }
bool tud_hid_n_mouse_report(uint8_t, uint8_t, uint8_t, int8_t, int8_t, int8_t, int8_t) { return true; }
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
    uint32_t last_t = 0;
    int reports = 0;
};

void axis(int v, int (&mm)[2]) {
    mm[0] = std::min(mm[0], v);
    mm[1] = std::max(mm[1], v);
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
        o << " LX=" << s.lx[0] << ".." << s.lx[1] << " LY=" << s.ly[0] << ".." << s.ly[1]
          << " RX=" << s.rx[0] << ".." << s.rx[1] << " RY=" << s.ry[0] << ".." << s.ry[1]
          << " L2=" << s.l2 << " R2=" << s.r2;
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

struct Expect {
    const char* label;
    const char* button;  // the output's name for it ("" = none)
};
// Xbox-layout labels (the script's); the outputs' PlayStation names.
const Expect kButtons[] = {
    {"a", "cross"}, {"b", "circle"}, {"x", "square"}, {"y", "triangle"}, {"lb", "L1"}, {"rb", "R1"},
    {"home", "PS"}, {"l3", "L3"}, {"r3", "R3"},
};

void check(const std::string& where, const std::map<std::string, Summary>& by, bool dinput) {
    auto has = [&](const char* label) { return by.count(label) != 0; };
    for (const Expect& e : kButtons) {
        if (!has(e.label)) continue;
        const Summary& s = by.at(e.label);
        if (!s.buttons.count(e.button))
            fail(where + " " + e.label + ": " + e.button + " not pressed");
        for (const auto& b : s.buttons)
            if (b != e.button) fail(where + " " + e.label + ": also " + b);
    }
    const char* back = dinput ? "select" : "share";
    const char* start = dinput ? "start" : "options";
    if (has("back") && !by.at("back").buttons.count(back)) fail(where + " back: " + back + " not pressed");
    if (has("start") && !by.at("start").buttons.count(start)) fail(where + " start: " + start + " not pressed");
    const std::pair<const char*, int> dpads[] = {{"dpad_up", 0}, {"dpad_right", 2}, {"dpad_down", 4}, {"dpad_left", 6}};
    for (const auto& [label, dir] : dpads)
        if (has(label) && !by.at(label).dpad.count(dir)) fail(where + " " + label + ": D-pad " + std::to_string(dir));
    if (has("lt_full") && by.at("lt_full").l2 != 255) fail(where + " lt_full: L2 " + std::to_string(by.at("lt_full").l2));
    if (has("rt_full") && by.at("rt_full").r2 != 255) fail(where + " rt_full: R2 " + std::to_string(by.at("rt_full").r2));
    if (has("ls_right") && by.at("ls_right").lx[1] != 255) fail(where + " ls_right: LX max " + std::to_string(by.at("ls_right").lx[1]));
    /* Up / left may stop at 1: Bluepad32 centres a DS4 stick on 127 ((v - 127) * 4, so -508..512)
     * and a full push up comes out one step short of 0. */
    if (has("ls_up") && by.at("ls_up").ly[0] > 1) fail(where + " ls_up: LY min " + std::to_string(by.at("ls_up").ly[0]));
    if (has("rs_right") && by.at("rs_right").rx[1] != 255) fail(where + " rs_right: RX max " + std::to_string(by.at("rs_right").rx[1]));
    if (has("rs_up") && by.at("rs_up").ry[0] > 1) fail(where + " rs_up: RY min " + std::to_string(by.at("rs_up").ry[0]));
    if (!dinput && has("touch_press") && !by.at("touch_press").buttons.count("touchpad"))
        fail(where + " touch_press: touchpad click not pressed");
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

/* ---- replay ------------------------------------------------------------------------------- */

enum class Mode { PS4, STEAM, DINPUT };
const char* mode_name(Mode m) { return m == Mode::PS4 ? "ps4" : m == Mode::STEAM ? "steam" : "dinput"; }

std::map<std::string, Summary> replay(const Fixture& fx, Mode mode, bool& known) {
    Gamepad gamepad;
    const DeviceDriverType driver = mode == Mode::PS4 ? DeviceDriverType::PS4
                                    : mode == Mode::STEAM ? DeviceDriverType::STEAM
                                                          : DeviceDriverType::DINPUT_DEBUG;
    gamepad.set_profile(UserProfile(), driver);
    std::vector<ReplayFrame> frames;
    known = bt_replay(fx.rows, fx.controller_type, fx.vid, fx.pid, gamepad, frames);

    PS4Device ps4;
    SteamDevice steam;
    DInputDevice dinput;
    DeviceDriver& dev = mode == Mode::PS4 ? static_cast<DeviceDriver&>(ps4)
                        : mode == Mode::STEAM ? static_cast<DeviceDriver&>(steam)
                                              : static_cast<DeviceDriver&>(dinput);
    dev.initialize();
    SteamPassthrough::clear();
    std::map<std::string, Summary> by_label;
    for (const ReplayFrame& f : frames) {
        mock_time_us = static_cast<uint64_t>(f.t) * 1000;
        if (mode == Mode::STEAM)
            SteamBtReport::store_ps5_core(f.core);
        gamepad.set_pad_in(f.in);
        g_last_report.clear();
        dev.process(0, gamepad);
        if (f.label->empty() || g_last_report.empty())
            continue;
        Summary& s = by_label[*f.label];
        if (mode == Mode::DINPUT)
            decode_dinput(g_last_report, s);
        else
            decode_sony(g_last_report, mode == Mode::STEAM, s, f.t);
        s.last_t = f.t;
        ++s.reports;
    }
    SteamPassthrough::clear();
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
    const char* pads[] = {"ds4"};  // Bluepad32 DS4 parser; more pads as their parsers are wired in
    for (const char* pad : pads) {
        Fixture fx;
        if (!load(dir + "/" + pad + ".jsonl.gz", fx)) {
            fail(std::string(pad) + ": fixture not readable");
            continue;
        }
        const std::vector<std::string> order = label_order(fx);
        for (Mode mode : {Mode::PS4, Mode::STEAM, Mode::DINPUT}) {
            const std::string where = std::string(pad) + "." + mode_name(mode);
            bool known = false;
            const auto by = replay(fx, mode, known);
            if (!known) {
                fail(where + ": no parser for controller type " + std::to_string(fx.controller_type));
                continue;
            }
            check(where, by, mode == Mode::DINPUT);
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
