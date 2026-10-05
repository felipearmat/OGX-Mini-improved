#ifndef _OGXM_CUSTOM_KBM_SETTINGS_H_
#define _OGXM_CUSTOM_KBM_SETTINGS_H_

#include <cstddef>
#include <cstdint>

/*  Mouse + keyboard output mode (custom addition, not upstream): what each pad input sends,
 *  set from the web app and kept in flash. The defaults follow Steam's desktop layout where it
 *  has one (A = Enter, B = Esc, Y = Space, D-pad = arrows, RT / LT = left / right click,
 *  right stick = pointer, left stick = scroll).
 *
 *  Settings is also the wire format (web app over USB and Bluetooth) and the flash format:
 *  48 bytes, a version byte first. Each input has a 2-byte action:
 *    byte 0: type (bits 7..4) | left modifiers held with it (bits 3..0: Ctrl, Shift, Alt, GUI)
 *    byte 1: code — keyboard: HID usage (0xE0..0xE7 are the modifier keys themselves);
 *            mouse: button index (0 left, 1 right, 2 middle, 3 back, 4 forward);
 *            media: index into kConsumerUsages.
 */
namespace kbm_settings {

    constexpr uint8_t kVersion = 1;

    enum Input : uint8_t {
        IN_A, IN_B, IN_X, IN_Y,
        IN_LB, IN_RB, IN_LT, IN_RT,
        IN_L3, IN_R3, IN_START, IN_BACK,
        IN_HOME, IN_MISC,
        IN_DPAD_UP, IN_DPAD_DOWN, IN_DPAD_LEFT, IN_DPAD_RIGHT,
        INPUT_COUNT
    };

    enum ActionType : uint8_t {
        ACTION_NONE = 0,
        ACTION_KEY = 1,
        ACTION_MOUSE = 2,
        ACTION_MEDIA = 3,
    };

    enum Modifier : uint8_t {
        MOD_CTRL = 0x01,
        MOD_SHIFT = 0x02,
        MOD_ALT = 0x04,
        MOD_GUI = 0x08,
    };

    enum StickMode : uint8_t {
        STICK_NONE = 0,
        STICK_POINTER = 1,
        STICK_SCROLL = 2,
        STICK_ARROWS = 3,
        STICK_WASD = 4,
        STICK_MODE_COUNT
    };

    enum Flag : uint8_t {
        FLAG_POINTER_ACCEL = 0x01,  // pointer speed grows with the square of the stick push
        FLAG_TOUCHPAD = 0x04,       // DS4 / DualSense touchpad moves the pointer, click = left
        FLAG_INVERT_SCROLL = 0x08,
    };

    // Media keys (USB HID consumer page), by index.
    constexpr uint16_t kConsumerUsages[] = {
        0x0223,  // 0 Home (AC Home)
        0x0224,  // 1 Back (AC Back)
        0x00E9,  // 2 Volume up
        0x00EA,  // 3 Volume down
        0x00E2,  // 4 Mute
        0x00CD,  // 5 Play / pause
        0x00B5,  // 6 Next track
        0x00B6,  // 7 Previous track
        0x00B7,  // 8 Stop
        0x0221,  // 9 Search (AC Search)
    };
    constexpr size_t kConsumerUsageCount = sizeof(kConsumerUsages) / sizeof(kConsumerUsages[0]);

#pragma pack(push, 1)
    struct Action {
        uint8_t type_mods;
        uint8_t code;
    };

    struct Settings {
        uint8_t version;
        Action actions[INPUT_COUNT];
        uint8_t left_stick;     // StickMode
        uint8_t right_stick;    // StickMode
        uint8_t pointer_speed;  // 1..20
        uint8_t scroll_speed;   // 1..20
        uint8_t deadzone;       // percent of full push, 0..50
        uint8_t flags;          // Flag
        uint8_t reserved[5];
    };
#pragma pack(pop)
    static_assert(sizeof(Settings) == 48, "kbm_settings::Settings is a wire format");

    constexpr Action action(ActionType type, uint8_t code, uint8_t mods = 0)
    {
        return Action{static_cast<uint8_t>((type << 4) | (mods & 0x0F)), code};
    }
    constexpr ActionType action_type(const Action& a)
    {
        return static_cast<ActionType>(a.type_mods >> 4);
    }
    constexpr uint8_t action_mods(const Action& a)
    {
        return a.type_mods & 0x0F;
    }

    Settings defaults();

    // Parse stored / received bytes. False (out untouched) for a short buffer or another
    // version; unknown action types / stick modes become "none", speeds are clamped.
    bool decode(const uint8_t* data, size_t len, Settings& out);

    // Current settings (defaults until set() is called at boot with the stored ones).
    const Settings& get();
    void set(const Settings& settings);

} // namespace kbm_settings

#endif // _OGXM_CUSTOM_KBM_SETTINGS_H_
