#include "USBDevice/DeviceDriver/KBM/KbmMapper.h"

#include <cmath>
#include <cstring>

namespace kbm {

using namespace kbm_settings;

namespace {

constexpr uint8_t TRIGGER_PRESSED = 64;     // of 255
constexpr float STICK_KEY_THRESHOLD = 0.5f;  // of full push, arrows / WASD stick modes
constexpr float POINTER_PX_PER_S = 150.0f;   // per speed step, at full push
constexpr float SCROLL_STEPS_PER_S = 1.5f;   // per speed step, at full push
constexpr float TOUCH_SCALE = 0.6f / 8.0f;   // per pointer speed step

constexpr uint8_t KEY_UP = 0x52, KEY_DOWN = 0x51, KEY_LEFT = 0x50, KEY_RIGHT = 0x4F;
constexpr uint8_t KEY_W = 0x1A, KEY_A = 0x04, KEY_S = 0x16, KEY_D = 0x07;
constexpr uint8_t KEY_FIRST_MODIFIER = 0xE0, KEY_LAST_MODIFIER = 0xE7;

struct Collector {
    KeyboardReport kb{};
    uint8_t key_count{0};
    uint16_t media{0};
    uint8_t mouse{0};

    void key(uint8_t usage)
    {
        if (usage >= KEY_FIRST_MODIFIER && usage <= KEY_LAST_MODIFIER) {
            kb.modifiers |= static_cast<uint8_t>(1u << (usage - KEY_FIRST_MODIFIER));
            return;
        }
        if (usage == 0)
            return;
        for (uint8_t i = 0; i < key_count; ++i)
            if (kb.keys[i] == usage)
                return;
        if (key_count < sizeof(kb.keys))
            kb.keys[key_count++] = usage;
    }

