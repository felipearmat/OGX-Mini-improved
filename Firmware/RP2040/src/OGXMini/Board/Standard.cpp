#include "Board/Config.h"
#include "OGXMini/Board/Standard.h"
#if ((OGXM_BOARD == PI_PICO) || (OGXM_BOARD == RP2040_ZERO) || (OGXM_BOARD == ADAFRUIT_FEATHER) || (OGXM_BOARD == RP2350_USB_A) || (OGXM_BOARD == RP2350_ZERO) || (OGXM_BOARD == RP2040_XIAO) || (OGXM_BOARD == OGX_LIVE))

#include <pico/multicore.h>
#include <pico/time.h>
#include <hardware/gpio.h>
#include <atomic>

#include "tusb.h"
#include "bsp/board_api.h"
#include "pio_usb.h"
#include "USBHost/PioHostPort.h"

#include "USBHost/HostManager.h"
#include "USBHost/GPIOHost/GPIOHost.h"
#include "USBDevice/DeviceManager.h"
#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"
#include "USBDevice/DeviceDriver/PS1PS2/psx_simulator.h"
#include "USBDevice/DeviceDriver/GameCube/gc_simulator.h"
#include "USBDevice/DeviceDriver/Dreamcast/Dreamcast.h"
#include "USBDevice/DeviceDriver/N64/N64.h"
#include "TaskQueue/TaskQueue.h"
#include "Gamepad/Gamepad.h"
#include "UserSettings/UserSettings.h"
#include "Board/board_api.h"
#include "Board/board_api_private/board_api_private.h"
#include "Board/ogxm_log.h"
#include "Diagnostics/DiagnosticsBoard.h"
#include "Diagnostics/Diagnostics.h"
#include "Board/FlashLog.h"

constexpr uint32_t FEEDBACK_DELAY_MS = 200;

Gamepad _gamepads[MAX_GAMEPADS];

/** Set during intentional mode-change reboot so host_mounted(false) does not queue a second teardown. */
static std::atomic<bool> s_driver_reboot_pending{false};
/** Debounce transient PIO USB umount before tearing down the USB device stack. */
static std::atomic<bool> s_host_teardown_scheduled{false};
constexpr uint32_t HOST_UNMOUNT_DEBOUNCE_MS = 1500;
constexpr int64_t HOST_LINE_STABLE_US = 40 * 1000;
constexpr int64_t HOST_MOUNT_TIMEOUT_US = 2500 * 1000;

/** Host stack and, when the SOF timer could not start, the frames due (USBHost/PioHostPort.h). */
static void pio_usb_host_loop_tick() {
    pio_host_port::service_frames();
    tuh_task();
}

static bool host_line_high() {
    return (gpio_get(PIO_USB_DP_PIN) != 0) || (gpio_get(PIO_USB_DP_PIN + 1) != 0);
}

/** D+ or D- high for HOST_LINE_STABLE_US (a device attached and powered). idle: run while waiting
 *  (Core0's task queue in the GPIO console modes), or nullptr. */
static void wait_for_stable_host_line(void (*idle)()) {
    absolute_time_t high_since = nil_time;
    while (true) {
        if (idle) {
            idle();
        }
        if (!host_line_high()) {
            high_since = nil_time;
            sleep_ms(10);
            continue;
        }
        if (is_nil_time(high_since)) {
            high_since = get_absolute_time();
        }
        if (absolute_time_diff_us(high_since, get_absolute_time()) >= HOST_LINE_STABLE_US) {
            return;
        }
        sleep_ms(5);
    }
}

static bool tuh_any_device_mounted() {
    const uint8_t addr_max = static_cast<uint8_t>(CFG_TUH_DEVICE_MAX + CFG_TUH_HUB);
    for (uint8_t d = 1; d <= addr_max; ++d) {
        if (tuh_mounted(d)) {
            return true;
        }
    }
    return false;
}

