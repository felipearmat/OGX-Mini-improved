#ifndef _DEVICE_DRIVER_TYPES_H_
#define _DEVICE_DRIVER_TYPES_H_

#include <cstdint>

enum class DeviceDriverType : uint8_t
{
    NONE = 0,
    XBOXOG,
    XBOXOG_SB,
    XBOXOG_XR,
    XINPUT,
    PS3,
    DINPUT_DEBUG,  // Custom: D-Input/Debug: the DInput gamepad and the web app's serial port (was DINPUT)
    PSCLASSIC,
    SWITCH,
    WIIU,
    WII,   // Wiimote (Pico W/2W: controller on PIO USB, BT for Wii link)
    PS1PS2,   // PS1/PS2 controller over GPIO (no USB device; output to console port)
    GAMECUBE, // GameCube controller over GPIO (single wire; no USB device)
    DREAMCAST, // Dreamcast controller over Maple Bus GPIO (no USB device; output/input to console)
    N64,       // N64 controller over GPIO (single wire; no USB device; output to N64 console)
    PS4,       // DualShock 4 USB HID (CUH-ZCT1x-class descriptor)
    STEAM,     // SteamOS/Bazzite: DualSense USB gamepad + HID mouse (touchpad passthrough)
    KBM,       // Custom: USB keyboard + mouse + media keys driven by the pad (USBDevice/DeviceDriver/KBM/KbmSettings)
    /* Custom: the former Web App mode, now part of D-Input/Debug (DINPUT_DEBUG). Kept as a value:
     * read from flash or sent by an older web app or ESP32, it means DINPUT_DEBUG (normalized()), and the
     * gamepad mapping code takes it for "the web app is open: the default mapping". */
    WEBAPP = 100,
    UART_BRIDGE
};

/* Custom: the mode a stored or received value stands for (the Web App mode is D-Input/Debug). */
constexpr DeviceDriverType normalized(DeviceDriverType driver)
{
    return driver == DeviceDriverType::WEBAPP ? DeviceDriverType::DINPUT_DEBUG : driver;
}

/* Custom: outputs that carry a touchpad press of their own (SDL's TOUCHPAD button, apart from
 * MISC1): the DS4 and DualSense outputs, Mouse + Keyboard (a left click) and the web app (its
 * touchpad panel). On the others a DS4 / DualSense touchpad press stands in for the Misc button,
 * so it still does something; where both exist, Misc is the pad's own extra button (Capture) and
 * the press stays the touchpad's (in STEAM mode it was both: Mute and the touchpad click). */
constexpr bool output_has_touchpad(DeviceDriverType driver)
{
    return driver == DeviceDriverType::PS4 || driver == DeviceDriverType::STEAM ||
           driver == DeviceDriverType::KBM || driver == DeviceDriverType::WEBAPP;
}

#endif // _DEVICE_DRIVER_TYPES_H_