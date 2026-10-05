#include "Custom/KbmSettings.h"

#include <cstring>

namespace kbm_settings {

namespace {

// Keyboard usages (USB HID usage tables, keyboard page).
constexpr uint8_t KEY_ENTER = 0x28;
constexpr uint8_t KEY_ESCAPE = 0x29;
constexpr uint8_t KEY_BACKSPACE = 0x2A;
constexpr uint8_t KEY_TAB = 0x2B;
constexpr uint8_t KEY_SPACE = 0x2C;
constexpr uint8_t KEY_RIGHT = 0x4F;
constexpr uint8_t KEY_LEFT = 0x50;
constexpr uint8_t KEY_DOWN = 0x51;
constexpr uint8_t KEY_UP = 0x52;
constexpr uint8_t KEY_MENU = 0x65;  // context menu ("Application")
constexpr uint8_t KEY_LEFT_CTRL = 0xE0;
constexpr uint8_t KEY_LEFT_SHIFT = 0xE1;
constexpr uint8_t KEY_LEFT_GUI = 0xE3;

constexpr uint8_t MOUSE_LEFT = 0;
constexpr uint8_t MOUSE_RIGHT = 1;
constexpr uint8_t MOUSE_BUTTON_COUNT = 5;

constexpr uint8_t MEDIA_HOME = 0;

constexpr uint8_t DEFAULT_SPEED = 8;
constexpr uint8_t MAX_SPEED = 20;
constexpr uint8_t DEFAULT_DEADZONE = 12;
constexpr uint8_t MAX_DEADZONE = 50;

Settings& current()
{
    static Settings settings = defaults();
    return settings;
}

uint8_t clamp_speed(uint8_t v)
{
    return v < 1 ? 1 : (v > MAX_SPEED ? MAX_SPEED : v);
}

Action sanitize(Action a)
{
    switch (action_type(a)) {
        case ACTION_KEY:
            return a;
        case ACTION_MOUSE:
            return a.code < MOUSE_BUTTON_COUNT ? a : Action{0, 0};
        case ACTION_MEDIA:
            return a.code < kConsumerUsageCount ? a : Action{0, 0};
        default:
            return Action{0, 0};
    }
}

} // namespace

Settings defaults()
{
    Settings s{};
    s.version = kVersion;
    s.actions[IN_A] = action(ACTION_KEY, KEY_ENTER);
    s.actions[IN_B] = action(ACTION_KEY, KEY_ESCAPE);
    s.actions[IN_X] = action(ACTION_KEY, KEY_BACKSPACE);
    s.actions[IN_Y] = action(ACTION_KEY, KEY_SPACE);
    s.actions[IN_LB] = action(ACTION_KEY, KEY_TAB, MOD_SHIFT);
    s.actions[IN_RB] = action(ACTION_KEY, KEY_TAB);
    s.actions[IN_LT] = action(ACTION_MOUSE, MOUSE_RIGHT);
    s.actions[IN_RT] = action(ACTION_MOUSE, MOUSE_LEFT);
    s.actions[IN_L3] = action(ACTION_KEY, KEY_LEFT_CTRL);
    s.actions[IN_R3] = action(ACTION_KEY, KEY_LEFT_SHIFT);
    s.actions[IN_START] = action(ACTION_KEY, KEY_MENU);
    s.actions[IN_BACK] = action(ACTION_KEY, KEY_LEFT_GUI);
    s.actions[IN_HOME] = action(ACTION_MEDIA, MEDIA_HOME);
    s.actions[IN_MISC] = Action{0, 0};
    s.actions[IN_DPAD_UP] = action(ACTION_KEY, KEY_UP);
    s.actions[IN_DPAD_DOWN] = action(ACTION_KEY, KEY_DOWN);
    s.actions[IN_DPAD_LEFT] = action(ACTION_KEY, KEY_LEFT);
    s.actions[IN_DPAD_RIGHT] = action(ACTION_KEY, KEY_RIGHT);
    s.left_stick = STICK_SCROLL;
    s.right_stick = STICK_POINTER;
    s.pointer_speed = DEFAULT_SPEED;
    s.scroll_speed = DEFAULT_SPEED;
    s.deadzone = DEFAULT_DEADZONE;
    s.flags = FLAG_POINTER_ACCEL | FLAG_TOUCHPAD;
    return s;
}

bool decode(const uint8_t* data, size_t len, Settings& out)
{
    if (data == nullptr || len < sizeof(Settings) || data[0] != kVersion)
        return false;
    Settings s{};
    std::memcpy(&s, data, sizeof(s));
    for (auto& a : s.actions)
        a = sanitize(a);
    if (s.left_stick >= STICK_MODE_COUNT)
        s.left_stick = STICK_NONE;
    if (s.right_stick >= STICK_MODE_COUNT)
        s.right_stick = STICK_NONE;
    s.pointer_speed = clamp_speed(s.pointer_speed);
    s.scroll_speed = clamp_speed(s.scroll_speed);
    if (s.deadzone > MAX_DEADZONE)
        s.deadzone = MAX_DEADZONE;
    s.flags &= FLAG_POINTER_ACCEL | FLAG_TOUCHPAD | FLAG_INVERT_SCROLL;
    std::memset(s.reserved, 0, sizeof(s.reserved));
    out = s;
    return true;
}

const Settings& get()
{
    return current();
}

void set(const Settings& settings)
{
    current() = settings;
}

} // namespace kbm_settings
