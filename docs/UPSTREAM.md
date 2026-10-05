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
| 802537e | Switch Pro output: L3/R3 swapped | merged #110 | Every pad showed stick clicks inverted in Switch Pro mode. Small, with test. |
| 3e9aa1e | Switch Pro output: decode host HD rumble properly | merged #116 | Stuck rumble from Steam UI ticks; wrong low-band amplitude. Upstream placed it in `Switch/HdRumble`; the fork follows since the 2026-09-30 sync. |
| 727ceb0 | PS3 output: rumble from Linux hosts (hid-sony report 0x01 one byte short) | merged #112 | TinyUSB strips the 0x01 padding byte as a report ID. Small. |
| 4663f95 | Bluetooth: per-pad state arrays overflowed by Bluetooth slot 1 | merged #111 | Memory corruption on every disconnect of the second slot (right Joy-Con of a pair). High priority. |
| b0de891 | PS4 output: real motion sensor clock | merged #122 | Bytes 10-11 were a call counter; hosts derive the sample interval from them. One line. |
| 850dac4 | NVSTool: park the other core for every flash write (flash_safe_execute) | merged #119 | Only the mode-change path stopped Core1. |
| 7b0b669 | Bluetooth pads: play host rumble pulses shorter than the feedback tick | merged #118 | Pulses < 250 ms never reached the pad. Touches `Gamepad.h`. |
| bca089b | Switch pads: rumble stuck on, stop/restart in long rumble | merged #115 | Idle neutral refresh (Bluepad32 patch) + 350 ms duration. |
| d09b6b2 | Bluepad32: byte-stream output queue (backport of upstream b6531db), drop the idle rumble refresh | todo | Replaces the #115 workaround with the real fix (the 32-slot queue dropped the stop). Its patch-series line touches the one #126 adds: send after #126 or rebase on it. |
| 4d03d39 | Switch parser: enable vibration during setup | merged #115 | Joy-Cons never vibrated (subcommand 0x48). Also for Bluepad32 upstream. |
| a561a98 | Switch parser: robust setup, sleep request | merged #115 | Per-step timeouts, stale replies, one pad in setup at a time, timer cleanup. Also for Bluepad32 upstream. |
| d0978c1 | Mode switch: park Core1 before flash write, watchdog | merged #119 | Pico W / Pico 2 W froze on mode change. |
| 6e3d25b (NVSTool part) | Flash writes with interrupts disabled, buffer off the stack, sector offset | merged #119 | Split from the mode indicator (fork feature) before sending. |
| a139b5d | Web App: send pad input in the legacy 23-byte layout | merged #120 | Web app never showed live input. |
| 297c863 | Disconnect combo: BT-core deadlock (set_led in HCI event) | merged #115 | Deadlock is an upstream bug. The 3 s hold and reboot-after are behaviour changes: offer separately or as options. |
| c7a6f2d (combo part) | Disconnect combo deferred to a run-loop timer | merged #115 | Tearing the pair down while parsing its report hung the BT core. |
| e091b01 | PS4 output: host output reports dropped (no rumble / lightbar) | merged #113 (output fix only) | Length check counted the report ID twice. Small, standalone. |
| a189ac8 | STEAM output: complete DualSense emulation for non-DualSense pads (motion, touch, battery, features, rumble/lightbar output fix) | merged #114 (output fix only), pr #130 (rest) | Output reports were dropped (same length check as PS4). Output fix sent as #114; the rest is a feature. |
| ba53db2 | Switch Pro wired input: follow the L3/R3 constant fix | merged #110 | Goes with 802537e (same PR); the wired Pro host driver compensated for the old values. |
| 7f61088 | Reboot after the last pad disconnects: arm a hardware watchdog too | merged #127 | Upstream #106 reboots from a BT run-loop timer; if the BT core hangs during teardown it never fires (suspected freeze with a Joy-Con pair). Same safety net as the combo. Not confirmed with a log. |
| 3f0a574 | Switch pads: rumble intensity follows the requested magnitude (amplitude, not frequency) | pr #126 (+ 44f2190) | Bluepad32 patch. 44f2190 replaced the DS4Windows recipe with the SDL mapping (same data on both actuators) after the per-side mapping left a single Joy-Con silent for one magnitude; PR updated 2026-10-02 and re-tested on hardware (Release, Joy-Con pair, Switch Pro mode). Same change for Bluepad32: ricardoquesada/bluepad32#232. |
| 77f69f3 | STEAM output: DualSense rumble flags as SDL / Steam send them | merged #114 | Goes with the STEAM output-report PR. |
| dd957f9 | DInput output: analog button pressure from the wrong buttons | merged #117 | circle/cross/square pressure rotated one position (PS3 was fixed upstream, DInput not; wiredopposite#265). Unit test of the real driver (`test_device_reports`); not tested on hardware. |
| b3b200f, 87ba6b7 | Motion (gyro/accel) in Switch Pro output mode | pr #129 (+ 6e63762) | Treated as a fix: Switch output had no motion at all. Includes the three Joy-Con adapter options (CMake defaults), on top of #128. 6e63762 (aligned copies for apply_orientation, Release hang) added to the PR 2026-10-02. |
| 06238bd | Bluetooth pads: host motors swapped in `set_rumble()` (strong request on the small motor) | todo | Every BT pad with two motors; inherited from the original OGX-Mini. Needs a hardware check first. |
| 8c59260 | btstack_config: BTstack logs in Debug builds only (condition was inverted) | todo | Release compiled log calls with no output; Debug had none. Small. |

