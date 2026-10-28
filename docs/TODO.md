# TODO / technical debt

Known issues and loose ends of features that already exist, to fix later. New features and
the plan for them: [ROADMAP.md](ROADMAP.md).

## USB

- **The adapter sometimes does not come back on USB after a restart** (into the ESP32 bridge or log
  monitor, after flashing the ESP32, rarely after a mode change; seen with the ESP32 on the Pico's
  VBUS and on its own USB port). 2026-10-16, reproduced with a script cycling reboot / log monitor
  / bridge on the hybrid (firmware ca7ad13): 1 in 25 transitions, going back from the log monitor
  to D-Input/Debug. The device was gone from its hub port altogether (no enumeration attempt in
  macOS's IOUSB tree, the hub and its other devices fine), the ESP32 still running (its LED
  searching), and it stayed so until the dongle alone was replugged: the RP2040 stopped somewhere
  in its boot before USB started, where no watchdog ran. The hybrid's boot is now covered by the
  watchdog from `main()` and records its stage (`Board/BootStage.h`), so such a hang reboots the
  adapter and the report reads `watchdog timeout during boot (<stage>)`; checked with a forced
  hang. With that build (and the firmware since ca7ad13) no hang in about 290 transitions. Open:
  the stage it stops in, from a report that shows it. An earlier case needed the hub reconnected
  (macOS kept a stale BOOTSEL device on its port), so that one may be a separate problem.
- **Web app: "The device has been lost" on connect until the dongle was replugged** (2026-10-28,
  hybrid dev-opt1 at 72ea233, flatpak Chrome on Linux). The web app had connected earlier that
  day; it failed after the bench logger and a Python script had used the port. Meanwhile the
  adapter never left USB (udev quiet, the same device number, uptime unbroken) and Python could
  still open the port the way Chrome does (9600 baud, DTR / RTS) and talk to it; restarting
  Chrome did not help, replugging did. Watch for it: if it comes back, take `chrome://device-log`
  before replugging, and note what used the port before.
- **Settings: a rewrite that a reset cannot cut** — NVSTool rewrites a whole 4 KB sector (erase,
  then program) for one entry; a reset in between erases 16 settings (2026-10-22: the second sector
  found all 0xFF). Erased entries are now free again, but what they held is lost. Write to a spare
  sector and switch over (or keep two copies) so a cut leaves the old data.