/* From upstream #160: TinyUSB gives up an enumeration after three tries and a device that stays
 * attached (an Xbox 360 wireless receiver powering up with the adapter) makes no new attach event,
 * so it stayed unenumerated until replugged. Start the host once the line is stable, and restart it
 * when nothing mounts within HOST_MOUNT_TIMEOUT_US. */
static void start_pio_usb_host(void (*idle)() = nullptr) {
    wait_for_stable_host_line(idle);
    (void)pio_host_port::start();
}

static void restart_pio_usb_host(void (*idle)() = nullptr) {
    OGXM_LOG("No USB host device mounted in time, restarting PIO USB host.\n");
    pio_host_port::stop();
    for (int i = 0; i < 20; ++i) {
        tuh_task();
        sleep_ms(1);
    }
    board_api_usbh::enable_host_line_irq_monitoring();
    sleep_ms(50);
    start_pio_usb_host(idle);
}

static void core0_idle() {
    TaskQueue::Core0::process_tasks();
}

namespace board_api_usbh {

void stop_pio_usb_host() {
    pio_host_port::stop_sof_timer();
    multicore_reset_core1();
    sleep_ms(50);
    (void)tuh_deinit(BOARD_TUH_RHPORT);
    for (int i = 0; i < 20; ++i) {
        tuh_task();
        sleep_ms(1);
    }
    enable_host_line_irq_monitoring();
}

} // namespace board_api_usbh

void core1_task() {
    /* Custom fix: let Core0 park this core while it writes flash (UserSettings/NVSTool.h). Without
     * it, a settings save applied live (no restart) wrote flash while this core ran from it, which
     * could hang it; the main loop then stopped and the hang watchdog restarted the adapter. */
    multicore_lockout_victim_init();
    HostManager& host_manager = HostManager::get_instance();
    host_manager.initialize(_gamepads);

    start_pio_usb_host();
    absolute_time_t host_started = get_absolute_time();
    bool host_ever_mounted = false;

    uint32_t tid_feedback = TaskQueue::Core1::get_new_task_id();
    TaskQueue::Core1::queue_delayed_task(tid_feedback, FEEDBACK_DELAY_MS, true,
    [&host_manager] {
        host_manager.send_feedback();
    });

    while (true) {
        pio_host_port::service_frames();
        TaskQueue::Core1::process_tasks();
        tuh_task();
        if (host_ever_mounted) {
            continue;
        }
        if (tuh_any_device_mounted()) {
            host_ever_mounted = true;
        } else if (absolute_time_diff_us(host_started, get_absolute_time()) >= HOST_MOUNT_TIMEOUT_US) {
            restart_pio_usb_host();
            host_started = get_absolute_time();
        }
    }
}

void set_gp_check_timer(uint32_t task_id) {
#if !defined(CONFIG_OGXM_FIXED_DRIVER) || defined(CONFIG_OGXM_FIXED_DRIVER_ALLOW_COMBOS)
    UserSettings& user_settings = UserSettings::get_instance();

    TaskQueue::Core0::queue_delayed_task(task_id, UserSettings::GP_CHECK_DELAY_MS, true,
    [&user_settings] {
        // Check for input source change (Start+Select when in PS1PS2/GAMECUBE mode)
        if (user_settings.check_for_input_source_change(_gamepads[0])) {
            // No reboot; input source stored
        }
        // Check gamepad inputs for button combo to change usb device driver
        else if (user_settings.check_for_driver_change(_gamepads[0])) {
            OGXM_LOG("Driver change detected, storing new driver.\n");
            s_driver_reboot_pending = true;
            user_settings.store_driver_type(user_settings.get_current_driver());
        }
    });
#else
    (void)task_id;  // Fixed output, combos disabled
#endif
}

