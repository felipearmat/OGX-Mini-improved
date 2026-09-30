# Unit tests

Host-side unit tests for the firmware logic this fork fixed or added. They build with the
computer's own compiler (no Pico SDK, no board needed) and run in under a second. Run them before
every commit and PR: each one pins a bug that was found on hardware, so a failing test means that
bug is back.

CI runs the same tests on every push and pull request (`.github/workflows/improved-ci.yml`, job
`host-tests`), together with the Pico 2 W firmware builds. A PR is only ready to merge when both
jobs pass.

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
| `test_device_reports` | The real DInput, PS3, PS4 and STEAM device drivers with TinyUSB stubbed: DInput analog pressure from the right buttons; PS3 rumble from Linux (report one byte short); PS4 / STEAM output reports reach the pad (they were dropped), lightbar-only updates keep the rumble, SDL's stop stops it, input GET_REPORT does not repeat the ID. |
| `test_sony_reports` | DS3 / DS4 / DualSense report helpers: output body alignment, rumble valid flags, touch points, battery, feature reports. |
| `test_sony_imu` | DS4 / DualSense motion units against the calibration report hosts apply; PS4 sensor clock. |
| `test_switch_pro_layout` | Switch Pro button bits against Linux `hid-nintendo` (L3 / R3 swapped). |
| `test_hd_rumble` | Switch HD rumble decoding, with blocks captured from Steam (stuck rumble, wrong amplitude). |
| `test_switch_imu` | Motion for the emulated Switch Pro Controller. |
| `test_switch_parser` | Bluepad32 Switch parser with the patch series, against a fake Joy-Con: vibration enabled at setup, setup retries, stale replies, one pad in setup at a time, timer cleanup, pair IMU side, idle rumble refresh, long rumble not interrupted, sleep request. |
| `test_rumble_refresh` | When the idle neutral rumble is re-sent to Switch pads. |
| `test_gamepad_out` | Short host rumble pulses between two feedback samples are not lost; host lightbar. |
| `test_nvstool` | Settings in flash, on a simulated XIP flash (interrupts off while writing, entries past the first sector). |
| `test_wire_pad` | 23-byte pad layout the web app expects. |
| `test_dongle_settings` | Adapter Options defaults and wire / flash format. |
| `test_joycon_settings` | Joy-Con motion defaults and sideways rotation. |
| `test_reported_mac` | MAC address reported by the emulated DS4 / DualSense. |
| `test_mode_indicator` | Boot blink code and lightbar colour per output mode. |

Checked at build time instead: the Bluetooth per-pad arrays stay sized for every Bluetooth slot
(`static_assert`s in `Bluepad32.cpp`).

Not covered (need the real board): the mode-switch freeze (Core1 parking, watchdog), the
disconnect-combo deadlock in the Bluetooth core, and the GATT header regeneration. Changes there
need a hardware check, described in the PR.

## Adding a test

- One file per area, `Firmware/RP2040/tests/test_<area>.cpp`, registered in
  `Firmware/RP2040/tests/CMakeLists.txt` with `ogxm_add_test(<name> <test file> <firmware sources>)`.
- Use the macros in `tests/test.h` (`TEST`, `CHECK`, `CHECK_EQ`, `TEST_MAIN`).
- Start the file with a comment naming the regressions it covers.
- Pico SDK and TinyUSB calls are replaced by the mocks in `tests/mocks/` or by stubs in the test
  (see `test_device_reports.cpp`).
- Check that the test fails without the fix before relying on it.