- **Hybrid: the ESP32 hears the RP2040's first answer only about 10 s after a restart** (the
  "TX power" / "Profiles from the RP2040" lines, 2026-10-21/22). It no longer changes the mode,
  but settings, profiles and "uses motion" arrive late. Find where the time goes (the ESP32's
  first GET_EXT, the RP2040's slave start, reads cut short).
- **Hybrid: I2C link checksum (next)** — measured 2026-10-21/22 with a DS4 (about 770 reports/s): 9-21 packets a minute rejected for a wrong length, so others with the right length but flipped bits get through. The RP2040 now drops packets whose length is wrong and needs a
  mode request twice (2026-10-21), but a write with flipped bits inside a valid length still
  arrives as is (a stray button press, a wrong stick value). If `i2c_slave.packets_rejected` in
  the report shows corruption happens, add a CRC-8 to every packet (both firmwares, a flag for
  older ones).
- **Fixture tests (end-to-end from real input)** — capture is in (`RAW_CAPTURE`,
  `Tools/bench/capture_pad.py`). Recorded and checked step by step (2026-10-27): `ds4`,
  `joycon_pair` (right half's IMU only, the pair-IMU default). Still to record (Pico W build,
  D-Input/Debug, web app closed):
  - Joy-Con setup: start the tool, then connect the pair (`joycon_pair_setup --steps idle`), so the
    Switch `0x21` setup replies (SPI stick calibration, firmware) are in a file; the main
    recording was made with the pads already connected.
  - GameSir Nova Lite, one file per mode: Switch (`nova_lite_switch`, also start before
    connecting), Android (`nova_lite_android`), iOS / Xbox-like if it has one, and wired USB
    (XInput, Switch) with `--hidraw` from the PC.
  - Optional: a lone Joy-Con L and R (`joycon_left`, `joycon_right`, sideways), the pair with the
    left half's IMU (Adapter Option), the DS4 on USB with `--hidraw` (wired path).
  Replay test `test_fixtures` in (2026-10-27): `ds4` and `joycon_pair` through PS4 / STEAM /
  D-Input / Switch / XInput. The Joy-Cons' stick calibration there comes from the recording's own
  range (the setup recording above will give the real one); then: the Nova Lite files once
  recorded, the other output modes (Wii U, PS3, Xbox OG...), synthetic fixtures for pads we lack. A DS4 stick pushed fully up or
  left reaches 1, not 0, in PS4 / D-Input (Bluepad32 centres it on 127: `(v - 127) * 4`), a
  one-step loss to look at in the Bluepad32 re-sync. The DS4 calibration feature report is not
  in the recordings yet. Spec: `.ignore/spec-input-fixtures.md`.
- **HD Rumble (Switch mode), next steps** — phase 1 (Pico W) passed the pattern test with a
  Joy-Con pair (2026-10-27). Still to try: a game / Eden with the direct Pro Controller driver, a
  lone Joy-Con, a Pro Controller if we get one, and the lost / late input reports with HD Rumble
  on (two links for a pair; the diagnostics are read in D-Input/Debug, so play in Switch mode,
  then switch and read the kept session). Phase 2 (hybrid, `GET_HD_RUMBLE`) passed the same
  patterns (2026-10-27). Missing: the link figures during a Switch-mode session (about 125 extra
  transfers a second): the ESP32's counters restart with the mode change, so keep them in the
  session summary (or log them as an event) before measuring.
- **Pico SDK 2.3.1 under test (since 2026-10-27)** — the fork moved from 2.1.0 (with four patches)
  to 2.3.1. Done 2026-10-27 on the bench hybrid (RP2040 side): DS4 and the Joy-Con pair in the web
  app, STEAM mode in Steam (touchpad as mouse), mode combos, disconnect combo, ESP32 flashing and
  restarts (settings 0.5 s later), no crash. **Every bug found from now on: check whether the bump causes it** (does it happen on
  v1.4.1, the last 2.1.0 build?). Hardware pass before offering it upstream: Pico 2 W and Pico W
  over Bluetooth (DS4, Joy-Con pair, a BLE pad; connect / reconnect, rumble, idle turn-off, mode
  combos, disconnect combo, Wii mode), a wired pad on the USB host port (PIO USB), the hybrid's
  RP2040 side (I2C link, ESP32 flashing and log bridge), settings saved to flash, the web app,
  a long session (over 71 min: timer wrap). Then the upstream PR (UPSTREAM, Features).
- **Pico W: Pico SDK timer backports (6473d8c) with an Xbox Series pad over BLE** — the case #146
  saw (connect rumble until a button press). Done with a DS4 on a Pico 2 W 2026-10-26; the stall
  needs a pad that sends nothing while idle. Asked in upstream #147 (UPSTREAM item 0).
- **Hybrid: I2C reads at 1 MHz (found 2026-10-26)** — most read errors were the RP2040's replies
  longer than its 16-byte TX FIFO stopping after their first byte at 1 MHz (85-99 % of settings
  replies); the ESP32 now reads at 400 kHz and writes at 1 MHz (`dff4b53`). Still about 1 settings
  read in 20 cut with a DS4 connected. Why exactly the first byte (the ESP32's master ends the read
  about 45 us in) is not known; check on the perfboard build whether 1 MHz reads work there before
  raising `CONFIG_I2C_READ_BAUDRATE`. The entries below predate it.
- **Hybrid: I2C read errors about 12 times higher with controllers since the evening of 2026-10-20**
  — about 4 per second with a Joy-Con pair (120 per 30 s), against 10.7 per 30 s that morning on
  the same jumpers; the same with and without the DMA receive (A/B), about 0.25 per second with no
  controller as before. Check the jumpers first (the board was handled for BOOTSEL around 18:08).
- **Hybrid: I2C read errors on the ESP32 grow with traffic** — 2026-10-20, Pico 2 W hybrid with I2C
  moved to GP26 / GP27 (jumper wires): `companion.i2c_read_errors` 11 at boot, then about 1 every
  5 s with no controller and 77 a few minutes after a Joy-Con pair connected; write errors 1-2,
  no queue drops, no lost reports. No earlier figure to compare (the bench test only checks write
  errors). Not the log kept in flash: with that option off the count kept rising, about 10 every
  30 s with the pair connected. The ESP32's I2C at 400 kHz (test build, same wires): 1.0 per 30 s
  instead of 10.7, 2 at boot instead of about 11, and the right Joy-Con's late reports 1-3 % instead
  of up to 16 %: the 1 MHz edges are too slow on these longer jumpers with 2.2 kΩ pull-ups (signal,
  not damaged pins). Expected to go away on the perfboard build (short SDA / SCL traces, layout in
  hardware/README): count `i2c_read_errors` there at 1 MHz before changing anything. Only if they
  stay: 1 kΩ pull-ups (or a second 2.2 kΩ in parallel), then a configurable ESP32 I2C rate.
- **Debug builds: about 330 compiler warnings in our code** (`-Wconversion` /
  `-Wsign-conversion`, implicit numeric conversions; about 1,900 more from the libraries).
  Release builds have none and CI keeps them at zero.

## Output modes

- **PS3 mode straight into a PS3 console, without an auth adapter.** Only tested through a Brook
  Wingman XE 2. The DInput mode used to answer the PS3's feature report 0x0300 (the "magic bytes" a
  third-party PS3 pad sends) under a ShanWan "PC / PS3" identity, which a PS3 accepts as a generic
  pad; D-Input/Debug dropped it (own VID / PID, generic HID for SDL / Steam). If PS3 mode fails on
  the console, bring that identity back as a PS3-mode variant.
