# Changes to offer upstream

Changes in this fork that should go to the original project
(MegaCadeDev/OGX-Mini-2026). Open one PR per fix against `develop`, with an
issue first when the bug isn't reported yet. Some Bluepad32 parser fixes also
belong in Bluepad32 itself (ricardoquesada/bluepad32).

Keep this list updated with every commit: add new candidates, and mark a line
when its PR is opened or merged.

Status: `todo` = not sent yet, `pr #N` = PR opened, `merged` = accepted upstream.

## Next fix PRs (send before any feature PR)

In this order; each one on its own branch from upstream `develop`, with its unit test when it has
one. Open upstream: #147 (Pico SDK timer backports, 2026-10-26). #103 (Docker build support) is someone else's. Merged: #129 (Switch Pro motion), #132 (rumble output
queue / SDL mapping), #133 (search policy), #135-#138 (web app firmware PRs, rumble motors),
#139 / #140 (diagnostics, 2026-10-14), #143 (Core1 flash lockout, 2026-10-24), #144 (docs: web app
guide and the diagnostics report in support issues, 2026-10-25). The fork is synced with upstream
`develop` after them (16b77fe, 2026-10-26): #143 / #144 kept in the fork's newer wording and its
own "Firmware rules" CI job (`improved-ci.yml`, so `build.yml` keeps no copy); taken from others:
#145 (README typo, already right in the fork's README) and #146 (georgboe: BTstack timers that
ran only on incoming BT packets), whose 2 ms run-loop poll the fork replaced with the cause's fix
(6473d8c, item 0 below).

0. **Pico SDK timer backports instead of #146's poll** (6473d8c) — #146 polls the BTstack run loop
   every 2 ms because timers ran only when a BT packet came. The cause is two Pico SDK 2.1.0 bugs:
   pico-sdk #2838 (async context leaves `alarm_pending` set after cancelling its alarm with no
   timer left, 2.3.0) and pico-sdk #2127 with #2190 / #2382 (alarm pool leaves the hardware alarm
   disarmed, 2.1.1 / 2.2.0). The SDK's own regression test for #2838 on a Pico 2 W: worker never on
   time on 2.1.0 or with either fix alone, every time with both. PR: the two patches in
   `patch_libs.cmake` (or an SDK bump to 2.3.x, upstream's choice) and `btstack_run_loop_execute()`
   back, crediting #146 for the diagnosis; also fixes the Wii run loop #146 did not cover. Pico 2 W
   pass done 2026-10-26 with a DS4 (mode changes, reboot after the last pad by its timer); the
   stall itself needs a pad silent while idle (Xbox Series BLE): ask georgboe to confirm with his.
   **Open: #147** (2026-10-26, branch `upstream-pr/43-sdk-timer-backports`, ce4a067, worktree
   `~/git/ogxm-pr/43-sdk-timer-backports`; upstream's `git apply` blocks; builds `PI_PICO2W`,
   `PI_PICOW`, `PI_PICO`, `ESP32_BLUEPAD32_I2C` from a clean SDK; text in
   `.ignore/upstream-pr-sdk-timers.md`). georgboe asked to confirm with his Xbox Series pad.
   Upstream's own `build.yml` on the PR branch (run from the fork): the 12 boards that finished
   applied both timer patches (two runners stuck on apt, rerun).
   **SDK bump instead (checked 2026-10-26):** upstream `develop` on Pico SDK 2.3.1 builds
   `PI_PICO2W`, `PI_PICOW`, `PI_PICO`, `PI_PICO2`, `RP2354`, `ESP32_BLUEPAD32_I2C`, `RP2040_ZERO`
   with the same warnings, about 2 KB smaller. 2.3.1 has both timer fixes and makes upstream's two
   SDK patches unneeded (it selects `hids_host.c` itself; CYW43 pre-buffer fixed by pico-sdk
   #2165; neither diff applies, which upstream's CMake takes as "already applied"). The bump PR:
   SDK tag in `build.yml` (clone and cache key), `CMakeLists.txt` and the build docs, drop the two
   SDK patches and their CMake blocks, `btstack_run_loop_execute()` back. It also brings almost
   two years of SDK changes (CYW43 BT writes no longer async, pico-sdk #3042; sleep / low-power
   fixes; alarm pool), so it needs a hardware pass of every Bluetooth and USB-host path first.
   Offered in #147 as the alternative; try it in the fork on the bench before proposing it.
   **The fork moved to 2.3.1 on 2026-10-27** (Features row below), so these two patches live only
   in #147's branch now.

1. **Core1 flash lockout** — the lockout part of cc0526d: every Core1 entry calls
   `multicore_lockout_victim_init()` first (Standard, 4-channel, BlueRetro and the hybrid did not,
   so a settings write could hang Core1 executing from flash), with
   `.github/scripts/check_firmware_rules.py`. Every board with flash writes while Core1 runs.
   High priority, standalone. **Merged: #143** (2026-10-24; branch
   `upstream-pr/42-core1-flash-lockout`, 2f95adf; adds a "Firmware rules" job to `build.yml`).
   The PS1 / PS2 Core1 entry (`psx_device_main()`) is left out on purpose: its fix needs a console
   test first ([docs/TODO.md](TODO.md), Output modes); not for upstream yet.