//Called by tusb host so we know to connect or disconnect usb
void standard::host_mounted(bool host_mounted) {
    static std::atomic<bool> tud_is_inited = false;
    board_api::set_led(host_mounted);

    DeviceDriverType dt = DeviceManager::get_instance().get_driver_type();
    if (dt == DeviceDriverType::PS1PS2 || dt == DeviceDriverType::GAMECUBE || dt == DeviceDriverType::DREAMCAST) {
        return;
    }

    if (!host_mounted && tud_is_inited.load() && !s_driver_reboot_pending.load()) {
        if (!s_host_teardown_scheduled.exchange(true)) {
            TaskQueue::Core0::queue_delayed_task(
                TaskQueue::Core0::get_new_task_id(),
                HOST_UNMOUNT_DEBOUNCE_MS,
                false,
                []() {
                    s_host_teardown_scheduled.store(false);
                    if (s_driver_reboot_pending.load()) {
                        return;
                    }
                    if (HostManager::get_instance().any_mounted()) {
                        return;
                    }
                    OGXM_LOG("USB disconnected, rebooting.\n");
                    board_api::usb::disconnect_all();
                    board_api::reboot();
                });
        }
    } else if (host_mounted) {
        s_host_teardown_scheduled.store(false);
        if (!tud_is_inited.load()) {
            TaskQueue::Core0::queue_task([]() {
                tud_init(BOARD_TUD_RHPORT);
                tud_is_inited.store(true);
            });
        }
    }
}

void standard::initialize() {
    diag::set_latency_gamepad(&_gamepads[0]);  // Custom: input-to-use latency in the diagnostics
    board_api::init_board();

    UserSettings& user_settings = UserSettings::get_instance();
    user_settings.initialize_flash();
    flash_log::init(board_api::ms_since_boot());  // Custom: the kept log in flash (Board/FlashLog.h)

    for (uint8_t i = 0; i < MAX_GAMEPADS; ++i) {
        _gamepads[i].set_profile(user_settings.get_profile_by_index(i), user_settings.get_current_driver());
    }

    DeviceManager::get_instance().initialize_driver(user_settings.get_current_driver(), _gamepads);
}