## Web app (MegaCadeDev/OGX-Mini-2026-WebApp)

| Commit (OGX-Mini-improved-WebApp) | Change | Status | Notes |
|---|---|---|---|
| be79a54 | Output modes: add Wii U, PS4 and STEAM to the mode lists | pr #2 | Firmware accepts them; multi-controller builds could not pick Wii U. |
| 508fe48, 3b249fe | Adapter Options panel | todo | Goes with the dongle options feature (a0e7cba). `serve.sh` and the README are fork-only. |

## Features (offer, upstream may or may not want them)

| Commit | Change | Status | Notes |
|---|---|---|---|
| c7a6f2d (option), 037b5bf | `OGXM_DISCONNECT_PADS_ON_MODE_CHANGE`: every BT pad is turned off before a mode-change reboot (Joy-Cons asked to sleep, others disconnected) | todo | ON in this fork; offer upstream as OFF by default. Now a runtime dongle option (web app). |
| 6e3d25b (indicator) | Output mode indicator (LED blinks, DS4/DualSense lightbar colour) | todo | |
| 0720fea | Pico 2 W external WS2812 status LED in the mode colour (`OGXM_EXT_RGB_*`) | todo | Builds on the mode indicator; send after it. Not tested on hardware yet. |
| 9cf4831 | PS4 output: motion in real DS4 units (16 per deg/s, 8192 per g) | todo | Steam read the Brook-style scaling as DS4 units (gyro 8x). Needs a check with a Brook auth dongle on a PS4 console before offering. Offered as a feature, with the Brook-style scale kept as the Legacy PS4 motion scale option (decided 2026-10-01). |
| f259b0f | PS4 output: complete DS4 emulation for PC hosts (feature reports, touchpad, battery, lightbar) | todo | Calibration/pairing/firmware features were all zero (gyro 8x on Steam, phantom touches). Includes Bluepad32 patch bluepad32_ds4_touchpad.diff (also for Bluepad32 upstream). Offered as a feature, with the Brook-style scale kept as the Legacy PS4 motion scale option (decided 2026-10-01). |
| f0ab2f2 | Host unit tests (CTest) | todo | CI workflow is fork-specific. Tests could go with the fixes they cover. |
| a0e7cba | Dongle options: runtime settings editable from the web app (6 options, CMake defaults) | pr #128 (mechanism only) | Each option goes with the PR of the feature that uses it (Joy-Con options in #129). Web app side (Adapter Options panel) to MegaCadeDev/OGX-Mini-2026-WebApp once options exist upstream. |
| (CI) | `build.yml`: job timeout and Pico SDK cache | merged #124 | Hung jobs were cancelled, but that board lost its UF2. |
| (CI) | `build.yml`: retry hung / failed steps up to 5 times (`.github/scripts/retry.sh`) | merged #125 | Also used by `improved-ci.yml` in the fork. |
| aae625d | Single controller option (one Bluetooth pad, lone Joy-Con does not wait for a partner), Start + L3 (on) / Start + L3 + LB (off), LED blinks while a lone Joy-Con waits | todo | Feature. Includes a Bluepad32 patch (pairing toggle) that only applies on top of MegaCadeDev/bluepad32's Joy-Con pairing. After the fixes are accepted. |
| e9b8735 | Mouse + Keyboard output mode (keyboard, mouse, media keys; mapping from the web app) | todo | Feature; web app side in OGX-Mini-improved-WebApp. After the fixes are accepted. |
| f1f49e9 | PC wake: Switch / DInput / Wii U / PS Classic woke the PC on every loop while suspended; XInput / STEAM never woke it | todo | Bug fix (the first part is an upstream bug: the PC wakes right after suspending). `wake_host_on_press()` in DeviceDriver.h + udev rule in Tools/linux. Verified on hardware (Ally X). |
| c859a0a | Mode combos: single Joy-Con stick at the edge acts as the D-pad | todo | Send only after every fix is accepted upstream and a build with all of them is tested on a Pico 2 W (decided 2026-10-02). |
| 6b8620a | cmake: regenerate the GATT header when the .gatt file changes | merged #121 | Small build fix. |