2. **DInput L2 / R2 buttons from the triggers** — DInput (upstream's mode of the same name) never
   sets the descriptor's L2 / R2 bits, so ZL / ZR do nothing on a PC; PS3 mode sets them. Two lines
   in `DInput.cpp` with the unit test `dinput_triggers_press_the_l2_r2_buttons` (6ac77ee). Small, standalone.
3. **Settings over the Bluetooth pairings** — NVSTool's 4 sectors include BTstack's 2-sector link
   key bank on the Pico W / Pico 2 W; the fix keeps the entries in the 2 before it. Small, with its
   unit test. Upstream has the same NVSTool.
4. **The same report resent with no controller** — a94c0e4
   (`Gamepad.h`): the "new input" flag cleared once the neutral state is delivered. Every board,
   every output driver that checks `new_pad_in()`. Small, with its unit test.
5. **USB host port second start** — 5bee335: Pico-PIO-USB's DMA channel, state machines and
   programs released before the host port starts again (panic "DMA channel 2 is already claimed"
   on the second wired plug, Pico W / Pico 2 W). Standalone.
6. **3dda3e9** — DInput 27-byte input report (macOS showed no input). Standalone.
7. **f1f49e9** — PC wake: one wake request per suspend, XInput / STEAM able to wake.
8. **8c59260** — BTstack logs in Debug builds only (inverted condition). With the warning part of
   e752d7b (Release builds without warnings: Bluepad32's deprecated scan calls, the Wiimote
   library's volatile `memcpy`, `_getentropy`) and the two small ones of 2a0aa04
   (variable-length array in `verify_datetime`, unused packet buffer in `WebAppDevice::process`).
9. **Diagnostics follow-ups on #139 / #140** — 4707bfb (Joy-Cons shown with about 20% lost:
   fractional counter step) and 44abe63 (shorter event texts, kept whole in the stored session).
10. **Trigger settings lost on a second profile apply** — the `TriggerSettings` part of 5945d2d.
   Every board. Minimal upstream version: `TriggerSettings::raw_has_customization()` and the six
   lines of `Gamepad::set_profile_settings()` that use it; with its unit test.
11. **ESP32 companion build** — 87431fb + 09f005a + the CMake line of ff5b909: builds with ESP-IDF
   5.5, the current BTstack port, on Linux (include case) and with any build directory.
12. **One copy of the shared code** — 9f6deb1 (`Firmware/common`, CI sync check) with the shared
   combo table of 1a31468. Sent as a fix: the ESP32 copies had drifted from the RP2040 ones (no
   newer modes, Web App combo ignored, analog buttons off by default), and every later fix lands
   on both boards only through it.
13. **Shared Bluepad32 core** — 5945d2d (without the trigger part sent in 9): the hybrid's ESP32
   behaved differently from the Pico W path in many ways (see its row); one copy fixes them all.
   With the fix part of 7f8b279 (rumble timing, `RumbleTiming.h`).
14. **Hybrid: the Pico W's Bluetooth features and one settings store** — 12c42cc, 1051821,
   2e1dc89, b7ea46e (on top of 12): the rest of the Pico W path shared with the ESP32, I2C link
   version 2, settings kept by the RP2040. Sent as fixes for the same reason as 11 and 12. After the
   hardware test ([docs/HYBRID_TEST_PLAN.md](HYBRID_TEST_PLAN.md)).
15. **Hybrid: maintenance through the adapter's USB port, link diagnostics, one BLE web app, USB
   host port with DualShock 3 pairing, DualSense passthrough** — the commits after b7ea46e (see
   CHANGELOG). The `SYSTEM` web app command and `Tools/hybrid` are useful upstream on every board
   (USB bootloader without the button). After the hardware test.
16. **Hybrid fixes found on hardware (2026-10-13 to 10-16)**, on top of 13 and 14: UART bridge line
   coding never dropped (fix part of 2bbea65: ESP32 flashing failed about 4 in 10), DS4 report
   interval 0 ms on the ESP32 (4942a75, 55% lost), Bluetooth name "Nintendo" (616c652, Joy-Con
   pair dropping a half), QoS 5 ms poll per Classic link (54e1eaf), a pad connecting calls off the
   last-pad restart (f7b7afa), I2C slave back on Core1 (fae5835), one BTstack configuration on the
   ESP32 (9e03880), per-half Joy-Con diagnostics (5698d1c), main-loop and boot watchdogs with the
   boot stage in the report (59e4786, 1997c79), mode change undone after the restart (ModeSync
   part of cc0526d), the ESP32's serial output by DMA into a RAM ring (aeb7c7a, boot lines cut;
   needs the kept log of 15); the I2C slave fixes of 2026-10-21/22 (non-blocking replies, length
   check, mode requests only from combos: 3742e45, 5ea8700); the main loop's step in a hang report
   (8e031db); the Pico-side wires set at build time and UART TX on GP28 (43fc128, 3157f49, d9fe90a,
   c635f04, 1314111; d13b942 reverted).