- **Dreamcast signal level.** Sources disagree on whether the Maple Bus data lines are 3.3 V or
  5 V (the Pico's are not 5 V tolerant; a Pico 2's are, while powered). Measure on a console (or a
  second Pico as the host, Tools/bench) before trusting the direct wiring; DreamPicoPort wires the
  pins straight to a Pico.
- **GameCube + N64 in one mode, detecting the console** (both are joybus on GP19): rewrite on
  joybus-pio (MIT; HayBox does the detection: answer 0x00 as a GameCube pad, 0x40 / 0x41 / 0x42 =
  GameCube, 0x01 / 0x02 / 0x03 = N64). The current N64 driver reads about half of each command in
  software and probably fails on a real N64; GameCube ignores 0xFF and 0x42. Bench: a second
  RP2040 running libjoybus's host examples.
- **PS1 / PS2 mode: Core1 outside the flash lockout (Standard board).** `psx_device_main()`
  (`PS1PS2/controller_simulator.c`), which runs on Core1, never calls
  `multicore_lockout_victim_init()`, so settings written while it runs (Start + Select held 2.5 s
  to change the input source, `store_input_source()`, no restart) fall back to "interrupts off on
  Core0" with Core1 executing from flash. Registering it is not enough: Core0's SEL interrupt
  resets and relaunches Core1 on every console transaction (`restart_pio_sm()`), which can run
  during the lockout handshake (both use the SIO FIFO) and brings Core1 back after a mode change
  has halted it (`usb::disconnect_all()`, then the writes run as if Core1 were stopped). Proposed:
  register in `psx_device_main()`; a "hold Core1" state in the PSX simulator (SEL restarts the PIO
  only); weak `flash_op_begin()` / `flash_op_end()` hooks called by NVSTool, used by the Standard
  board to hold it during writes and from the mode change on; `check_firmware_rules.py` extended to
  every function given to `multicore_launch_core1()`. Found reading the code (2026-10-19), not seen
  on hardware; changes the protocol's timing, so test on a PS1 / PS2 before relying on it. The Pico
  W boards run PS1 / PS2 from Core0 (poll mode) and are not affected. Not for upstream yet.
