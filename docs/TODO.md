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
  UART interrupt or DMA.
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
