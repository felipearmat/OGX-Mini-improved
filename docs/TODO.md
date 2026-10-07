# TODO / technical debt

Known issues in this fork, to fix later.

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

## Debug builds

- **DebugLite hangs at boot in Switch Pro mode** (seen after f259b0f, which shifts
  boot timing slightly). The Switch driver's USB-init logging (core 0, OGXM_LOG with its
  own mutex) collides with Bluepad32's banner/printf (core 1): both stop mid-line and the
  board freezes. Release builds are fine. Make logging safe across cores (one lock for
  every printf, or a ring buffer drained by one core).

## Features for a later version

- **Mouse + Keyboard mode: motion (gyro) as pointer.** Turn the pad to move the pointer (Joy-Con,
  DS4, DualSense, Switch Pro), as an option in the web app next to the touchpad one. Left out of
  the first version (decided 2026-10-02): needs the axis signs checked per pad type on hardware
  (`MotionImu::remap_to_ds4_playing_frame` gives the DS4 playing frame; yaw = pointer X, pitch =
  pointer Y), a drift deadband and a speed setting. The settings record has 5 reserved bytes and
  flag bit 0x02 free for it.

## Protocol completeness (from the 2026-09-30 reference review)

- Switch Pro output: subcommands 0x50 (battery voltage) and 0x43 (read IMU registers) are
  only acked; GP2040-CE answers them with data (0xD0 / 0xC0 replies).
- STEAM output: DualSense firmware info (0x20) is synthetic; inputtino returns a real
  controller's dump. Consider using real values.
