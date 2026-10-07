// Mouse + keyboard output mode: settings wire format (USBDevice/DeviceDriver/KBM/KbmSettings) and the pad-to-report
// mapping (USBDevice/DeviceDriver/KBM/KbmMapper).
#include <cstring>

#include "USBDevice/DeviceDriver/KBM/KbmMapper.h"
#include "USBDevice/DeviceDriver/KBM/KbmSettings.h"
#include "test.h"

using namespace kbm_settings;

namespace {

constexpr uint32_t MS = 1000;

bool has_key(const kbm::KeyboardReport& kb, uint8_t usage)
{
    for (uint8_t k : kb.keys)
        if (k == usage)
            return true;
    return false;
}

int key_count(const kbm::KeyboardReport& kb)
{
    int n = 0;
    for (uint8_t k : kb.keys)
        n += k != 0;
    return n;
}

}  // namespace

TEST(defaults_follow_steam_desktop_layout) {
    const Settings s = defaults();
    CHECK_EQ(s.version, kVersion);
    CHECK_EQ(action_type(s.actions[IN_A]), ACTION_KEY);
    CHECK_EQ(s.actions[IN_A].code, 0x28);  // Enter
    CHECK_EQ(s.actions[IN_B].code, 0x29);  // Esc
    CHECK_EQ(s.actions[IN_Y].code, 0x2C);  // Space
    CHECK_EQ(action_type(s.actions[IN_RT]), ACTION_MOUSE);
    CHECK_EQ(s.actions[IN_RT].code, 0);  // left click
    CHECK_EQ(s.actions[IN_LT].code, 1);  // right click
    CHECK_EQ(s.actions[IN_LB].code, 0x2B);  // Tab ...
    CHECK_EQ(action_mods(s.actions[IN_LB]), MOD_SHIFT);  // ... with Shift
    CHECK_EQ(s.right_stick, STICK_POINTER);
    CHECK_EQ(s.left_stick, STICK_SCROLL);
}

TEST(decode_sanitises) {
    Settings raw = defaults();
    raw.actions[IN_X] = action(static_cast<ActionType>(9), 0x10);  // unknown type
    raw.actions[IN_Y] = action(ACTION_MOUSE, 7);                    // no such button
    raw.actions[IN_LB] = action(ACTION_MEDIA, 200);                 // no such media key
    raw.left_stick = 42;
    raw.pointer_speed = 0;
    raw.scroll_speed = 99;
    raw.deadzone = 90;
    raw.flags = 0xFF;
    raw.reserved[0] = 5;
    uint8_t bytes[sizeof(Settings)];
    std::memcpy(bytes, &raw, sizeof(bytes));
    Settings s{};
    CHECK(decode(bytes, sizeof(bytes), s));
    CHECK_EQ(action_type(s.actions[IN_X]), ACTION_NONE);
    CHECK_EQ(action_type(s.actions[IN_Y]), ACTION_NONE);
    CHECK_EQ(action_type(s.actions[IN_LB]), ACTION_NONE);
    CHECK_EQ(s.actions[IN_A].code, 0x28);  // valid ones kept
    CHECK_EQ(s.left_stick, STICK_NONE);
    CHECK_EQ(s.pointer_speed, 1);
    CHECK_EQ(s.scroll_speed, 20);
    CHECK_EQ(s.deadzone, 50);
    CHECK_EQ(s.flags, FLAG_POINTER_ACCEL | FLAG_TOUCHPAD | FLAG_INVERT_SCROLL);  // unknown bits dropped
    CHECK_EQ(s.reserved[0], 0);
}

TEST(decode_rejects_other_version_and_short_buffer) {
    Settings s = defaults();
    s.pointer_speed = 3;
    uint8_t bytes[sizeof(Settings)];
    std::memcpy(bytes, &s, sizeof(bytes));
    bytes[0] = kVersion + 1;
    Settings out = defaults();
    CHECK(!decode(bytes, sizeof(bytes), out));
    bytes[0] = kVersion;
    CHECK(!decode(bytes, sizeof(bytes) - 1, out));
    CHECK(!decode(nullptr, sizeof(bytes), out));
    CHECK_EQ(out.pointer_speed, defaults().pointer_speed);  // untouched on failure
}

TEST(buttons_become_keys_modifiers_and_clicks) {
    kbm::Mapper m;
    kbm::Input in;
    in.buttons = kbm::BTN_A | kbm::BTN_LB | kbm::BTN_L3;
    in.dpad = kbm::DPAD_UP;
    in.trigger_r = 200;
    in.trigger_l = 30;  // below the press threshold
    m.update(in, defaults(), MS);
    const auto& kb = m.keyboard();
    CHECK(has_key(kb, 0x28));  // A = Enter
    CHECK(has_key(kb, 0x2B));  // LB = Tab (+ Shift below)
    CHECK(has_key(kb, 0x52));  // D-pad up = Up arrow
    CHECK_EQ(kb.modifiers, 0x01 | 0x02);  // L3 = Left Ctrl, LB's Shift
    CHECK_EQ(key_count(kb), 3);  // Ctrl is a modifier, not a key slot
    CHECK(m.mouse_pending());
    const kbm::MouseReport r = m.take_mouse();
    CHECK_EQ(r.buttons, 0x01);  // RT = left click; LT not pressed
    CHECK(!m.mouse_pending());
}

TEST(home_sends_media_key_and_release_clears) {
    kbm::Mapper m;
    kbm::Input in;
    in.buttons = kbm::BTN_SYS;
    m.update(in, defaults(), MS);
    CHECK_EQ(m.media(), 0x0223);  // AC Home
    in.buttons = 0;
    m.update(in, defaults(), MS);
    CHECK_EQ(m.media(), 0);
    CHECK_EQ(key_count(m.keyboard()), 0);
    CHECK_EQ(m.keyboard().modifiers, 0);
}

