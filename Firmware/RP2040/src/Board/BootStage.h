#ifndef _OGXM_BOOT_STAGE_H_
#define _OGXM_BOOT_STAGE_H_

#include <cstdint>
#include <hardware/watchdog.h>

/* Custom: how far the boot got, kept in watchdog scratch registers 2-3 (Board/BootMode.h uses
 * 0-1, the SDK's reboot 4-7). They survive a watchdog reset but not a power cycle, so after a
 * hang during boot (the hybrid arms the watchdog from the start, see begin()) the next boot's
 * report names the stage it stopped in (Diagnostics/DiagnosticsBoard.cpp). */
namespace boot_stage {

    enum class Stage : uint32_t
    {
        NONE = 0,
        STDIO,          // stdio_init_all
        CORE_INIT,      // flash_safe_execute_core_init
        BOARD_INIT,     // board_api::init_board (clocks, LEDs)
        ESP32_PINS,     // esp32_api::init
        FLASH,          // UserSettings::initialize_flash
        DRIVER,         // output mode chosen, USB device driver set up
        CORE1,          // Core1 launched
        USB_HOST,       // wired USB host port
        ESP32_UART,     // the ESP32's serial output
        ESP32_RESET,    // esp32_api::reset
        USB_DEVICE,     // tud_init
        RUNNING,        // main loop
        UART_BRIDGE,    // ESP32 bridge / log monitor
        /* Main loop steps (the hybrid sets them on every pass): after a hang recovery the report
         * names the step the main loop was stuck in. */
        LOOP_TASKS,     // TaskQueue::Core0::process_tasks
        LOOP_FLASH_LOG, // flash_log::tick
        LOOP_USB,       // device driver process + tud_task
        LOOP_ESP_RX,    // the ESP32's serial output into the kept log
        LOOP_USB_HOST,  // wired USB host port
        LOOP_LED,       // status LED
    };

    constexpr uint32_t kMagic = 0xB0075A6Eu;
    constexpr uint32_t kWatchdogMs = 8000;  // the boot takes about 1.5 s

    struct Previous
    {
        Stage stage = Stage::NONE;
        bool by_watchdog = false;   // watchdog_caused_reboot()
        bool hang = false;          // watchdog_enable_caused_reboot(): it timed out
    };

    inline Previous& previous_storage()
    {
        static Previous previous;
        return previous;
    }

    inline void set(Stage stage)
    {
        watchdog_hw->scratch[2] = kMagic;
        watchdog_hw->scratch[3] = static_cast<uint32_t>(stage);
    }

    /* First thing at boot: keeps the stage the previous boot reached and why it reset, before
     * anything arms the watchdog (watchdog_enable() writes the marker the SDK reads the reset
     * cause from), then starts this boot. */
    inline void begin()
    {
        Previous& previous = previous_storage();
        previous.stage = watchdog_hw->scratch[2] == kMagic ? static_cast<Stage>(watchdog_hw->scratch[3])
                                                            : Stage::NONE;
        previous.by_watchdog = watchdog_caused_reboot();
        previous.hang = watchdog_enable_caused_reboot();
        set(Stage::STDIO);
    }

    /* The stage the previous boot reached (NONE after a power cycle) and why it reset. */
    inline const Previous& previous() { return previous_storage(); }

    inline bool in_main_loop(Stage stage) { return stage >= Stage::LOOP_TASKS; }

    inline const char* name(Stage stage)
    {
        switch (stage)
        {
            case Stage::STDIO:       return "stdio init";
            case Stage::CORE_INIT:   return "flash lockout init";
            case Stage::BOARD_INIT:  return "board init";
            case Stage::ESP32_PINS:  return "ESP32 pins";
            case Stage::FLASH:       return "flash settings";
            case Stage::DRIVER:      return "output mode driver";
            case Stage::CORE1:       return "Core1 start";
            case Stage::USB_HOST:    return "USB host port";
            case Stage::ESP32_UART:  return "ESP32 UART";
            case Stage::ESP32_RESET: return "ESP32 reset";
            case Stage::USB_DEVICE:  return "USB device init";
            case Stage::RUNNING:     return "main loop";
            case Stage::UART_BRIDGE: return "ESP32 bridge";
            case Stage::LOOP_TASKS:     return "tasks";
            case Stage::LOOP_FLASH_LOG: return "flash log";
            case Stage::LOOP_USB:       return "USB";
            case Stage::LOOP_ESP_RX:    return "ESP32 output";
            case Stage::LOOP_USB_HOST:  return "USB host port";
            case Stage::LOOP_LED:       return "status LED";
            default:                 return "unknown";
        }
    }

} // namespace boot_stage

#endif // _OGXM_BOOT_STAGE_H_
