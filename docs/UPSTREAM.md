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
| 44147f1 | Switch Pro output: L3/R3 swapped | todo | Every pad showed stick clicks inverted in Switch Pro mode. Small, with test. |
| 83e2514 | Switch Pro output: decode host HD rumble properly | todo | Stuck rumble from Steam UI ticks; wrong low-band amplitude. Adds `Custom/HdRumble` (rename to fit upstream layout). |
| 2c6d768 | PS3 output: rumble from Linux hosts (hid-sony report 0x01 one byte short) | todo | TinyUSB strips the 0x01 padding byte as a report ID. Small. |
| 9adde01 | Bluetooth pads: play host rumble pulses shorter than the feedback tick | todo | Pulses < 250 ms never reached the pad. Touches `Gamepad.h`. |
| 332aed6 | Switch pads: rumble stuck on, stop/restart in long rumble | todo | Idle neutral refresh (Bluepad32 patch) + 350 ms duration. |
| 7d1f0b3 | Switch parser: enable vibration during setup | todo | Joy-Cons never vibrated (subcommand 0x48). Also for Bluepad32 upstream. |
| 4b1fd60 | Switch parser: robust setup, sleep request | todo | Per-step timeouts, stale replies, one pad in setup at a time, timer cleanup. Also for Bluepad32 upstream. |
| 721c322 | Mode switch: park Core1 before flash write, watchdog | todo | Pico W / Pico 2 W froze on mode change. |
| e0629c9 (NVSTool part) | Flash writes with interrupts disabled, buffer off the stack, sector offset | todo | Split from the mode indicator (fork feature) before sending. |
| e18b8ea | Web App: send pad input in the legacy 23-byte layout | todo | Web app never showed live input. |
| bd9aa29 | Disconnect combo: BT-core deadlock (set_led in HCI event) | todo | Deadlock is an upstream bug. The 3 s hold and reboot-after are behaviour changes: offer separately or as options. |
| 6335503 (combo part) | Disconnect combo deferred to a run-loop timer | todo | Tearing the pair down while parsing its report hung the BT core. |
| eada5e5 | PS4 output: host output reports dropped (no rumble / lightbar) | todo | Length check counted the report ID twice. Small, standalone. |
| ed512cf | PS4 output: motion in real DS4 units (16 per deg/s, 8192 per g) | todo | Steam read the Brook-style scaling as DS4 units (gyro 8x). Needs a check with a Brook auth dongle on a PS4 console before offering. |
| c2b93d0 | STEAM output: complete DualSense emulation for non-DualSense pads (motion, touch, battery, features, rumble/lightbar output fix) | todo | Output report was read one byte off (same bug as PS4). Split the output fix into its own PR. |
| bda2bdf | PS4 output: complete DS4 emulation for PC hosts (feature reports, touchpad, battery, lightbar) | todo | Calibration/pairing/firmware features were all zero (gyro 8x on Steam, phantom touches). Includes Bluepad32 patch bluepad32_ds4_touchpad.diff (also for Bluepad32 upstream). |

## Features (offer, upstream may or may not want them)

| Commit | Change | Status | Notes |
|---|---|---|---|
| f33a17d, 1179a0f | Motion (gyro/accel) in Switch Pro output mode | todo | README upstream says Switch output has no motion. Joy-Con pair IMU side / orientation options. |
| 6335503 (option), 0a67038 | `OGXM_DISCONNECT_PADS_ON_MODE_CHANGE`: every BT pad is turned off before a mode-change reboot (Joy-Cons asked to sleep, others disconnected) | todo | ON in this fork; offer upstream as OFF by default. Old name `OGXM_DISCONNECT_JOYCONS_ON_MODE_CHANGE` still accepted. |
| e0629c9 (indicator) | Output mode indicator (LED blinks, DS4/DualSense lightbar colour) | todo | |
| 12a7c2a | Host unit tests (CTest) | todo | CI workflow is fork-specific. Tests could go with the fixes they cover. |

## Fork only (not for upstream)

- `docs/TODO.md`, `docs/UPSTREAM.md`, `.github/workflows/improved-ci.yml`.
- `.github/workflows/build.yml`: release job limited to the original repository (keeps the fork from publishing releases).
