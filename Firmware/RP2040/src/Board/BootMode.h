#ifndef _OGXM_BOOT_MODE_H_
#define _OGXM_BOOT_MODE_H_

#include <cstdint>
#include <hardware/watchdog.h>

/* A one-shot mode for the next boot, kept in watchdog scratch registers 0-1 (the SDK's reboot
 * uses 4-7): no flash write, gone after a power cycle. Asked from the web app's USB commands
 * (WebApp::PacketID::SYSTEM), so the adapter's own USB port does everything on the RP2040 + ESP32
 * hybrid: ESP32 updates and logs go through the RP2040. */
namespace boot_mode {

    enum class Mode : uint32_t
    {
        NORMAL = 0,
        ESP32_FLASH = 1,    // hybrid: USB serial to the ESP32 in its ROM bootloader (esptool)
        ESP32_MONITOR = 2,  // hybrid: USB serial with the kept log, then the ESP32's live output
    };

    constexpr uint32_t kMagic = 0x0B0071DEu;

    inline void set(Mode mode)
    {
        watchdog_hw->scratch[0] = kMagic;
        watchdog_hw->scratch[1] = static_cast<uint32_t>(mode);
    }

    /* The mode asked for this boot (once: cleared when read). */
    inline Mode take()
    {
        const bool asked = watchdog_hw->scratch[0] == kMagic;
        const uint32_t mode = watchdog_hw->scratch[1];
        watchdog_hw->scratch[0] = 0;
        watchdog_hw->scratch[1] = 0;
        return asked ? static_cast<Mode>(mode) : Mode::NORMAL;
    }

} // namespace boot_mode

#endif // _OGXM_BOOT_MODE_H_
