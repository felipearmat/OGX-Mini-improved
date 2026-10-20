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
- **Hybrid: part of the ESP32's serial output lost at boot** — the kept log shows pieces missing
  from the ESP32's first lines ("rst:… Initializing" read as "rstizing", a MAC address cut short).
  The RP2040 reads its UART in the main loop only; its 32-byte RX FIFO lasts about 2.8 ms at
  115200, likely less than the loop takes while booting. Suspected, not measured: read it from the
  UART interrupt or DMA. 2026-10-20 (Pico 2 W hybrid, `improved-v1.3.0-7-g2811166-dev`): the same
  ESP32 boot arrives whole through the UART bridge (where Core1 only moves bytes) and cut in the
  kept log of the normal modes ("rst…lizing", "running at 3"): the wiring is fine, which points at
  the main-loop reading.
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
- **Bench logger stops when a USB device vanishes while it lists the ports** — 2026-10-20, while the
  RP2350 went to BOOTSEL: pyserial's `list_ports.comports()` read an empty `idVendor` and raised
  `TypeError` inside `Logger.watch_usb()` / `ports()` (`Tools/bench/logger.py`), which ended the
  logger. Proposed: catch the error there and try again on the next pass. Not reproduced since
  then (the user suspects the hub it was on that time): 12 cycles in a row (9 `reboot`, 3 `bootsel --uf2`, each
  back in D-Input/Debug) with the logger running, plus about 260 USB events that day, without an
  error. Low priority; apply the fix if it comes back.
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

- **Gyro too sensitive in STEAM mode (Joy-Cons and DS4).** Reported 2026-09-30 on hardware:
  camera / aim moves much faster than the real rotation, and Steam's "gyro stabilization"
  option has to be turned on to make it usable (same for a DS4). Check that the synthesized
  DualSense motion units (`USBDevice/DeviceDriver/Sony/SonyImu.h`) and the calibration report 0x05 agree, for Joy-Con
  (Switch units, `USBDevice/DeviceDriver/Switch/SwitchImu`) and DS4 sources; compare with a real DualSense's degrees per
  count in Steam's calibration view; check whether the noise floor (not only the scale) is what
  the stabilization option hides.

## Rumble

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

- **DebugLite hangs at boot in Switch Pro mode** (seen after f259b0f, which shifts
  boot timing slightly). The Switch driver's USB-init logging (core 0, OGXM_LOG with its
  own mutex) collides with Bluepad32's banner/printf (core 1): both stop mid-line and the
  board freezes. Release builds are fine. Make logging safe across cores (one lock for
  every printf, or a ring buffer drained by one core).