- **Web app over USB on Android.** Chrome on Android has no Web Serial; the web-serial-polyfill
  (Web Serial over WebUSB) would reach the D-Input/Debug serial port from the same web app code.

## Motion

- **DS4 motion calibration in Bluepad32 differs from Linux / DS4Windows** — compared 2026-10-22: (1) the accelerometer bias is computed but not subtracted (Linux: `raw - bias`); (2) the gyro pitch divisor uses `|minus + bias|` where Linux uses `minus - bias` (as for yaw / roll); (3) over Bluetooth it asks feature report 0x02 (the USB id) where Linux and DS4Windows ask 0x05, read in the Bluetooth field order. Measured on the test DS4 (v2, calibration 0x02 read over USB): biases -5 / 8 / 6, plus / minus ±8800-8867, speed 540 / 540 → gyro gain 62.4-62.7 (1/1024 deg/s per count) by Linux's formula; the same bytes in Bluepad32's order and pitch sign give 0.2-0.3 % off, and the uncalibrated default (62.5) is as close. Accelerometer biases 30-68 counts (4-8 mg). So all three are small on this pad (a pad with large biases or asymmetric ranges would show more), and none explains the 360 degree turn integrating to about 349 (3 %): the same pad on a USB cable, with its own calibration and nothing of ours in between, integrated a hand turn to 348 degrees, so that shortfall is the pad or the hand, not the adapter. Fix (1)-(3) by a Bluepad32 patch, also for upstream; low priority.
- **Linux evdev motion (hid-playstation) did not follow a turn** — 2026-10-21, STEAM mode, Joy-Con
  pair: integrating the "Motion Sensors" evdev device gave only the bias drift while the same
  reports' raw gyro (hidraw) integrated to the turn. Either the integration script or how
  hid-playstation reads the synthesized report (calibration, sensor timestamp); matters for Linux
  games reading motion without Steam. Check with `evtest` on the motion device.
- **PS3 mode: Joy-Con / Pro motion still in the Sixaxis-style frame** (gravity on Z), as PS3.cpp
  expects; a DS4 source arrives in the DS4 report frame (gravity on Y) and goes through the same
  conversion, so one of the two is wrong for a DS3 host. Check on a PS3 (or the DS3 test in
  Linux hid-sony) which frame the Sixaxis report wants.

- **"search stopped (controller)" logged twice** for one Start + L3 (hybrid, 2026-10-22). Cosmetic.

## Rumble

- **Hybrid: DS4 rumble in regular pulses after a reconnection** — 2026-10-21, STEAM mode: host
  rumble already on while the DS4 reconnected came as a long buzz with short, evenly spaced
  pauses; the same rumble with the DS4 already connected was continuous. Suspect the lightbar
  update sharing the DS4 output report each feedback tick (mode colour vs host colour after the
  reconnection). Check with the full debug mode (what the host asks, what the ESP32 sends).
