# Changes to offer upstream

Changes in this fork that should go to the original project
(MegaCadeDev/OGX-Mini-2026). Open one PR per fix against `develop`, with an
issue first when the bug isn't reported yet. Some Bluepad32 parser fixes also
belong in Bluepad32 itself (ricardoquesada/bluepad32).

Keep this list updated with every commit: add new candidates, and mark a line
when its PR is opened or merged.

Status: `todo` = not sent yet, `pr #N` = PR opened, `merged` = accepted upstream.

## Bug fixes (general, every user benefits)

| Commit | Change | Status | Notes |
|---|---|---|---|
| 44147f1 | Switch Pro output: L3/R3 swapped | pr #110 | Every pad showed stick clicks inverted in Switch Pro mode. Small, with test. |
| 83e2514 | Switch Pro output: decode host HD rumble properly | pr #116 | Stuck rumble from Steam UI ticks; wrong low-band amplitude. Adds `Custom/HdRumble` (rename to fit upstream layout). |
| 2c6d768 | PS3 output: rumble from Linux hosts (hid-sony report 0x01 one byte short) | pr #112 | TinyUSB strips the 0x01 padding byte as a report ID. Small. |
| 40c26bc | Bluetooth: per-pad state arrays overflowed by Bluetooth slot 1 | pr #111 | Memory corruption on every disconnect of the second slot (right Joy-Con of a pair). High priority. |
| dacb85b | PS4 output: real motion sensor clock | pr #122 | Bytes 10-11 were a call counter; hosts derive the sample interval from them. One line. |
| 7e188c2 | NVSTool: park the other core for every flash write (flash_safe_execute) | pr #119 | Only the mode-change path stopped Core1. |
| 9adde01 | Bluetooth pads: play host rumble pulses shorter than the feedback tick | pr #118 | Pulses < 250 ms never reached the pad. Touches `Gamepad.h`. |
| 332aed6 | Switch pads: rumble stuck on, stop/restart in long rumble | pr #115 | Idle neutral refresh (Bluepad32 patch) + 350 ms duration. |
| 7d1f0b3 | Switch parser: enable vibration during setup | pr #115 | Joy-Cons never vibrated (subcommand 0x48). Also for Bluepad32 upstream. |
| 4b1fd60 | Switch parser: robust setup, sleep request | pr #115 | Per-step timeouts, stale replies, one pad in setup at a time, timer cleanup. Also for Bluepad32 upstream. |
| 721c322 | Mode switch: park Core1 before flash write, watchdog | pr #119 | Pico W / Pico 2 W froze on mode change. |
| e0629c9 (NVSTool part) | Flash writes with interrupts disabled, buffer off the stack, sector offset | pr #119 | Split from the mode indicator (fork feature) before sending. |
| e18b8ea | Web App: send pad input in the legacy 23-byte layout | pr #120 | Web app never showed live input. |
| bd9aa29 | Disconnect combo: BT-core deadlock (set_led in HCI event) | pr #115 | Deadlock is an upstream bug. The 3 s hold and reboot-after are behaviour changes: offer separately or as options. |
| 6335503 (combo part) | Disconnect combo deferred to a run-loop timer | pr #115 | Tearing the pair down while parsing its report hung the BT core. |
| eada5e5 | PS4 output: host output reports dropped (no rumble / lightbar) | pr #113 (output fix only) | Length check counted the report ID twice. Small, standalone. |
| ed512cf | PS4 output: motion in real DS4 units (16 per deg/s, 8192 per g) | todo | Steam read the Brook-style scaling as DS4 units (gyro 8x). Needs a check with a Brook auth dongle on a PS4 console before offering. |
| c2b93d0 | STEAM output: complete DualSense emulation for non-DualSense pads (motion, touch, battery, features, rumble/lightbar output fix) | pr #114 (output fix only) | Output reports were dropped (same length check as PS4). Output fix sent as #114; the rest is a feature. |
| bda2bdf | PS4 output: complete DS4 emulation for PC hosts (feature reports, touchpad, battery, lightbar) | todo | Calibration/pairing/firmware features were all zero (gyro 8x on Steam, phantom touches). Includes Bluepad32 patch bluepad32_ds4_touchpad.diff (also for Bluepad32 upstream). |
| c8b8a73 | Switch Pro wired input: follow the L3/R3 constant fix | pr #110 | Goes with 44147f1 (same PR); the wired Pro host driver compensated for the old values. |
| 8bc9c22 | STEAM output: DualSense rumble flags as SDL / Steam send them | pr #114 | Goes with the STEAM output-report PR. |
| 49ab92c | DInput output: analog button pressure from the wrong buttons | pr #117 | circle/cross/square pressure rotated one position (PS3 was fixed upstream, DInput not; wiredopposite#265). Unit test of the real driver (`test_device_reports`); not tested on hardware. |

## Web app (MegaCadeDev/OGX-Mini-2026-WebApp)

| Commit (OGX-Mini-improved-WebApp) | Change | Status | Notes |
|---|---|---|---|
| fdc03ec | Output modes: add Wii U, PS4 and STEAM to the mode lists | pr #2 | Firmware accepts them; multi-controller builds could not pick Wii U. |
| 00ed399, 277f936 | Adapter Options panel | todo | Goes with the dongle options feature (0703a2d). `serve.sh` and the README are fork-only. |

## Features (offer, upstream may or may not want them)

| Commit | Change | Status | Notes |
|---|---|---|---|
| f33a17d, 1179a0f | Motion (gyro/accel) in Switch Pro output mode | todo | README upstream says Switch output has no motion. Joy-Con pair IMU side / orientation options. |
| 6335503 (option), 0a67038 | `OGXM_DISCONNECT_PADS_ON_MODE_CHANGE`: every BT pad is turned off before a mode-change reboot (Joy-Cons asked to sleep, others disconnected) | todo | ON in this fork; offer upstream as OFF by default. Now a runtime dongle option (web app). |
| e0629c9 (indicator) | Output mode indicator (LED blinks, DS4/DualSense lightbar colour) | todo | |
| 12a7c2a | Host unit tests (CTest) | todo | CI workflow is fork-specific. Tests could go with the fixes they cover. |
| 0703a2d | Dongle options: runtime settings editable from the web app (6 options, CMake defaults) | todo | Needs the web app side too (OGX-Mini-improved-WebApp: Adapter Options panel), PR to MegaCadeDev/OGX-Mini-2026-WebApp. |
| b2dc5ba | cmake: regenerate the GATT header when the .gatt file changes | pr #121 | Small build fix. |

## Other projects

- wiredopposite/OGX-Mini (original project): dormant; its master is fully in upstream. Issue #118 (L3/R3) and PR #265 (analog axes) match fixes here. Not sending PRs there.
- GP2040-CE: its PS3 driver copies output report 0x01 the way OGX-Mini did before 2c6d768; the Linux rumble fix could be offered there.

## Fork only (not for upstream)

- `docs/TODO.md`, `docs/UPSTREAM.md`, `.github/workflows/improved-ci.yml`, the fork section of `README.md` and fork notes in the docs.
- Removal of the `WebApp` submodule (points to the unmaintained wiredopposite web app; this fork uses OGX-Mini-improved-WebApp).
- `.github/workflows/build.yml`: release job limited to the original repository (keeps the fork from publishing releases).
