// Virtual adapter: the firmware's web app protocol (USBDevice/DeviceDriver/WebApp) and settings
// storage (UserSettings, NVSTool on the simulated flash) running on a PC, for the web app's
// end-to-end tests (OGX-Mini-improved-WebApp, e2e/). Its USB serial port is stdin / stdout (raw
// bytes), its flash a file that outlives a restart:
//
//   virtual_adapter FLASH_FILE
//
// A new flash file starts in D-Input/Debug. The harness can open and close the port and hold a
// mode combo on fd 3 (virtual_board.h); in any other mode there is no serial port, as on the board.
//
// Exit codes: 0 when stdin closes (the page disconnected), 3 = the firmware restarted (reboot()),
// 4 = USB bootloader requested. The test harness starts it again on 3, as the board would come
// back, with the same flash file.
#include <cstdio>

#include "virtual_board.h"

#include "Gamepad/Gamepad.h"
#include "USBDevice/DeviceDriver/WebApp/WebApp.h"
#include "UserSettings/UserSettings.h"
#include "Diagnostics/DiagnosticsBoard.h"
#include "Board/ModeIndicator.h"
#include "Board/FlashLog.h"
#include "UserSettings/DongleSettings.h"

namespace {

/* The external RGB status LED (GP6): its colour to the harness whenever it changes (web app
 * preview, saved colours, brightness, the option turned off). */
void report_led(DeviceDriverType mode)
{
    static int32_t last = -1;
    const dongle_settings::Settings& options = dongle_settings::get();
    uint8_t r = 0, g = 0, b = 0;
    if (options.status_led_pico_ext)
        mode_indicator::led_color(mode, options.status_led_brightness, r, g, b);
    const int32_t rgb = (r << 16) | (g << 8) | b;
    if (rgb == last)
        return;
    last = rgb;
    char line[16];
    std::snprintf(line, sizeof(line), "LED %06x", static_cast<unsigned>(rgb));
    virtual_board::control_write(line);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: virtual_adapter FLASH_FILE\n");
        return 2;
    }
    virtual_board::start(argv[1]);

    UserSettings& user_settings = UserSettings::get_instance();
    user_settings.initialize_flash();
    if (virtual_board::fresh_flash())
        user_settings.store_driver_type_only(DeviceDriverType::DINPUT_DEBUG);  // as if the combo had been held
    flash_log::init(virtual_board::ms());
    diag::board_boot();

    const DeviceDriverType mode = user_settings.get_current_driver();
    char line[16];
    std::snprintf(line, sizeof(line), "MODE %u", static_cast<unsigned>(mode));
    virtual_board::control_write(line);

    static Gamepad gamepad;
    gamepad.set_profile(user_settings.get_profile_by_index(0), DeviceDriverType::WEBAPP);

    static WebAppDevice device;
    if (mode == DeviceDriverType::DINPUT_DEBUG)
        device.initialize();

    bool dtr = false;
    while (virtual_board::running())
    {
        virtual_board::pump();
        const int requested = virtual_board::take_mode_request();
        if (requested >= 0)
            user_settings.store_driver_type(static_cast<DeviceDriverType>(requested));  // restarts if valid and new
        if (mode != DeviceDriverType::DINPUT_DEBUG)
            continue;  // another output mode: no serial port for the web app
        if (virtual_board::dtr() != dtr)
        {
            dtr = virtual_board::dtr();
            device.line_state_cb(0, dtr, false);  // the page opened or closed the port
        }
        uint8_t source;
        int32_t accel[3], gyro[3];
        if (virtual_board::take_pad_motion(source, accel, gyro))
        {
            Gamepad::PadIn in{};  // a controller that only moves (the harness's P command)
            in.motion_source = source;
            for (int i = 0; i < 3; ++i)
            {
                in.accel[i] = accel[i];
                in.gyro[i] = gyro[i];
            }
            gamepad.set_pad_in(in);
        }
        device.process(0, gamepad);
        flash_log::tick(virtual_board::ms(), true);
        report_led(mode);
        virtual_board::pump();
    }
    return virtual_board::finish();
}