- **Hybrid: Joy-Cons not reconnecting for minutes after being turned off a few times** — 2026-10-20,
  Pico 2 W hybrid, Joy-Con pair already paired with the ESP32, turned off directly (link closed,
  reason 0x08) several times in a row: each time the last one left, the ESP32 restarted as
  designed (the white LED blink) and they came back by pressing a button, except twice: once only
  the left one came back (the right one only on the next try), and once neither did for 7.5 min
  (ESP32 restart at 477 s, both back at 926 s). Nothing was logged in between: no connection
  attempt, pairing failure or rejected key. The log now has those events (connection requests,
  failed connections, key requests, scan state; see CHANGELOG): repeat with it.
  **2026-10-20, with the log:** two different ways of turning them off. Start + Select (the
  adapter's sleep request, link closed with reason 0x13): a button press brings the pad back (a
  "link request" from it). A tap on the sync button (reason 0x08): a button press lights its LEDs
  but no connection request ever reaches the ESP32, also after the adapter freed the link (750 ms
  supervision timeout) and restarted. The pad's pairing info (SPI 0x2000) already held the ESP32
  as its host with a link key, the second entry was empty, and writing it again through the
  rail's pairing subcommand (0x01) changed nothing (tried, reverted). The same pads behave the same
  with Windows and macOS: a Joy-Con trait, not an adapter bug. Documented in the README (combos).
  The comebacks above after a sync tap were the ESP32 connecting to the pads (a "key request"
  without a "link request"), not the pads calling: still open is why that took 7.5 min once.
- **Joy-Cons once did not reconnect by pressing a button after the dongle rebooted**
  (2026-09-30, DebugLite log `joycon-hang.log`, 19:42-19:45): the right Joy-Con had just done a
  fresh pairing (its link key had been dropped), both were turned off two minutes later, the
  dongle rebooted with both keys stored, and no connection came from either Joy-Con for 2 minutes
  of button presses; a sync press made the dongle find and connect them with the stored keys.
  Also, the very first time (Release build, no log) the dongle seemed frozen after both Joy-Cons
  were turned off and took minutes to start searching again. Suspect: the reboot after the last
  pad disconnects (upstream #106) is a BT run-loop timer that never fires if the BT core hangs
  during the teardown; a 3 s hardware watchdog is now armed with it as a safety net.
  Not reproduced afterwards: at 20:04 (disconnect combo) and 20:05 (quick sync press) both
  Joy-Cons reconnected on their own a few seconds after the reboot. Watch for it after a fresh
  pairing. Related: a failed L2CAP open makes Bluepad32 drop the pad's link key
  (`uni_bt_bredr.c`), so the pad then needs a fresh sync.

- **Steam trigger test doesn't rumble with a solo Joy-Con in Switch Pro mode.**
  In Steam's controller settings, pressing ZL/ZR makes Steam send short pulses:
  about 6 rumble-only reports `74 88 3d 62` per motor, then the neutral `00 01 40 40`, in about 200 ms.
  The dongle decodes them (intensity ~127) and holds the peak until the next
  Bluepad32 feedback tick (commit 7b0b669), but the solo Joy-Con still doesn't
  vibrate. Paired Joy-Cons and DS4 don't rumble on this screen either, so it is not Joy-Con
  specific. Steam's own rumble test does vibrate a solo Joy-Con (2026-10-02, with the rumble
  intensity fix), as do games and apps. Next steps:
  - Capture a DebugLite log (`serialog.py`) and check that `set_rumble` runs for the pulse.
  - Check whether the ~200 ms pulse survives the 250 ms feedback sampling and the 350 ms
    Bluepad32 rumble duration for every pad type (DS4 too).

- **Far Cry 6 ignores gyro aiming in PS4 mode** (Steam Input gyro as mouse or as joystick,
  Steam Input forced on). Steam reads the DS4 motion correctly (its calibration and test view
  track rotation), and the same DS4 aims fine in Switch Pro mode. Likely Proton handing the game
  the DS4 natively (PlayStation controllers are passed through to games), so Steam's gyro
  output never reaches it. Check with other games; try PROTON_DISABLE_HIDRAW / SDL hints.