    void action(const Action& a)
    {
        switch (action_type(a)) {
            case ACTION_KEY:
                // Left Ctrl / Shift / Alt / GUI sit in bits 0..3 of the HID modifier byte.
                kb.modifiers |= action_mods(a);
                key(a.code);
                break;
            case ACTION_MOUSE:
                mouse |= static_cast<uint8_t>(1u << a.code);
                break;
            case ACTION_MEDIA:
                if (media == 0 && a.code < kConsumerUsageCount)
                    media = kConsumerUsages[a.code];
                break;
            default:
                break;
        }
    }
};

float axis(int16_t v)
{
    return static_cast<float>(v) / 32767.0f;
}

// Radial deadzone; returns the push (0..1) and the unit direction.
float stick_push(int16_t raw_x, int16_t raw_y, uint8_t deadzone_pct, float& ux, float& uy)
{
    const float x = axis(raw_x);
    const float y = axis(raw_y);
    const float mag = std::sqrt(x * x + y * y);
    const float dz = static_cast<float>(deadzone_pct) / 100.0f;
    ux = uy = 0.0f;
    if (mag <= dz || mag <= 0.0f)
        return 0.0f;
    ux = x / mag;
    uy = y / mag;
    const float push = (mag - dz) / (1.0f - dz);
    return push > 1.0f ? 1.0f : push;
}

int8_t take(float& acc)
{
    float whole = std::trunc(acc);
    if (whole > 127.0f)
        whole = 127.0f;
    if (whole < -127.0f)
        whole = -127.0f;
    acc -= whole;
    return static_cast<int8_t>(whole);
}

} // namespace

bool KeyboardReport::operator==(const KeyboardReport& o) const
{
    return modifiers == o.modifiers && std::memcmp(keys, o.keys, sizeof(keys)) == 0;
}

void Mapper::reset()
{
    *this = Mapper();
}

void Mapper::update(const Input& in, const Settings& s, uint32_t dt_us)
{
    Collector c;
    const float dt = static_cast<float>(dt_us) / 1e6f;

    const bool pressed[INPUT_COUNT] = {
        (in.buttons & BTN_A) != 0, (in.buttons & BTN_B) != 0,
        (in.buttons & BTN_X) != 0, (in.buttons & BTN_Y) != 0,
        (in.buttons & BTN_LB) != 0, (in.buttons & BTN_RB) != 0,
        in.trigger_l >= TRIGGER_PRESSED, in.trigger_r >= TRIGGER_PRESSED,
        (in.buttons & BTN_L3) != 0, (in.buttons & BTN_R3) != 0,
        (in.buttons & BTN_START) != 0, (in.buttons & BTN_BACK) != 0,
        (in.buttons & BTN_SYS) != 0, (in.buttons & BTN_MISC) != 0,
        (in.dpad & DPAD_UP) != 0, (in.dpad & DPAD_DOWN) != 0,
        (in.dpad & DPAD_LEFT) != 0, (in.dpad & DPAD_RIGHT) != 0,
    };
    for (uint8_t i = 0; i < INPUT_COUNT; ++i)
        if (pressed[i])
            c.action(s.actions[i]);

    const uint8_t modes[2] = {s.left_stick, s.right_stick};
    const int16_t sx[2] = {in.lx, in.rx};
    const int16_t sy[2] = {in.ly, in.ry};
    for (int st = 0; st < 2; ++st) {
        float ux, uy;
        const float push = stick_push(sx[st], sy[st], s.deadzone, ux, uy);
        if (push <= 0.0f)
            continue;
        switch (modes[st]) {
            case STICK_POINTER: {
                const float curve = (s.flags & FLAG_POINTER_ACCEL) ? push * push : push;
                const float speed = curve * s.pointer_speed * POINTER_PX_PER_S * dt;
                acc_x_ += ux * speed;
                acc_y_ += uy * speed;
                break;
            }
            case STICK_SCROLL: {
                const float steps = push * s.scroll_speed * SCROLL_STEPS_PER_S * dt;
                const float dir = (s.flags & FLAG_INVERT_SCROLL) ? -1.0f : 1.0f;
                acc_wheel_ += -uy * steps * dir;  // push up = scroll up (wheel positive)
                acc_pan_ += ux * steps;
                break;
            }
            case STICK_ARROWS:
            case STICK_WASD: {
                const bool arrows = modes[st] == STICK_ARROWS;
                const float x = ux * push, y = uy * push;
                if (y <= -STICK_KEY_THRESHOLD) c.key(arrows ? KEY_UP : KEY_W);
                if (y >= STICK_KEY_THRESHOLD) c.key(arrows ? KEY_DOWN : KEY_S);
                if (x <= -STICK_KEY_THRESHOLD) c.key(arrows ? KEY_LEFT : KEY_A);
                if (x >= STICK_KEY_THRESHOLD) c.key(arrows ? KEY_RIGHT : KEY_D);
                break;
            }
            default:
                break;
        }
    }

    if (s.flags & FLAG_TOUCHPAD) {
        const uint8_t* p = in.touch_point;
        const bool touching = in.touch_valid && (p[0] & 0x80) == 0;
        if (touching) {
            const int x = p[1] | ((p[2] & 0x0F) << 8);
            const int y = (p[2] >> 4) | (p[3] << 4);
            if (touching_) {
                const float scale = s.pointer_speed * TOUCH_SCALE;
                acc_x_ += static_cast<float>(x - touch_x_) * scale;
                acc_y_ += static_cast<float>(y - touch_y_) * scale;
            }
            touch_x_ = x;
            touch_y_ = y;
        }
        touching_ = touching;
        if (in.touch_valid && in.touch_click)
            c.mouse |= 0x01;
    }

    keyboard_ = c.kb;
    media_ = c.media;
    mouse_buttons_ = c.mouse;
}

bool Mapper::mouse_pending() const
{
    return mouse_buttons_ != sent_buttons_ ||
           std::fabs(acc_x_) >= 1.0f || std::fabs(acc_y_) >= 1.0f ||
           std::fabs(acc_wheel_) >= 1.0f || std::fabs(acc_pan_) >= 1.0f;
}

MouseReport Mapper::take_mouse()
{
    MouseReport r;
    r.buttons = mouse_buttons_;
    r.x = take(acc_x_);
    r.y = take(acc_y_);
    r.wheel = take(acc_wheel_);
    r.pan = take(acc_pan_);
    sent_buttons_ = mouse_buttons_;
    return r;
}

} // namespace kbm
