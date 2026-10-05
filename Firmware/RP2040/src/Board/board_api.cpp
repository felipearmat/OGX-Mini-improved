#include <pico/stdlib.h>
#include <pico/mutex.h>
#include <pico/multicore.h>
#include <pico/platform.h>
#include <hardware/clocks.h>
#include <hardware/timer.h>
#include <hardware/watchdog.h>

#include "tusb.h"

#if defined(CONFIG_EN_USB_HOST)
#include "pio_usb.h"
#endif

#include "Board/Config.h"
#include "UserSettings/NVSTool.h"
#include "Board/board_api.h"
#include "Board/ogxm_log.h"
#include "Board/board_api_private/board_api_private.h"
#include "TaskQueue/TaskQueue.h"

static constexpr uint32_t DISCONNECT_WATCHDOG_MS = 5000;

#if defined(CONFIG_EN_USB_HOST)
#include "USBHost/HostManager.h"
#endif

extern "C" {

// TUSB_OPT_TIME_CALLBACK=1: TinyUSB expects the platform to supply millis; the SDK helper may
// not be linked in this configuration. Used by our tusb_time_delay_ms_api override and TinyUSB.
uint32_t tusb_time_millis_api(void) {
    return time_us_32() / 1000u;
}

// Overrides TinyUSB's weak default (busy-wait on millis only). With PIO USB host and
// skip_alarm_pool there is no hardware 1 kHz SOF interrupt — pio_usb_host_frame() must run
// about once per millisecond. Enumeration calls tusb_time_delay_ms_api() for 50+ ms resets
// from inside tuh_task(); without servicing PIO here, SOFs stop and control transfers fail
// ("Enumeration attempt N" retries, controller never works).
void tusb_time_delay_ms_api(uint32_t ms) {
    const uint32_t start = tusb_time_millis_api();
    while ((tusb_time_millis_api() - start) < ms) {
#if defined(CONFIG_EN_USB_HOST)
        pio_usb_host_frame();
        const uint32_t now = tusb_time_millis_api();
        while (tusb_time_millis_api() == now) {
            tight_loop_contents();
        }
#endif
    }
}

} // extern "C"

namespace board_api {

mutex_t gpio_mutex_;

/* Custom: RGB LED colour for set_led(true), see set_led_color(). Guarded by gpio_mutex_. */
static bool rgb_color_set_ = false;
static uint8_t rgb_color_[3] = {0, 0, 0};

bool usb::host_connected() {
    if (board_api_usbh::host_connected) {
        return board_api_usbh::host_connected();
    }
    return false;
}

bool usb::host_any_pad_mounted() {
#if defined(CONFIG_EN_USB_HOST)
    return HostManager::get_instance().any_mounted();
#else
    return false;
#endif
}

//Only call this from core0
void usb::disconnect_all() {
    OGXM_LOG("Disconnecting USB and resetting Core1\n");

    /* Custom fix: every caller writes flash and reboots right after this. If anything below
     * hangs, the watchdog reboots the board instead of leaving it frozen (USB alive, BT dead). */
    watchdog_enable(DISCONNECT_WATCHDOG_MS, true);

    TaskQueue::suspend_delayed_tasks();

    /* Custom fix: park Core1 in RAM (it only stops with interrupts enabled, so it holds no
     * spinlock) before stop_pio_usb_host() / multicore_reset_core1(). Force-resetting it while
     * it owned a shared lock (e.g. timer / alarm pool) made the next sleep_ms() on Core0 wait
     * forever: the combo was detected but the mode was never saved. */
    if (multicore_lockout_victim_is_initialized(1)) {
        (void)multicore_lockout_start_timeout_us(500 * 1000);
    }
#if defined(CONFIG_EN_USB_HOST)
    /*
     * stop_pio_usb_host may be a weak empty stub (default) or a strong board
     * override (Standard / PicoW). Do NOT use `if (stop_pio_usb_host)` — a weak
     * stub still has a non-null address, so Core1 was never reset on Pico W/2W
     * and NVS flash during mode switch hung under live BT/XIP.
     */
    board_api_usbh::stop_pio_usb_host();
#endif
    /* Always halt Core1 before flash/reboot (BT / host / GPIO simulators). */
    multicore_reset_core1();
    /* Custom: the NVS writes that follow must not wait for a lockout of the halted core. */
    NVSTool::set_other_core_halted();
    sleep_ms(500);
    tud_disconnect();
    sleep_ms(500);
}

// If using PicoW, only use this method from the core running btstack and after you've called init_bluetooth
void set_led(bool state) {
    mutex_enter_blocking(&gpio_mutex_);

    if (board_api_led::set_led) {
        board_api_led::set_led(state);
    }
    if (board_api_bt::set_led) {
        board_api_bt::set_led(state);
    }
    if (board_api_rgb::set_led) {
        if (rgb_color_set_) {
            board_api_rgb::set_led(state ? rgb_color_[0] : 0x00,
                                   state ? rgb_color_[1] : 0x00,
                                   state ? rgb_color_[2] : 0x00);
        } else {
            board_api_rgb::set_led(state ? 0x00 : 0xFF, state ? 0xFF : 0x00, 0x00);
        }
    }

    mutex_exit(&gpio_mutex_);
}

void set_led_color(uint8_t r, uint8_t g, uint8_t b) {
    mutex_enter_blocking(&gpio_mutex_);
    rgb_color_[0] = r;
    rgb_color_[1] = g;
    rgb_color_[2] = b;
    rgb_color_set_ = true;
    mutex_exit(&gpio_mutex_);
}

void reboot() {
    OGXM_LOG("Rebooting\n");
    /* Watchdog reset is reliable from either core (BT runs on Core1). */
    watchdog_reboot(0, 0, 0);
    /* Custom fix: the reset is immediate, so still running here means it did not happen (seen
     * once on a Pico 2 W: the board stayed dark after "Rebooting" until it was replugged). Ask
     * the core for a system reset instead of spinning forever, then keep retrying. */
    while (1) {
        busy_wait_ms(100);
        OGXM_LOG("Reboot fallback: system reset request\n");
        *reinterpret_cast<volatile uint32_t*>(0xE000ED0Cu) = 0x05FA0004u;  // AIRCR: VECTKEY | SYSRESETREQ
        busy_wait_ms(100);
        watchdog_reboot(0, 0, 1);
    }
}

uint32_t ms_since_boot() {
    return to_ms_since_boot(get_absolute_time());
}

//Call after board is initialized
void init_bluetooth() {
    if (board_api_bt::init) {
        board_api_bt::init();
    }
}

//Call on core0 before any other method
void init_board() {
    if (!set_sys_clock_khz(SYSCLOCK_KHZ, true)) {
        if (!set_sys_clock_khz((SYSCLOCK_KHZ / 2), true)) {
            panic("Failed to set sys clock");
        }
    }

    stdio_init_all();

    if (!mutex_is_initialized(&gpio_mutex_)) {
        mutex_init(&gpio_mutex_); 
        mutex_enter_blocking(&gpio_mutex_);

        if (ogxm_log::init) {
            ogxm_log::init();
        }
        if (board_api_led::init) {
            board_api_led::init();
        }
        if (board_api_rgb::init) {
            board_api_rgb::init();
        }
        if (board_api_usbh::init) {
            board_api_usbh::init();
        }

        mutex_exit(&gpio_mutex_);
    }
    OGXM_LOG("Board initialized\n");
}

} // namespace board_api