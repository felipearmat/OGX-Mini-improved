# Unit tests

Host-side unit tests for the firmware logic this fork fixed or added. They build with the
computer's own compiler (no Pico SDK, no board needed) and run in under a second. Run them before
every commit and PR: each one pins a bug that was found on hardware, so a failing test means that
bug is back. Hardware checks for the RP2040 + ESP32 hybrid: [HYBRID_TEST_PLAN.md](HYBRID_TEST_PLAN.md);
automated tests on a real adapter (settings saved live, restarts, the ESP32 link):
[Tools/bench](../Tools/bench/README.md), whose `logger.py` keeps everything the adapter says on the
bench in files; the web app against the firmware's own code, no board
needed: [Virtual adapter](#virtual-adapter-web-app-end-to-end-tests) below.

CI runs the same tests on every push and pull request (`.github/workflows/improved-ci.yml`, job
`host-tests`), the web app's end-to-end tests against this firmware (job `webapp-e2e`), together with the Pico 2 W firmware builds (Release builds have no compiler warnings; one
fails the build), the firmware rules (`python3 .github/scripts/check_firmware_rules.py`:
checks in the source for bugs that only showed on hardware, such as a Core1 entry that does not
register for the flash lockout), the ESP32 companion firmware build, the
shared-sources check (`python3 .github/scripts/check_shared_sources.py`, see
[Firmware/common/README.md](../Firmware/common/README.md)) and the documentation checks below. A PR
is only ready to merge when every job passes. Steps that hang or depend on the network (submodules, apt, Pico SDK, configure + build)
are retried up to 5 times, each attempt under its own time limit (`.github/scripts/retry.sh`), so a
stuck download does not cost a whole image.

## Running them

Requirements: CMake 3.16+, Ninja (or Make), a C/C++ compiler with C++20, and for the Bluepad32
Switch parser test a 32-bit toolchain (`gcc-multilib` / `g++-multilib` on Debian / Ubuntu; the
test is skipped when `-m32` is not available).

The tests use three submodules:

```sh
git submodule update --init --recursive \
    Firmware/external/bluepad32 Firmware/external/libfixmath Firmware/external/tinyusb
```

Build and run, from the repository root:

```sh
cmake -S Firmware/RP2040/tests -B build-tests -G Ninja
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Each test binary can also be run on its own for its detailed output, e.g.
`./build-tests/test_device_reports`. Configuring the tests applies the Bluepad32 Switch parser
patches to the `bluepad32` submodule, the same way the firmware build does.

## What they cover

| Test | Covers |
|---|---|
| `test_device_reports` | The real DInput, PS3, PS4 and STEAM device drivers with TinyUSB stubbed: every HID mode's input report has the size its report descriptor declares (DInput sent 19 of 27 bytes, so macOS dropped all its input); remote wakeup only on a deliberate press, once per suspend; DInput analog pressure from the right buttons; PS3 rumble from Linux (report one byte short); PS4 / STEAM output reports reach the pad (they were dropped), lightbar-only updates keep the rumble, SDL's stop stops it, input GET_REPORT does not repeat the ID; STEAM: touchpad press and Misc are two buttons; PS4: the touchpad press from a wired pad. |
| `test_sony_reports` | DS3 / DS4 / DualSense report helpers: output body alignment, rumble valid flags, touch points, battery, feature reports. |
| `test_sony_imu` | DS4 / DualSense motion units against the calibration report hosts apply; PS4 sensor clock. |
| `test_switch_pro_layout` | Switch Pro button bits against Linux `hid-nintendo` (L3 / R3 swapped). |
| `test_hd_rumble` | Switch HD rumble decoding, with blocks captured from Steam (stuck rumble, wrong amplitude). |
| `test_switch_imu` | Motion for the emulated Switch Pro Controller. |
| `test_switch_parser` | Bluepad32 Switch parser with the patch series, against a fake Joy-Con: vibration enabled at setup, setup retries, stale replies, one pad in setup at a time, timer cleanup, pair IMU side, Joy-Con pairing off, long rumble not interrupted, rumble intensity and the same rumble data on both actuators, sleep request. |
| `test_output_queue` | Bluepad32 output queue with the byte-stream ring buffer: a rumble "stop" after a burst of updates is not dropped; random put / get sequences match a FIFO. |
| `test_scan_policy` | Search for new controllers while others are connected (no 100% BLE scan next to a Classic pad). |
| `test_idle_motion` | Motion as input for the idle turn-off: offsets, noise, spikes, stuck sensors and the controller's own rumble don't count; a clear rotation or tilt does. |
| `test_gamepad_out` | Short host rumble pulses between two feedback samples are not lost; host lightbar. |
| `test_nvstool` | Settings in flash, on a simulated XIP flash (interrupts off while writing, entries past the first sector). |
| `test_wire_pad` | 23-byte pad layout the web app expects. |
| `test_dongle_settings` | Adapter Options defaults and wire / flash format; the status LED options (version 6) restart the adapter; detailed controller diagnostics only with the log in flash. |
| `test_webapp_contract` | What the web app must agree on, written from the firmware's code into `Firmware/common/contract/webapp_contract.json`: packet IDs, output mode values, capability bits, the settings record (length, offsets, defaults, how every older version decodes) and the colour per mode. Fails when the committed file is out of date (`./build-tests/test_webapp_contract --write` rewrites it, then copy it to the web app's `tests/contract/`). The web app's own tests (`npm test` in OGX-Mini-improved-WebApp) check its constants against the same file, and its CI fails when its copy differs from the firmware's. |
| `test_joycon_settings` | Joy-Con motion defaults and sideways rotation. |
| `test_reported_mac` | MAC address reported by the emulated DS4 / DualSense. |
| `test_kbm` | Mouse + Keyboard mode: settings wire format and the pad-to-report mapping. |
| `test_diagnostics` | Diagnostics: controller identity, link and input timing, wired controllers, USB output, the session summary kept across the mode change, the JSON report. |
| `test_esp32_bridge` | I2C link of the RP2040 + ESP32 hybrid: packet layout byte by byte, mode IDs and button bits (shared by both firmwares in `Firmware/common`); mode combos unique and holding Start; every hybrid mode reachable by combo and none of them a GPIO or Wii mode; who decides the mode at start-up, and a mode combo on an ESP32 that has not got the RP2040's settings yet (the RP2040 needs the same combo request twice within 2 s). |
| `test_bt_shared` | Bluepad32 code shared by the Pico W and the hybrid's ESP32 (`Firmware/common/Bluepad32`): disconnect combo hold and grace, profile mapper, mode combos (lone Joy-Con stick as D-pad, Start + L3), the restart after the last pad waiting in the first minute after a start, the battery level logged as low, the touchpad press as Misc only on outputs without a touchpad. |
| `test_spin_lock_budget` | Claimed spin locks stay within the Pico SDK's 8 (one more freezes a Release build at boot). |
| `test_mode_indicator` | Boot blink code and lightbar colour per output mode; status LED colour scaled to its brightness (lit channels stay visible, modes stay distinct); the shared status LED (`StatusLed`): blink code first, fast / slow blink by search state, solid without search, written only on a change. |

Checked at build time instead: the Bluetooth per-pad arrays stay sized for every Bluetooth slot
(`static_assert`s in `Bluepad32.cpp`).

Not covered (need the real board): the mode-switch freeze (Core1 parking, watchdog), the
disconnect-combo deadlock in the Bluetooth core, and the GATT header regeneration. Changes there
need a hardware check, described in the PR.

## Virtual adapter (web app end-to-end tests)

`Firmware/RP2040/tests/virtual_adapter` builds the firmware's web app side for the computer: the
real `WebAppDevice` (USB protocol), `UserSettings` and `NVSTool` on a simulated flash, the
diagnostics report and the mode colours, as on a Pico 2 W with the external status LED on GP6. Its
USB serial port is stdin / stdout and its flash a file, so settings outlive a restart:

```sh
cmake -S Firmware/RP2040/tests/virtual_adapter -B build-va && cmake --build build-va
./build-va/virtual_adapter flash.bin
```

It needs only the `libfixmath` and `tinyusb` submodules (the host tests build it too). What the
board would do it does as a process: a restart (a mode change, an option that needs one) exits
with code 3, the USB bootloader with 4, and a new flash file starts in D-Input/Debug; in any other
mode there is no serial port. An optional control channel on fd 3 opens and closes the port (the
page's DTR), holds a mode combo (`M<mode>`) and reports the output mode at boot and the external
status LED's colour (`virtual_board.h`).

The web app (OGX-Mini-improved-WebApp, `e2e/`) runs its Playwright suite in Chromium against it,
with `navigator.serial` replaced by a port wired to the process: connecting, profiles saved across
a restart, a mode change away from D-Input/Debug and its combo back, live options (mode combos,
search times) without a restart, options that need one (status LEDs and brightness among them), the
mode colour preview, code box and save, the motion view's live stream (the virtual adapter's `P`
command), the mouse + keyboard mapping, and the diagnostics panel and report. Both repositories' CI run
it: this one against the web app's master, the web app's against this repository's master.

## Adding a test

- One file per area, `Firmware/RP2040/tests/test_<area>.cpp`, registered in
  `Firmware/RP2040/tests/CMakeLists.txt` with `ogxm_add_test(<name> <test file> <firmware sources>)`.
- Use the macros in `tests/test.h` (`TEST`, `CHECK`, `CHECK_EQ`, `TEST_MAIN`).
- Start the file with a comment naming the regressions it covers.
- Pico SDK and TinyUSB calls are replaced by the mocks in `tests/mocks/` or by stubs in the test
  (see `test_device_reports.cpp`).
- Check that the test fails without the fix before relying on it.

## Documentation checks

Two scripts keep the docs in line with the code; CI runs both (job `docs`):

- `python3 .github/scripts/gen_docs_tables.py --check` — the README's output-mode table (combos,
  LED blinks, lightbar colours) is generated from the firmware source
  (`UserSettings.cpp`, `ModeIndicator.cpp`, `DeviceDriverTypes.h`). After changing a mode, a combo
  or a colour, run it without `--check` to rewrite the table. A new mode needs a row in its
  `MODE_INFO`, or the check fails.
- `python3 .github/scripts/check_doc_links.py` — every relative link in every Markdown file points
  to a tracked file (compared case-sensitively) and every `#anchor` to an existing heading.

