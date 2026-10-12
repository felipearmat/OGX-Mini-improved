# Firmware/common — code shared by every firmware

Sources here are built by both the RP2040 / RP2350 firmware (`Firmware/RP2040`, every
`OGXM_BOARD`) and the ESP32 companion firmware (`Firmware/ESP32`, the Bluetooth side of
`ESP32_BLUEPAD32_I2C`). Each file has one copy, so a change reaches every board.

| Path | What |
|------|------|
| `I2C/Esp32Bridge.h` | I2C wire format between the RP2040 and the ESP32 (packets, IDs, slave address); version 2 adds motion / touchpad / battery, lightbar and settings, diagnostics, profiles |
| `USBDevice/DeviceDriver/DeviceDriverTypes.h` | output mode IDs (cross the I2C link, stored in flash / NVS) |
| `Gamepad/GamepadBits.h` | button / D-pad bits and analog offsets; both `Gamepad` types derive from it |
| `Gamepad/Range.h`, `Gamepad/fix16ext.h` | range scaling and fixed-point helpers |
| `Gamepad/GamepadMapper.h` | a pad's user profile applied to its input: button / D-pad remapping, stick and trigger shaping; the RP2040's `Gamepad` derives from it, the ESP32 uses it as is |
| `Gamepad/RumbleTiming.h` | rumble timing for Bluetooth pads: feedback period, rumble duration, connection rumble, DualShock 4 grace |
| `UserSettings/ModeCombos.h` | output-mode button combos and the modes the RP2040 + ESP32 hybrid offers (the README mode table is generated from it) |
| `UserSettings/UserProfile.*`, `JoystickSettings.*`, `TriggerSettings.*` | profile layout and stick / trigger shaping (the web app writes this layout) |
| `Bluepad32/BtPad.h` | how a Bluetooth pad behaves, for every board that runs Bluepad32 (Pico W / Pico 2 W and the hybrid's ESP32): input mapping, rumble, connection setup, disconnect combo, BLE keepalives, reboot timing |
| `Bluepad32/ScanPolicy.h`, `Bluepad32/ScanControl.*` | when to search for new controllers, and applying it to Bluepad32 / BTstack |
| `Bluepad32/ComboHold.h`, `Bluepad32/IdleOff.h`, `Bluepad32/IdleMotion.h` | the disconnect combo hold; turning an idle controller off (motion counts as use) |
| `Bluepad32/JoyConSettings.*` | Joy-Con motion orientation and pair rumble (dongle options) |
| `UserSettings/ComboCheck.h` | mode combos, Start + L3, a lone Joy-Con's stick as the D-pad, combos turned off in the web app |
| `UserSettings/DongleSettings.*` | the dongle options (web app); on the hybrid the RP2040 sends them to the ESP32 |
| `Board/ModeIndicator.*` | blink code and lightbar colour per output mode |
| `Gamepad/Lightbar.h`, `Gamepad/MotionSource.h` | a lightbar colour; where a pad's motion comes from (crosses the I2C link) |

The platform keeps only what differs: its time source, how it reboots, and where a pad's input
goes (a local `Gamepad` on the Pico W, the I2C packet on the ESP32).

## Rules

- **Same include path as before.** `Firmware/common` is an include root in both builds, so
  `#include "UserSettings/UserProfile.h"` resolves here on every board. Don't keep a copy at
  the same path under `RP2040/src` or `ESP32/main`.
- **No platform headers** (pico-sdk, ESP-IDF, FreeRTOS, TinyUSB, BTstack). The one platform
  interface shared code may use is `Board/ogxm_log.h` (`OGXM_LOG`), which each firmware
  provides. **Exception:** `Bluepad32/` may include Bluepad32 (`uni*.h`, `parser/*.h`): only
  boards that run Bluepad32 build it, and no other shared file may include it.
- **Wire and storage values never move.** `Firmware/RP2040/tests/test_esp32_bridge.cpp` pins the I2C layout,
  the mode IDs and the button bits; add new IDs at the end.
- **A file with the same name in both firmwares** is either moved here or listed in
  [`not_shared.txt`](not_shared.txt) with the reason.

## Checks (CI: `.github/workflows/improved-ci.yml`)

- `python3 .github/scripts/check_shared_sources.py` — the rules above (duplicates, stale
  `not_shared.txt` entries, shadowed files, platform headers, Bluepad32 code outside `Bluepad32/`) and `#include` paths whose upper /
  lower case doesn't match the file (they build on macOS / Windows, not on Linux).
- Unit tests (`Firmware/RP2040/tests`), including `test_esp32_bridge`, `test_bt_shared` and
  `test_scan_policy`.
- Firmware builds of every `OGXM_BOARD` and of the ESP32 companion (ESP-IDF 5.5).