The hybrid's Pico 2 board target (ff5b909) and the connection rumble on the ESP32 come after these,
then the feature list below. Code shared by the boards goes to `Firmware/common` from now on, so
later fixes reach every board in one PR.

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
| d09b6b2 | Bluepad32: byte-stream output queue (backport of upstream b6531db), drop the idle rumble refresh | merged #132 | Together with the rumble intensity / SDL mapping in one PR (#126 closed 2026-10-05). |
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
| 7f61088 | Reboot after the last pad disconnects: arm a hardware watchdog too | merged #127 | Upstream #106 reboots from a BT run-loop timer; if the BT core hangs during teardown it never fires (suspected freeze with a Joy-Con pair). Same safety net as the combo. Not confirmed with a log. 2026-10-26: checked against the Pico SDK 2.1.0 timer bugs (item 0): not them, the timer fired on time 3/3 on the unpatched SDK (the list never empties there). Cause still unknown; the watchdog stays as the safety net. |
| 3f0a574 | Switch pads: rumble intensity follows the requested magnitude (amplitude, not frequency) | merged #132 (replaces #126) | Bluepad32 patch. 44f2190 replaced the DS4Windows recipe with the SDL mapping (same data on both actuators) after the per-side mapping left a single Joy-Con silent for one magnitude; PR updated 2026-10-02 and re-tested on hardware (Release, Joy-Con pair, Switch Pro mode). Same change for Bluepad32: ricardoquesada/bluepad32#232. |
| 77f69f3 | STEAM output: DualSense rumble flags as SDL / Steam send them | merged #114 | Goes with the STEAM output-report PR. |
| dd957f9 | DInput output: analog button pressure from the wrong buttons | merged #117 | circle/cross/square pressure rotated one position (PS3 was fixed upstream, DInput not; wiredopposite#265). Unit test of the real driver (`test_device_reports`); not tested on hardware. |
| b3b200f, 87ba6b7 | Motion (gyro/accel) in Switch Pro output mode | merged #129 (+ 6e63762) | Treated as a fix: Switch output had no motion at all. Includes the three Joy-Con adapter options (CMake defaults), on top of #128. 6e63762 (aligned copies for apply_orientation, Release hang) added to the PR 2026-10-02. |
| 06238bd | Bluetooth pads: host motors swapped in `set_rumble()` (strong request on the small motor) | merged #136 (2026-10-08) | Every BT pad with two motors. Verified on hardware (DS4, web app Rumble Test). |
| fefe680 | Web App mode: rumble test (`SET_GP_OUT`, 0x81) | merged #135 | First of the firmware PRs the web app needs. |
| fcc1471, a8a202c, 7b0ffbc | Web App mode: unmapped buttons, `GP_TOUCH` (0x82) touchpad packet, live input capped at 100 Hz, TX FIFO cleared on open | merged #137 (2026-10-08) | Second of the web app firmware PRs. |
| a1e3d7c | Bluetooth: DS4 / DualSense touchpad press and Switch Capture as the Misc button (DualSense mute left out) | merged #138 (2026-10-07) | Third of the web app firmware PRs. Verified on hardware (DS4, with #137). |
| ee7dc25 (live part) | Web App mode: diagnostics report (`GET_DIAGNOSTICS`, 0x74): BT / wired controllers, link (from events only), input timing, search state, events ring; Bluepad32 diagnostics hooks patch | merged #139 (2026-10-14) | RAM only, no extra Bluetooth commands. Reduced 2026-10-07 (force push): the HCI link queries (RSSI, channel map, failed contacts, remote version) moved to the link-diagnostics option PR. Verified on hardware (Pico 2 W, DS4). |
| ee7dc25 (session / crash part) | Diagnostics: session summary kept across reboots (RAM + flash key `diag_session`, v1), events across reboots, crash record (hard fault / panic), input-to-output latency for every session end, mode-change / options events | merged #140 (2026-10-14) | Builds on #139. Latency is read on every session freeze (the fork only recorded it on the button combo). Switch firmware version left out (its Bluepad32 patch does not apply on upstream). Verified on hardware (Pico 2 W, DS4). |
| bdfa608 | Diagnostics: "Detailed link diagnostics" dongle option (Adapter Options v8, byte 7), off by default (`OGXM_DIAGNOSTICS_LINK`), applied without a restart: RSSI, channels in use, failed contacts, transmit power, controller Bluetooth chip | todo | Builds on #140; fork first, then upstream; web app checkbox in WebApp #2. |
| 8c59260 | btstack_config: BTstack logs in Debug builds only (condition was inverted) | todo | Release compiled log calls with no output; Debug had none. Small. |
| f1f49e9 | PC wake: Switch / DInput / Wii U / PS Classic woke the PC on every loop while suspended; XInput / STEAM never woke it | todo | Bug fix (the first part is an upstream bug: the PC wakes right after suspending). `wake_host_on_press()` in DeviceDriver.h + udev rule in Tools/linux. Verified on hardware (Ally X). |
| 87431fb | ESP32 companion firmware did not build: ESP-IDF 5.5, BTstack port used in place, missing `<string>`; Switch setup patch reads the SPI address where the request is built | todo | Upstream bug (the hybrid can't be built there either). The Switch setup patch part goes with the Bluepad32 setup-robustness patch. |
| 09f005a | ESP32: `#include` with the wrong case in `board_api.cpp` | todo | Upstream bug, fails on Linux only. One line; send with 87431fb. |
| cc0526d (lockout part) | Core1 registers for the flash lockout on every board (`multicore_lockout_victim_init()`), firmware rule check in CI | todo | Next fix PR 1. Standard / 4-channel / BlueRetro / hybrid: a settings write could hang Core1. Verified on hardware (hybrid, Pico 2 W: 12 live saves in a row). |
| 10f3520 | NVSTool: settings entries kept off BTstack's 2-sector link key bank (Pico W / Pico 2 W; the first-boot format wrote over the pairings) | todo | Next fix PR 2. Unit test. |
| a94c0e4 | `Gamepad::get_pad_in()`: the "new input" flag stayed set with no pad after boot or a disconnect, so every driver resent the same report on every loop | todo | Next fix PR 3. Verified on hardware (hybrid: 0 bytes a second idle). |
| 5bee335 | USB host port: release Pico-PIO-USB's DMA channel, state machines and programs before a second start | todo | Next fix PR 4. Panic on the second wired plug (Pico W / Pico 2 W). Seen in a stored crash record; not reproduced on hardware. |
| e752d7b (warning part), 2a0aa04 (VLA, unused buffer) | Release builds without compiler warnings | todo | With 8c59260 (next fix PR 7). Bluepad32 part as a patch (`bluepad32_no_deprecated_scan_calls.diff`), also for Bluepad32 upstream. |
| 4707bfb, 44abe63 | Diagnostics: fractional counter step (Joy-Con loss shown as 20%), shorter event texts | todo | Next fix PR 8, on top of #139 / #140. |
| 2bbea65 (bridge part) | UART bridge: the host's line coding was dropped when Core1 held the lock (`mutex_try_enter`) | todo | Hybrid ESP32 flashing failed about 4 in 10 (9600 baud kept). Upstream's bridge has the same code. With next fix PR 15 or alone. |
| 3dda3e9 | DInput output: the 27-byte input report the descriptor declares (19 were sent) | todo | macOS drops short input reports: DInput showed no input at all there. Standalone, with the descriptor-size unit test. |
| ff5b909 (ESP32 CMake part) | ESP32 component: `FWDefines.cmake` included from its own directory (`CMAKE_CURRENT_LIST_DIR`) | todo | A build directory outside `Firmware/ESP32` (`idf.py -B`) failed. One line; send with 87431fb. |
| 7f8b279 (fix part) | Hybrid rumble: RP2040 sends the peak since the last read; ESP32 plays longer than its read period and holds DS4 rumble 6 s after connect | todo | Rumble was weak and choppy on the hybrid. The connection rumble in the same commit is a feature. Uses `RumbleTiming.h` in `Firmware/common` (offer after 9f6deb1, or inline the constants). |
| 1a31468 (fix part) | Hybrid: Web App combo ignored by the ESP32 (missing from its valid modes); RP2040 side accepted GPIO modes that take the I2C pins | todo | Sent with 9f6deb1 (item 11 above): the shared combo table is the fix, both sides read the same modes. |
| 12c42cc, 1051821, 2e1dc89, b7ea46e | Hybrid: search control, idle turn-off, combo check, lightbar, Joy-Con pairs, motion / touchpad / battery, diagnostics and settings over I2C (link version 2); the RP2040 keeps the settings | todo | Fixes the hybrid's split settings (a profile saved over Bluetooth applied at the next boot, a mode saved there never reaching the RP2040) and its missing Pico W features. Builds and unit tests; hardware test planned. |
| 5945d2d | Shared Bluepad32 core (`Firmware/common/Bluepad32/BtPad.h`, `ScanPolicy.h`, `ComboHold.h`, `Gamepad/GamepadMapper.h`): the hybrid's ESP32 gets the Pico W path's behaviour; trigger settings kept when a profile is applied again | todo | Hybrid bugs fixed by it: rumble motors swapped; touchpad press not Misc, DualSense mute as Misc; stick shaping on the ESP32 had the bugs already fixed on the RP2040 (ellipse scale uninitialised, degrees for radians, wrong anti-deadzone field, no idle-noise gate); XInput stock stick feel and the Web App default mapping not applied; Wii U Pro digital triggers, Wiimote held sideways, Z as triggers, Nunchuk stick on the right stick; DS4 / DualSense "virtual mouse" slot accepted; no player LED; DualSense adaptive triggers left on; no disconnect combo; no reboot after the last pad disconnects; search for new controllers kept running next to the pad; no Xbox / Switch 2 BLE keepalives. Builds and unit tests; not yet on hardware. |
| 78230eb | STEAM output: digital triggers (Joy-Con / Switch Pro ZL / ZR) reached the host as 0; no L2 / R2 bits | todo | Same defect upstream: `SteamBtReport::ps5_trigger_byte(gp->brake)` reads the analog value only and the report has no L2 / R2 bits. Why: STEAM replaces a non-DualSense pad's triggers with its raw input; Switch pads report ZL / ZR as buttons. Reference: the mapped path's own fallback; L2 / R2 bits as a DualSense, PS4 / PS3 here and GP2040-CE. Unit test included. Upstream has no `BtPad.h`: port the two rules into its SteamBtReport. Confirmed in a game through Steam (2026-10-23). |
| 8ec5554 | Switch output: declared stick calibration matches the output | todo | Goes with 5d3190f (SPI image, right stick at 0x6046): upstream still copies a real Pro Controller's calibration (right centre 2141, ranges 1414-1566) for an output centred at 2047 ± 1489, so hosts scale the sticks off centre and short. Reference: HOJA builds its declared calibration from its output. Unit test included. Confirmed in a game through Steam (2026-10-23). |
| ebfc858 | GameCube output: a trigger at 255 clicks the digital L / R | todo | Same upstream (L / R only from LB / RB): a Joy-Con's ZL / ZR never clicked. Reference: HayBox (digital and analog trigger together); the Wii U adapter mode already clicked at 255. Needs 6048d4d for 8-bit pads to reach 255. Console test still to do. |
| 6048d4d | Bluepad32 patch: full-scale triggers by bit replication | todo | Upstream's Bluepad32 widens 8-bit triggers `* 4` too (a full DS4 trigger reads 254). Reference: SDL `* 257`. Also for Bluepad32 upstream (ricardoquesada/bluepad32: ds3 / ds4 / ds5 / psmove / steam / wii parsers). |
| 0a8924d | Docs: reference projects (docs/REFERENCES.md) | fork only | Where each behaviour decision comes from; cited by the fix PRs above. |
| 414954e | Live motion to the web app (GP_MOTION, MOTION_STREAM) | fork only for now | Needs the fork's web app protocol additions; offer with the motion view. |
| 245710a | Hybrid: OGXM_HYBRID_PICO_W build option (no onboard LED on a Pico W / Pico 2 W) | todo | Why: the hybrid build offered GP25 as a status LED, the radio chip's line on a W. Upstream's hybrid has the same LED_INDICATOR_PIN 25. Small. |
| 31bd745 | Detailed controller diagnostics only with the log in flash | fork only | Fork options (Adapter Options v8). |
| 573db6a | ESP32: log when the LED options arrive | fork only | Diagnostics of the fork's options. |
| b451868 | D-Input/Debug: serial writes never wait for a host that stopped reading (main loop hang) | todo | Why: a program holding the web app's port without reading made `write_serial` wait forever (the watchdog restarted the adapter). Upstream's Web App mode has the same blocking writes. Small. |
| a57eb17 | Diagnostics: per-second rates tick in every board's main loop | todo | Why: the rates rolled over only when the web app asked for a report (reports_per_s 4x). Goes with upstream's diagnostics (#139 / #140). |
| c21e77c | Builds: dependency patches applied to clean trees with a stamp, checked by `Tools/check_patched_trees.py` | todo | Why: a stale Bluepad32 tree (an old patch left in, a new one silently not applied) built firmware that matched no source. Upstream applies patches too. |
| fdf97ec | Bench logger: a line split between a flash page and the RAM keeps its source | fork only | Fork bench tools. |
| 5b33088 | Diagnostics: Classic connection requests, failed connections, key events and the scan state in the log | todo | With upstream's diagnostics; helps "won't pair" reports. |
| 88436f4 | Web app open: sticks and triggers without the profile's settings (RAW_LIVE_INPUT) | fork only for now | Needs the fork's web app (two dots in Axis Settings). Upstream's Web App mode applies the settings too; offer with the web app change. |
| 2fab119 | Status LED options restart the adapter | fork only | Fork options (status LEDs). |
| 667e222 | "Detailed link diagnostics" renamed "Detailed controller diagnostics" | fork only | Labels only. |
| dff4b53 | Hybrid: the mode combo after an ESP32 restart (combo request repeated as a combo), ESP32 I2C reads at 400 kHz / writes at 1 MHz, restart between transfers, restart backoff, "battery low" event | todo | Why: a pad that kept dropping (flat Joy-Con) restarted the ESP32; at 1 MHz the RP2040's replies longer than 16 bytes were cut after one byte 85-99 % of the time, so the settings arrived late, and the combo, sent flagged once, never reached the RP2040's two-request confirmation. Upstream's hybrid also reads at 1 MHz; check whether its replies show the same before sending the rate part. Depends on the fork's shared code (ModeSync.h, mode requests flagged as combos). |
| b14c6d4 | Pico W: diagnostics events for the reboot after the last pad ("reboot in 500 ms (watchdog 3000 ms)", "reboot timer fired") | todo | Why: the reason recorded after the reboot is the same whether the 500 ms timer or the 3 s watchdog fallback rebooted; the events tell them apart (used 2026-10-26 to check #127's case). Small; goes with the diagnostics events upstream already has. |
| c645e1a | STEAM mode: a DS4 / DualSense touchpad press is the touchpad click, not Mute as well; Misc only on outputs without a touchpad; wired DS4 / DualSense press bit | todo | Why: since #138 the press also set Misc, and STEAM sends Misc as Mute next to the click (seen in Steam 2026-10-27); wired DualSense pressed Misc on any touch. SDL keeps TOUCHPAD and MISC1 apart. Upstream: its own STEAM report takes Capture as Mute (`SteamBtReport.h`) and its Bluepad32.cpp sets Misc on the press (#138): check the double press there first. The rule lives in the fork's shared `GamepadMapper`; upstream's version needs the same per-mode flag. Not related to the SDK bump. |

## Web app (MegaCadeDev/OGX-Mini-2026-WebApp)

| Commit (OGX-Mini-improved-WebApp) | Change | Status | Notes |
|---|---|---|---|
| be79a54 | Output modes: add Wii U, PS4 and STEAM to the mode lists | merged #2 (2026-10-14) | Firmware accepts them; multi-controller builds could not pick Wii U. |
| 1b964bc | Diagnostics panel and log report (USB), larger receive buffers, packet resync | merged #2 (2026-10-14) | Needs firmware #139. Session / crash display already in the panel (shown once the follow-up firmware PR sends them). |
| 508fe48, 3b249fe | Adapter Options panel | todo |
| 4ae300c and earlier | Status LED panel, mode colour wheel, D-Input/Debug in the mode lists, capabilities, unit / contract tests | todo | After the firmware features they use (rows above). |
| 5600f85, 9f1b02d | Log report with the adapter's kept log and the log kept in flash; "Keep the log in flash" option in the Diagnostics panel | todo | After the firmware features (GET_LOG, GET_FLASH_LOG). |
| eeb326f, e95834e | Playwright end-to-end tests against the firmware's virtual adapter | todo | With the virtual adapter. | Goes with the dongle options feature (a0e7cba). `serve.sh` and the README are fork-only. |
| 6c45e65 (web app) | Touchpad and Motion panel: a box that turns with the controller (prototype) | fork only for now | Needs the fork's diagnostics report (`motion` block); offer with the live motion stream once it exists. |

## Features (offer, upstream may or may not want them)

| Commit | Change | Status | Notes |
|---|---|---|---|
| c150a67 | Pico SDK 2.3.1 instead of 2.1.0: no SDK patch (its two plus #147's two are in 2.3.1), version check in CMake | todo (under test in the fork) | Why: 2.1.0 needed four patches (hids_host.c, CYW43 pre-buffer, two timer bugs); 2.3.1 has them all, builds every board with no new warning, about 2 KB smaller. Brings about two years of SDK changes, so it goes upstream only after the fork's hardware pass (TODO: Pico SDK 2.3.1 under test). Every fork bug from now on is checked against it first. If #147 is merged first, this PR drops its two patches too. |
| c7a6f2d (option), 037b5bf | `OGXM_DISCONNECT_PADS_ON_MODE_CHANGE`: every BT pad is turned off before a mode-change reboot (Joy-Cons asked to sleep, others disconnected) | todo | ON in this fork; offer upstream as OFF by default. Now a runtime dongle option (web app). |
| 6e3d25b (indicator) | Output mode indicator (LED blinks, DS4/DualSense lightbar colour) | todo | |
| 0720fea | Pico 2 W external WS2812 status LED in the mode colour (`OGXM_EXT_RGB_*`) | todo | Builds on the mode indicator; send after it. Not tested on hardware yet. |
| 9cf4831 | PS4 output: motion in real DS4 units (16 per deg/s, 8192 per g) | todo | Steam read the Brook-style scaling as DS4 units (gyro 8x). Needs a check with a Brook auth dongle on a PS4 console before offering. Offered as a feature, with the Brook-style scale kept as the Legacy PS4 motion scale option (decided 2026-10-01). |
| f259b0f | PS4 output: complete DS4 emulation for PC hosts (feature reports, touchpad, battery, lightbar) | todo | Calibration/pairing/firmware features were all zero (gyro 8x on Steam, phantom touches). Includes Bluepad32 patch bluepad32_ds4_touchpad.diff (also for Bluepad32 upstream). Offered as a feature, with the Brook-style scale kept as the Legacy PS4 motion scale option (decided 2026-10-01). |
| f0ab2f2 | Host unit tests (CTest) | todo | CI workflow is fork-specific. Tests could go with the fixes they cover. |
| a0e7cba | Dongle options: runtime settings editable from the web app (6 options, CMake defaults) | pr #128 (mechanism only) | Each option goes with the PR of the feature that uses it (Joy-Con options in #129). Web app side (Adapter Options panel) to MegaCadeDev/OGX-Mini-2026-WebApp once options exist upstream. |
| (CI) | `build.yml`: job timeout and Pico SDK cache | merged #124 | Hung jobs were cancelled, but that board lost its UF2. |
| (CI) | `build.yml`: retry hung / failed steps up to 5 times (`.github/scripts/retry.sh`) | merged #125 | Also used by `improved-ci.yml` in the fork. |
| (scan policy) | Search for new controllers: stop with every slot in use, 60 s full then reduced search for an open slot, inquiry restart retried, LED shows the search | merged #133 | Bug fix first (a full-duty BLE scan next to a Classic pad loses most of its reports; `ScanPolicy.h` + `Bluepad32.cpp`), the times / combo as a feature after. Replaces the earlier single controller option (aae625d). |
| e9b8735 | Mouse + Keyboard output mode (keyboard, mouse, media keys; mapping from the web app) | todo | Feature; web app side in OGX-Mini-improved-WebApp. After the fixes are accepted. |
| c859a0a | Mode combos: single Joy-Con stick at the edge acts as the D-pad | todo | Send only after every fix is accepted upstream and a build with all of them is tested on a Pico 2 W (decided 2026-10-02). |
| 6b8620a | cmake: regenerate the GATT header when the .gatt file changes | merged #121 | Small build fix. |
| cc0526d, aa40929, e752d7b (combo part) | D-Input/Debug: DInput and the Web App mode in one composite device (HID + CDC, own ID `cafe:4010`), its combo can't be turned off, Web App mode (100) read as D-Input/Debug | todo | Changes DInput's identity (the ShanWan PS3 answer is gone): discuss upstream first. Web app side in the same feature. |
| cc0526d, c8364f5, 12a551c, abc0835 | Status LED options (Pico, Pico GP6, ESP32, ESP32 GPIO 4, brightness; Adapter Options v6) and the web app's colour per mode (`GET_LED_COLORS` / `SET_LED_COLORS` / `LED_PREVIEW`), palette by console family | todo | After the mode indicator (6e3d25b) and 0720fea. Verified on hardware (hybrid, Pico 2 W). |
| 4ab507b | Kept log on every board (diagnostics events as `[rp]` lines, the hybrid's ESP32 output), read without leaving the mode (`GET_LOG`) | todo | Feature, after the diagnostics PRs (merged). `Tools/bench/logger.py` is fork-only. |
| 58ba7e9 | Kept log in flash (`Board/FlashLog`, dongle option "keep the log in flash", `GET_FLASH_LOG`), crash records with the build ID | todo | Feature; after the kept log (`GET_LOG`) and the diagnostics PRs. `OGXM_DEV_DEFAULTS` and the CI dev builds are fork-only. |
| e91f18d (capabilities part) | `GET_CAPABILITIES`: the web app shows only what the hardware has | todo | Needed by the Adapter Options / Status LED panels upstream. |
| 3742e45 (motion part) | Joy-Con / Pro motion in DS4 units for PS3 / PS4 / STEAM, and in the DS4 report frame for PS4 / STEAM (aim about 14 times too strong, gravity at half, Steam reading a pad lying flat as upright) | todo | Upstream has the same remap (Pico W). Fix PR after the hybrid block; the hybrid's I2C gyro scaling and the I2C slave fixes go with items 15-16. |
| 5d3190f | Switch mode: the emulated SPI flash answers any address (Linux hid-nintendo reads the right stick calibration at 0x6046 and got 0xFF: right stick in one quadrant) | todo | Upstream has the same address-by-address answers. Small, self-contained fix PR. |
| 743322d (NVS part) | Settings: erased NVS entries count as free (a cut sector rewrite made saved settings read as defaults and new ones fail, "table full") | todo | Upstream's NVSTool has the same check. Small fix PR with its unit test. |
| c9e3b33 | Firmware version from the git tag, `GET_VERSION` (firmware, board, ESP32) for the web app's update check | todo | |
| 96862ec | Diagnostics: DS4 / DualSense battery and charging state from the pad's status byte (Linux hid-playstation scale), forwarded by the hybrid's ESP32 | todo | Depends on the diagnostics report and LinkDiag (D-Input/Debug). The Bluepad32 DS4 battery formula (level x 25 + 1, level 11 wraps) could go to Bluepad32 upstream as its own fix. |
| 568380c | Hybrid: USB reconnect for "MAC address per controller" (the Pico W loop had it, the hybrid's did not) | todo | Only with the dongle options (Adapter Options); goes with them. |
| 2a0aa04, cc0526d (contract) | Virtual adapter (the web app side of the firmware built for the PC) and the firmware / web app contract file | todo | With the host unit tests (f0ab2f2); upstream's web app could run the same Playwright suite. |
| 9f6deb1 | `Firmware/common`: one copy of the code both firmwares build (I2C bridge format, mode IDs, button bits, profile / stick / trigger settings) | todo | Sent as a fix (item 11 above): the ESP32 copies had drifted (no newer modes, analog buttons off by default). After 87431fb. |

## Other projects

- wiredopposite/OGX-Mini (original project): dormant; its master is fully in upstream. Issue #118 (L3/R3) and PR #265 (analog axes) match fixes here. Not sending PRs there.
- ricardoquesada/bluepad32 (`develop`), Switch parser patches ported onto the current parser: enable vibration #226 (merged), rumble intensity #227 (merged; its per-side mapping left a single Joy-Con silent for one magnitude, follow-up #232 with the SDL mapping; maintainer will test it), #232 merged 2026-10-03; #228 (idle rumble refresh) closed 2026-10-05: the maintainer replaced the 32-slot output queue with a byte-stream ring buffer (b6531db), which fixes the lost stop (verified on hardware); #229 to #231 rebased onto that `develop` (tests moved to the new `uni_circular_buffer_get()` signature), request sleep #229, per-step setup timeout and stale replies #230 (on top of #226), one pad in setup at a time #231 (on top of #230; reimplemented without global state, not run on hardware). 2026-10-19: #229 rebased again onto `develop` (conflict with the new Home LED subcommand in the same `switch`, both cases kept; Bluepad32's posix tests pass with ASan + UBSan); #230 / #231 still merge cleanly; no maintainer comment waiting for an answer. Not sent: DS4 touchpad getter (upstream has no touchpad API at all; better as an issue proposing one), pair IMU side (upstream has no merged Joy-Con pairs; could be a review comment on its open PR #219, which copies motion from the left half). Candidates later: 8BitDo PIDs `2dc8:6003` / `6103`, accepting short DS4 / DS5 reports (behaviour change, needs discussion). **2026-10-23:** #229 (request sleep) merged; #230 / #231 still open. Their `develop` is 102 commits past our pinned `bd81534` (BTstack update, Switch 2 IMU fixes, a `set_led_brightness` parser API...): re-sync our 13 patches onto it (next task). New candidate for them: `bluepad32_full_scale_triggers.diff` (6048d4d).
- GP2040-CE: its PS3 driver copied output report 0x01 the way OGX-Mini did before 727ceb0 (motor bytes one off, right motor on with any report from Linux): OpenStickCommunity/GP2040-CE#1740, merged 2026-10-11.

## Fork only (not for upstream)

- `docs/TODO.md`, `docs/ROADMAP.md`, `docs/UPSTREAM.md`, `.github/workflows/improved-ci.yml`, the fork's `README.md` (rewritten: use cases, hardware, generated mode table) and fork notes in the docs.
- CI checks `.github/scripts/check_shared_sources.py`, `gen_docs_tables.py` and `check_doc_links.py` (could be offered with `Firmware/common` and a README that uses the generated table).
- Removal of the `WebApp` submodule (points to the unmaintained wiredopposite web app; this fork uses OGX-Mini-improved-WebApp).
- `Tools/bench` (bench tests on the fork's own hardware) and the `Tools/hybrid` conveniences (6c20ec8, c6a5a73, 71e1abd: port listing that survives a device leaving).
- `.github/workflows/build.yml`: release job limited to the original repository (keeps the fork from publishing releases).
