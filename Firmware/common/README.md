# Firmware/common — code shared by every firmware

Sources here are built by both the RP2040 / RP2350 firmware (`Firmware/RP2040`, every
`OGXM_BOARD`) and the ESP32 companion firmware (`Firmware/ESP32`, the Bluetooth side of
`ESP32_BLUEPAD32_I2C`). Each file has one copy, so a change reaches every board.

| Path | What |
|------|------|
| `I2C/Esp32Bridge.h` | I2C wire format between the RP2040 and the ESP32 (packets, IDs, slave address) |
| `USBDevice/DeviceDriver/DeviceDriverTypes.h` | output mode IDs (cross the I2C link, stored in flash / NVS) |
| `Gamepad/GamepadBits.h` | button / D-pad bits and analog offsets; both `Gamepad` types derive from it |
| `Gamepad/Range.h`, `Gamepad/fix16ext.h` | range scaling and fixed-point helpers |
| `UserSettings/UserProfile.*`, `JoystickSettings.*`, `TriggerSettings.*` | profile layout and stick / trigger shaping (the web app writes this layout) |

## Rules

- **Same include path as before.** `Firmware/common` is an include root in both builds, so
  `#include "UserSettings/UserProfile.h"` resolves here on every board. Don't keep a copy at
  the same path under `RP2040/src` or `ESP32/main`.
- **No platform headers** (pico-sdk, ESP-IDF, FreeRTOS, TinyUSB, BTstack). The one platform
  interface shared code may use is `Board/ogxm_log.h` (`OGXM_LOG`), which each firmware
  provides.
- **Wire and storage values never move.** `tests/test_esp32_bridge.cpp` pins the I2C layout,
  the mode IDs and the button bits; add new IDs at the end.
- **A file with the same name in both firmwares** is either moved here or listed in
  [`not_shared.txt`](not_shared.txt) with the reason.

## Checks (CI: `.github/workflows/improved-ci.yml`)

- `python3 .github/scripts/check_shared_sources.py` — the rules above (duplicates, stale
  `not_shared.txt` entries, shadowed files, platform headers).
- Unit tests (`Firmware/RP2040/tests`), including `test_esp32_bridge`.
- Firmware builds of every `OGXM_BOARD` and of the ESP32 companion (ESP-IDF 5.5).
