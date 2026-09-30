# TODO / technical debt

Known issues in this fork, to fix later.

## Rumble

- **Steam trigger test doesn't rumble with a solo Joy-Con in Switch Pro mode.**
  In Steam's controller settings, pressing ZL/ZR makes Steam send short pulses:
  about 6 rumble-only reports `74 88 3d 62` per motor, then the neutral `00 01 40 40`, in about 200 ms.
  The dongle decodes them (intensity ~127) and holds the peak until the next
  Bluepad32 feedback tick (commit 9adde01), but the solo Joy-Con still doesn't
  vibrate. Paired Joy-Cons and DS4 don't rumble on this screen either. Rumble
  commands from games and apps do work. Next steps:
  - Capture a DebugLite log (`serialog.py`) and check that `set_rumble` runs for the pulse.
  - Check how the Switch parser encodes the pulse: `switch_encode_rumble` gets a
    frequency derived from the magnitude and a fixed amplitude, so a mid-level
    pulse may land on a frequency the Joy-Con barely plays.
  - Also check the Joy-Con solo vs. paired paths in the feedback loop.

- **Rumble intensity control.** Bluepad32's Switch parser encodes rumble with a
  fixed amplitude (500) and maps the requested magnitude to frequency
  (`weak << 2`, `weak`), so the strength doesn't follow what the host asks for.
  Map the magnitude to the amplitude table instead, at fixed frequencies (e.g.
  160 Hz low band and 320 Hz high band). The host side is already decoded as a
  0-255 intensity (`Custom/HdRumble`). Consider passing the host's HD rumble
  through unchanged when both ends are Switch pads. DS4Windows (SwitchProDevice.cs,
  PrepareRumbleData) does exactly this: fixed 320 Hz in both bands, amplitude from the
  table indexed by the requested strength, capped at 800 of 1003.

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

## Buttons

- **Capture button not read with paired Joy-Cons** (Switch Pro output mode).
  A solo Joy-Con (L) was not checked. Look at how the merged pair builds its
  buttons in the Switch parser (the Capture bit comes from the left Joy-Con)
  and at the mapping to `SwitchPro::Btn::CAPTURE`.

## Debug builds

- **DebugLite hangs at boot in Switch Pro mode** (seen after bda2bdf, which shifts
  boot timing slightly). The Switch driver's USB-init logging (core 0, OGXM_LOG with its
  own mutex) collides with Bluepad32's banner/printf (core 1): both stop mid-line and the
  board freezes. Release builds are fine. Make logging safe across cores (one lock for
  every printf, or a ring buffer drained by one core).

## Protocol completeness (from the 2026-09-30 reference review)

- Switch Pro output: subcommands 0x50 (battery voltage) and 0x43 (read IMU registers) are
  only acked; GP2040-CE answers them with data (0xD0 / 0xC0 replies).
- STEAM output: DualSense firmware info (0x20) is synthetic; inputtino returns a real
  controller's dump. Consider using real values.