- **STEAM mode mouse interface unbound on Linux.** hid-playstation matches both USB interfaces
  (same VID/PID), fails on the mouse one ("Duplicate device found for MAC address") and leaves it
  without a driver. Harmless on Linux (hid-playstation already exposes the DualSense touchpad as a
  pointer, and Steam's desktop layout works), but the extra mouse interface only helps on Windows.
  Options: expose the mouse on a separate VID/PID configuration, or drop it on Linux hosts.

## Wired USB host (Pico W / Pico 2 W)

- **Panic "DMA channel 2 is already claimed" (on hold).** Recorded by the crash handler on
  2026-10-07 (Pico 2 W, XInput mode, core 0, 233 s after boot; no wired controller plugged, only
  GND / GP4 wired on the board). The Pico W host mux starts PIO USB (`tuh_init`, which claims DMA
  channel 2, state machines and PIO program space) when it sees GP0 / GP1 high, and stops it with
  `tuh_deinit` when the device goes away; TinyUSB's PIO USB HCD has no `hcd_deinit`, so nothing is
  released and the next `tuh_init` claims the channel again and panics. Likely trigger here:
  floating GP0 / GP1. Fix idea: initialise the PIO USB host once and reuse it (only reset
  TinyUSB's state) on later plug-ins; also check why the line was seen high with nothing plugged.

## Buttons

- **Capture button not read with paired Joy-Cons** (Switch Pro output mode).
  A solo Joy-Con (L) was not checked. Look at how the merged pair builds its
  buttons in the Switch parser (the Capture bit comes from the left Joy-Con)
  and at the mapping to `SwitchPro::Btn::CAPTURE`.
  Probably fixed by a1e3d7c (Switch Capture now feeds the Misc button, which Switch Pro mode
  sends as Capture); confirm on hardware with a pair and with a solo left Joy-Con, then remove.

## Debug builds

- ~~**Hybrid: the mode colour preview (and its Cancel) reaches the ESP32's LED late with a controller connected**~~ — fixed by the ESP32 reading at 400 kHz (dff4b53): the 37-byte GET_EXT reply now arrives whole. 2026-10-27, DS4 connected, the user's check: the LED follows the preview and Cancel almost at once. The planned GET_PAD colour bytes are dropped.
- **ESP32 LED options applied late with a controller connected** — 2026-10-23, Pico 2 W hybrid: turning the ESP32's external LED off or changing the brightness in the web app took effect much later (the user saw it off a while after). With no controller, the ESP32 logs the new options 1-2 s after the change ("LED options: ...", added for this). With a controller the link showed 42 % of the RP2040's replies cut and about 1800 packets rejected (16 % and 3 with none): the 37-byte GET_EXT reply, the longest on the link, rarely arrives whole then. Same jumper signal problem as the I2C corruption above; check on the soldered board, CRC planned. Since 2026-10-23 the LED options restart the adapter instead (CHANGELOG); the other live options sent to the ESP32 (search times, idle time, transmit power, combo mask, controller diagnostics) still take the slow path, accepted: they matter after a mode change, which restarts the ESP32.
- **ESP32 "status LED" option drives GPIO15** (upstream's pin, `CONFIG_LED_PIN_1`): the 38-pin DevKit has no LED there; its blue LED that flickers with activity is the serial activity LED (not ours). Decide a pin with the final board.
- **The kept log (LogRing) survives a power cut** — 2026-10-22, Pico 2 W hybrid: with the USB cable out 9 s the ring in `.uninitialized_data` still passed its magic and bounds check and `monitor` showed the previous session (intact this time). Only the magic is checked, not the reset cause, so after a longer cut a half-decayed ring could show garbage as the "kept log". Low priority: drop the ring on a power-on / brown-out reset (RP2040 `vreg_and_chip_reset`, RP2350 `powman` chip reset bits), keep it on watchdog reboots.
- **DebugLite hangs at boot in Switch Pro mode** (seen after f259b0f, which shifts
  boot timing slightly). The Switch driver's USB-init logging (core 0, OGXM_LOG with its
  own mutex) collides with Bluepad32's banner/printf (core 1): both stop mid-line and the
  board freezes. Release builds are fine. Make logging safe across cores (one lock for
  every printf, or a ring buffer drained by one core).
