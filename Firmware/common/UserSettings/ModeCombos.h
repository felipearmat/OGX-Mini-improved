#ifndef _MODE_COMBOS_H_
#define _MODE_COMBOS_H_

#include <array>
#include <cstdint>

#include "Gamepad/GamepadBits.h"
#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"

/* Output-mode button combos (held about 3 s), shared by the RP2040 firmware and the ESP32
 * companion of the hybrid, which reads the combos itself. The README's mode table is generated
 * from this table (.github/scripts/gen_docs_tables.py). */
namespace mode_combos {

    /* Buttons in the high 16 bits, D-pad in the low byte. */
    constexpr uint32_t combo(uint16_t buttons, uint8_t dpad = 0)
    {
        return (static_cast<uint32_t>(buttons) << 16) | static_cast<uint32_t>(dpad);
    }

    struct ModeCombo
    {
        uint32_t combo;
        DeviceDriverType driver;
    };

    using B = GamepadBits;

    /* Wii, GameCube and N64 are fixed builds only (OGXM_FIXED_DRIVER), without a combo. */
    constexpr std::array<ModeCombo, 15> MODE_COMBOS = {{
        { combo(B::BUTTON_START,                                 B::DPAD_RIGHT), DeviceDriverType::XBOXOG    },
        { combo(B::BUTTON_START | B::BUTTON_RB,                  B::DPAD_RIGHT), DeviceDriverType::XBOXOG_SB },
        { combo(B::BUTTON_START | B::BUTTON_LB,                  B::DPAD_RIGHT), DeviceDriverType::XBOXOG_XR },
        { combo(B::BUTTON_START | B::BUTTON_LB | B::BUTTON_RB),                  DeviceDriverType::WEBAPP    },
        { combo(B::BUTTON_START | B::BUTTON_RB,                  B::DPAD_UP),    DeviceDriverType::KBM       },
        { combo(B::BUTTON_START | B::BUTTON_RB,                  B::DPAD_LEFT),  DeviceDriverType::DINPUT    },
        { combo(B::BUTTON_START,                                 B::DPAD_DOWN),  DeviceDriverType::SWITCH    },
        { combo(B::BUTTON_START | B::BUTTON_LB,                  B::DPAD_DOWN),  DeviceDriverType::WIIU      },
        { combo(B::BUTTON_START,                                 B::DPAD_UP),    DeviceDriverType::XINPUT    },
        { combo(B::BUTTON_START,                                 B::DPAD_LEFT),  DeviceDriverType::PS3       },
        { combo(B::BUTTON_START | B::BUTTON_LB,                  B::DPAD_LEFT),  DeviceDriverType::PS4       },
        { combo(B::BUTTON_START | B::BUTTON_LB,                  B::DPAD_UP),    DeviceDriverType::STEAM     },
        { combo(B::BUTTON_START | B::BUTTON_A),                                  DeviceDriverType::PSCLASSIC },
        { combo(B::BUTTON_START | B::BUTTON_B),                                  DeviceDriverType::PS1PS2    },
        { combo(B::BUTTON_START | B::BUTTON_Y),                                  DeviceDriverType::DREAMCAST },
    }};

    /* Modes of the RP2040 + ESP32 hybrid (ESP32_BLUEPAD32_I2C*): the USB modes. Left out: the GPIO
     * modes (PS1 / PS2, Dreamcast, GameCube, N64 use GP19 / GP21, the I2C link and the UART-bridge
     * select pin), Wii (needs the Pico W radio), the DVD remote (needs a ROM release builds don't
     * have) and Steel Battalion (disabled on the ESP32 by the original project). The first one is
     * the default. */
    constexpr DeviceDriverType HYBRID_MODES[] = {
        DeviceDriverType::XBOXOG,
        DeviceDriverType::DINPUT,
        DeviceDriverType::SWITCH,
        DeviceDriverType::PS3,
        DeviceDriverType::PS4,
        DeviceDriverType::STEAM,
        DeviceDriverType::PSCLASSIC,
        DeviceDriverType::XINPUT,
        DeviceDriverType::WIIU,
        DeviceDriverType::KBM,
        DeviceDriverType::WEBAPP,
    };

} // namespace mode_combos

#endif // _MODE_COMBOS_H_