TEST(at_most_six_keys) {
    Settings s = defaults();
    for (uint8_t i = 0; i < INPUT_COUNT; ++i)
        s.actions[i] = action(ACTION_KEY, static_cast<uint8_t>(0x04 + i));  // a, b, c, ...
    kbm::Mapper m;
    kbm::Input in;
    in.buttons = 0x0FFF;
    in.dpad = 0x0F;
    m.update(in, s, MS);
    CHECK_EQ(key_count(m.keyboard()), 6);
    CHECK(has_key(m.keyboard(), 0x04));  // first inputs win
}

TEST(right_stick_moves_pointer_at_set_speed) {
    Settings s = defaults();
    s.flags = 0;  // linear
    s.deadzone = 0;
    s.pointer_speed = 8;  // 1200 px/s at full push
    kbm::Mapper m;
    kbm::Input in;
    in.rx = 32767;
    int total = 0;
    for (int i = 0; i < 100; ++i) {  // 100 ms
        m.update(in, s, MS);
        total += m.take_mouse().x;
    }
    CHECK(total >= 118 && total <= 120);
    // Pushed up (negative Y) moves the pointer up (negative).
    in.rx = 0;
    in.ry = -32767;
    m.update(in, s, 10 * MS);
    CHECK(m.take_mouse().y < 0);
}

TEST(pointer_accel_curve_and_deadzone) {
    Settings s = defaults();
    s.deadzone = 20;
    kbm::Mapper m;
    kbm::Input in;
    in.rx = 32767 / 10;  // inside the deadzone
    m.update(in, s, 100 * MS);
    CHECK(!m.mouse_pending());
    // Half way past the deadzone: linear gives half speed, the accel curve a quarter.
    in.rx = static_cast<int16_t>(32767 * 0.6f);
    s.flags = 0;
    m.update(in, s, 100 * MS);
    const int linear = m.take_mouse().x;
    m.reset();
    s.flags = FLAG_POINTER_ACCEL;
    m.update(in, s, 100 * MS);
    const int curved = m.take_mouse().x;
    CHECK(linear >= 58 && linear <= 61);
    CHECK(curved >= 29 && curved <= 31);
}

TEST(left_stick_scrolls_and_keeps_remainder) {
    Settings s = defaults();
    s.deadzone = 0;
    s.scroll_speed = 8;  // 12 steps/s at full push
    kbm::Mapper m;
    kbm::Input in;
    in.ly = -32767;  // push up = scroll up
    m.update(in, s, 50 * MS);  // 0.6 step: nothing yet
    CHECK(!m.mouse_pending());
    m.update(in, s, 50 * MS);  // 1.2 steps
    CHECK(m.mouse_pending());
    CHECK_EQ(m.take_mouse().wheel, 1);
    s.flags |= FLAG_INVERT_SCROLL;
    m.reset();
    m.update(in, s, 100 * MS);
    CHECK_EQ(m.take_mouse().wheel, -1);
}

TEST(stick_as_arrows_and_wasd) {
    Settings s = defaults();
    s.left_stick = STICK_ARROWS;
    s.right_stick = STICK_WASD;
    kbm::Mapper m;
    kbm::Input in;
    in.lx = 32767;    // right
    in.ry = -32767;   // up
    m.update(in, s, MS);
    CHECK(has_key(m.keyboard(), 0x4F));  // Right arrow
    CHECK(has_key(m.keyboard(), 0x1A));  // W
    in.lx = 32767 / 4;  // below the key threshold
    in.ry = 0;
    m.update(in, s, MS);
    CHECK_EQ(key_count(m.keyboard()), 0);
}

TEST(touchpad_moves_pointer_and_clicks) {
    Settings s = defaults();
    s.pointer_speed = 8;  // 0.6 px per touchpad unit
    kbm::Mapper m;
    kbm::Input in;
    in.touch_valid = true;
    auto touch = [&](int x, int y, bool down) {
        in.touch_point[0] = down ? 0x01 : 0x81;
        in.touch_point[1] = static_cast<uint8_t>(x & 0xFF);
        in.touch_point[2] = static_cast<uint8_t>(((x >> 8) & 0x0F) | ((y & 0x0F) << 4));
        in.touch_point[3] = static_cast<uint8_t>(y >> 4);
    };
    touch(500, 300, true);
    m.update(in, s, MS);  // first contact: no jump
    CHECK(!m.mouse_pending());
    touch(600, 250, true);
    m.update(in, s, MS);
    kbm::MouseReport r = m.take_mouse();
    CHECK_EQ(r.x, 60);
    CHECK_EQ(r.y, -30);
    touch(600, 250, false);  // lifted
    m.update(in, s, MS);
    touch(900, 900, true);  // new contact far away: no jump
    m.update(in, s, MS);
    CHECK(!m.mouse_pending());
    in.touch_click = true;
    m.update(in, s, MS);
    CHECK_EQ(m.take_mouse().buttons, 0x01);
}

TEST(large_movement_split_into_int8_reports) {
    Settings s = defaults();
    s.flags = 0;
    s.deadzone = 0;
    s.pointer_speed = 20;  // 3000 px/s
    kbm::Mapper m;
    kbm::Input in;
    in.rx = 32767;
    m.update(in, s, 100 * MS);  // 300 px at once (host was not polling)
    CHECK_EQ(m.take_mouse().x, 127);
    CHECK_EQ(m.take_mouse().x, 127);
    CHECK_EQ(m.take_mouse().x, 46);
    CHECK(!m.mouse_pending());
}

TEST_MAIN()