void standard::run() {
    DeviceDriverType current_driver = UserSettings::get_instance().get_current_driver();
    const bool gpio_device_mode = (current_driver == DeviceDriverType::PS1PS2 || current_driver == DeviceDriverType::GAMECUBE || current_driver == DeviceDriverType::DREAMCAST);
    absolute_time_t gpio_host_started = nil_time;
    bool gpio_host_ever_mounted = false;

    if (gpio_device_mode) {
        HostManager& host_manager = HostManager::get_instance();
        host_manager.initialize(_gamepads);

        start_pio_usb_host(core0_idle);
        gpio_host_started = get_absolute_time();

        uint32_t tid_feedback = TaskQueue::Core0::get_new_task_id();
        TaskQueue::Core0::queue_delayed_task(tid_feedback, FEEDBACK_DELAY_MS, true, [&host_manager]() {
            host_manager.send_feedback();
        });

        HostInputSource input_src = UserSettings::get_instance().get_input_source();
        if (current_driver == DeviceDriverType::DREAMCAST) {
            dreamcast_set_core1_device_mode(input_src != HostInputSource::DREAMCAST_GPIO);
        }
        multicore_reset_core1();
        if (current_driver == DeviceDriverType::PS1PS2) {
            multicore_launch_core1(psx_device_main);
        } else if (current_driver == DeviceDriverType::GAMECUBE) {
            multicore_launch_core1(gamecube_core1_entry);
        } else if (current_driver == DeviceDriverType::N64) {
            multicore_launch_core1(n64_core1_entry);
        } else {
            multicore_launch_core1(dreamcast_core1_entry);
        }
        if (input_src == HostInputSource::PSX_GPIO) {
            GPIOHost::psx_host_init(0, 20, 19);
        } else if (input_src == HostInputSource::GAMECUBE_GPIO) {
            GPIOHost::joybus_host_init(0, 19);
        }
        if (input_src == HostInputSource::DREAMCAST_GPIO) {
            GPIOHost::dreamcast_host_init(1, 0, DREAMCAST_PIN_A, -1);
        }
        if (input_src == HostInputSource::N64_GPIO) {
            GPIOHost::n64_host_init(0, 19);
        }
        /* Present as USB device to console immediately so PS2/GC/Dreamcast see the adapter at boot
         * (host_mounted() returns early for these drivers and never calls tud_init). */
        tud_init(BOARD_TUD_RHPORT);
    } else {
        multicore_reset_core1();
        multicore_launch_core1(core1_task);

        /* Custom: D-Input/Debug starts USB right away, so the web app connects without a
         * controller (as the Web App mode did). */
        if (current_driver != DeviceDriverType::DINPUT_DEBUG) {
            while (!tud_inited()) {
                TaskQueue::Core0::process_tasks();
                sleep_ms(100);
            }
        } else {
            host_mounted(true);
        }

        /* GPIO input when output is USB (e.g. real PS1/PS2/GC controller → Switch/Xbox/DInput) */
        HostInputSource input_src = UserSettings::get_instance().get_input_source();
        if (input_src == HostInputSource::PSX_GPIO) {
            GPIOHost::psx_host_init(0, 20, 19);
        } else if (input_src == HostInputSource::GAMECUBE_GPIO) {
            GPIOHost::joybus_host_init(0, 19);
        } else if (input_src == HostInputSource::N64_GPIO) {
            GPIOHost::n64_host_init(0, 19);
        }
    }

    uint32_t tid_gp_check = TaskQueue::Core0::get_new_task_id();
    set_gp_check_timer(tid_gp_check);

    DeviceDriver* device_driver = DeviceManager::get_instance().get_driver();

    while (true) {
        TaskQueue::Core0::process_tasks();
        flash_log::tick(board_api::ms_since_boot(), !gpio_device_mode);  // not in the GPIO console modes
        diag::tick(board_api::ms_since_boot());  // Custom fix: per-second rates roll over every second, not only on web app requests
        if (gpio_device_mode) {
            pio_usb_host_loop_tick();
            if (!gpio_host_ever_mounted) {
                if (tuh_any_device_mounted()) {
                    gpio_host_ever_mounted = true;
                } else if (absolute_time_diff_us(gpio_host_started, get_absolute_time()) >= HOST_MOUNT_TIMEOUT_US) {
                    restart_pio_usb_host(core0_idle);
                    gpio_host_started = get_absolute_time();
                }
            }
            tud_task();
            HostInputSource input_src = UserSettings::get_instance().get_input_source();
            if (input_src == HostInputSource::PSX_GPIO) {
                GPIOHost::psx_host_poll(_gamepads[0]);
            } else if (input_src == HostInputSource::GAMECUBE_GPIO) {
                GPIOHost::joybus_host_poll(_gamepads[0]);
            } else if (input_src == HostInputSource::DREAMCAST_GPIO) {
                GPIOHost::dreamcast_host_poll(_gamepads[0]);
            } else if (input_src == HostInputSource::N64_GPIO) {
                GPIOHost::n64_host_poll(_gamepads[0]);
            }
        } else {
            tud_task();
            HostInputSource input_src = UserSettings::get_instance().get_input_source();
            if (input_src == HostInputSource::PSX_GPIO) {
                GPIOHost::psx_host_poll(_gamepads[0]);
            } else if (input_src == HostInputSource::GAMECUBE_GPIO) {
                GPIOHost::joybus_host_poll(_gamepads[0]);
            } else if (input_src == HostInputSource::DREAMCAST_GPIO) {
                GPIOHost::dreamcast_host_poll(_gamepads[0]);
            } else if (input_src == HostInputSource::N64_GPIO) {
                GPIOHost::n64_host_poll(_gamepads[0]);
            }
        }
        for (uint8_t i = 0; i < MAX_GAMEPADS; ++i) {
            device_driver->process(i, _gamepads[i]);
        }
#if MAIN_LOOP_DELAY_US > 0
        sleep_us(MAIN_LOOP_DELAY_US);
#endif
    }
}

// #else // OGXM_BOARD == PI_PICO || ... || RP2354

// void standard::host_mounted(bool host_mounted) {}
// void standard::initialize() {}
// void standard::run() {}

#endif // PI_PICO || RP2040_ZERO || ADAFRUIT_FEATHER || RP2350_USB_A || RP2350_ZERO || RP2040_XIAO