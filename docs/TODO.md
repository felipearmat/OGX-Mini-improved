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
  through unchanged when both ends are Switch pads.

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
- `s_ps4_rumble_ok_ms` has MAX_GAMEPADS entries but the feedback loop indexes it with the
  Bluetooth slot (up to CONFIG_BLUEPAD32_MAX_DEVICES - 1): out of bounds for a DS4 in
  slot 1 (upstream code).