## Other projects

- wiredopposite/OGX-Mini (original project): dormant; its master is fully in upstream. Issue #118 (L3/R3) and PR #265 (analog axes) match fixes here. Not sending PRs there.
- ricardoquesada/bluepad32 (`develop`), Switch parser patches ported onto the current parser: enable vibration #226 (merged), rumble intensity #227 (merged; its per-side mapping left a single Joy-Con silent for one magnitude, follow-up #232 with the SDL mapping; maintainer will test it), #232 merged 2026-10-03; #228 (idle rumble refresh) closed 2026-10-05: the maintainer replaced the 32-slot output queue with a byte-stream ring buffer (b6531db), which fixes the lost stop (verified on hardware); #229 to #231 rebased onto that `develop` (tests moved to the new `uni_circular_buffer_get()` signature), request sleep #229, per-step setup timeout and stale replies #230 (on top of #226), one pad in setup at a time #231 (on top of #230; reimplemented without global state, not run on hardware). Not sent: DS4 touchpad getter (upstream has no touchpad API at all; better as an issue proposing one), pair IMU side (upstream has no merged Joy-Con pairs; could be a review comment on its open PR #219, which copies motion from the left half). Candidates later: 8BitDo PIDs `2dc8:6003` / `6103`, accepting short DS4 / DS5 reports (behaviour change, needs discussion).
- GP2040-CE: its PS3 driver copied output report 0x01 the way OGX-Mini did before 727ceb0 (motor bytes one off, right motor on with any report from Linux): OpenStickCommunity/GP2040-CE#1740.

## Fork only (not for upstream)

- `docs/TODO.md`, `docs/UPSTREAM.md`, `.github/workflows/improved-ci.yml`, the fork section of `README.md` and fork notes in the docs.
- Removal of the `WebApp` submodule (points to the unmaintained wiredopposite web app; this fork uses OGX-Mini-improved-WebApp).
- `.github/workflows/build.yml`: release job limited to the original repository (keeps the fork from publishing releases).
