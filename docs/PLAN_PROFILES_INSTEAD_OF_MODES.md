# Plan: profiles instead of modes

Roadmap item 9 ([ROADMAP](ROADMAP.md#9-profiles-instead-of-modes)). Written 2026-11-27 (decisions the
same day), before any code; check every "today" below against the code when the work starts.

## Why

Upstream issue #152 asked for a quick way to invert the right stick in game (OGX360 does it with
R3 + D-pad). A Discord user wanted Share and the touchpad press swapped in Switch mode only. Both
need a mapping chosen from the controller, per game or per mode. Today neither is possible: the
mapping is one profile per player, for every mode, and only the web app changes it.

## Decisions (maintainer, 2026-11-27)

- **The colour belongs to the profile**, not to the output mode: each profile has its own LED /
  lightbar colour, chosen in the web app.
- **As many profiles as modes today**, or close to it: 14 (one per combo in `MODE_COMBOS`), not 8.
- **One set of profiles for every controller / player** on the adapter: no per-player profiles,
  no per-player active profile.

## How it works today

- **Mode** = the output only (`DeviceDriverType`). The combos in
  `Firmware/common/UserSettings/ModeCombos.h` (`MODE_COMBOS`, 14 entries; read by `ComboCheck.h`,
  shared with the hybrid's ESP32) pick one; `UserSettings::check_for_driver_change` stores it under
  `DRIVER_TYPE_KEY` and the board restarts (USB re-enumerates as the new device).
- **Profile** = `UserProfile` (`Firmware/common/UserSettings/UserProfile.h`, 190 bytes, packed):
  id, stick and trigger settings, D-pad / button / analog-button mappings. 8 profiles
  (`MAX_PROFILES`), stored under `PROFILE_KEY(id)`; the active one per player under
  `ACTIVE_PROFILE_KEY(index)`. Changed only by the web app (USB `WebApp.cpp`, Bluetooth
  `BLEServer` / `BleStore`, the hybrid over I2C `ESP32_Bluepad32_I2C.cpp`), each a write and a
  restart (`store_profile`, `store_profile_and_driver_type`).
- The mapping is applied once, by the input drivers (`GamepadMapper`, `MAP_*` masks); the outputs
  read fixed bits (since 396cff4).
- Per-mode things already there: the mode colour (`Board/ModeIndicator.cpp`), the modes turned off
  in the web app (`DongleSettings`), XInput's stock stick feel (`GamepadMapper::set_profile_settings`).
- Storage: NVSTool, `NVS_SECTORS=4` with 2 for BTstack's bank on the Pico W boards, so 2 sectors
  of 256-byte entries (about 30, `VALUE_LEN_MAX` 240), plus the 2-sector journal. Count the entries
  in use before adding any.

## Design (recommended)

**Profile *n* is combo *n*.** The 14 combos of `MODE_COMBOS` become 14 profiles; a combo loads its
profile, and a profile says everything about how the adapter behaves:

- **Header table** (new, one NVS entry, `PROFILE_HEADS_KEY`): per profile its output
  (`DeviceDriverType`, 1 byte), its colour (RGB, 3 bytes) and flags; 14 × 4 bytes plus a version
  byte, 57 bytes in one 240-byte entry. Room for a short name per profile if wanted later
  (14 × 10 characters would need a second entry).
- **Mappings**: `UserProfile` as today (190 bytes, one NVS entry each), `MAX_PROFILES` 8 → 14.
  Its layout does not change, so the web app's profile page, BLE, the hybrid's I2C profile transfer
  and the contract tests keep working; only the id range grows (the web app's profile list and the
  checks `id <= MAX_PROFILES` on every transport).
- **Defaults** = today's behaviour: profile *n* has the output of today's combo *n*, its colour
  from today's palette (`ModeIndicator.cpp`) and default mappings. Profiles 1-8 keep their saved
  mappings; the header table is written with the defaults the first time it is missing.
- **Active profile**: one for the adapter (`ACTIVE_PROFILE_KEY` without an index); every connected
  controller uses it. The per-player keys are dropped (frees up to 3 entries).
- **A combo** loads its profile: same output as the active one → apply its mappings at once, no
  restart (`Gamepad::set_profile` for every gamepad, as the web app preview does); a different
  output → store the active profile and restart, as a mode change does today.
- **No flash write on a same-output switch.** The new profile takes effect in RAM at once; the
  active profile is written only after about 10 s on it, or when the controllers go idle. A write
  locks the other core out for some milliseconds (an input hiccup right at the switch) and wears
  the flash when someone flips between two profiles. A power cut before the write only means the
  previous profile comes back at the next start.
- **A pulse of rumble on a switch.** With the same output the host sees nothing change, and the LED
  is often out of sight: a short rumble on every connected controller confirms the switch.
- **Only outputs the board can drive.** A profile set to PS1/PS2 or Dreamcast does nothing on the
  hybrid or on a board without that wiring. The web app offers only the outputs the board
  advertises (`Capabilities.h`); the firmware falls back to the profile's default (and then to the
  board's default output) when a stored profile names one it can't drive.
- **D-Input/Debug fixed** on Start + LB + RB (the original project's web app combo), outside the
  14 profiles and not editable, so the web app is always reachable. D-Input/Debug as an ordinary
  output can still be chosen in a profile.
- **Colour**: the status LED and the DS4 / DualSense lightbar take the active profile's colour; the
  mode colour palette becomes only the defaults. The existing `LED_COLORS_KEY` (custom colours per
  mode) folds into the header table.

Storage, counted on 2026-11-27 (NVSTool, 2 settings sectors, 31 entries): today about 21 in use
(init flag, 8 profiles, active profile per player, driver, input source, date tag, two diagnostics,
dongle, KBM and LED settings). With 14 profiles, one active key and the header table: about 26 of
31. **Raise `NVS_SECTORS` in the same release** rather than later: turbo and macros (roadmap items
1 and 2) will want data per profile, and growing the settings area moves the journal and the flash
log below it (a sector-layout migration, see the journal's CHANGELOG entry). One migration instead
of two; size it for 14 profiles plus room for per-profile turbo / macro records.

Options still open:

1. Short names per profile ("Racing", "Inverted camera"): in the header table (a second entry) or
   only in the browser.
2. A cue for "this profile differs from its defaults" is no longer needed with a colour per
   profile; drop the breathing-LED idea of 2026-11-26 unless wanted for something else.
3. A "next / previous profile" combo was considered and left out: the 14 direct combos cover the
   need without another hidden combo.

Compatibility:

- **Downgrade**: an older firmware flashed over this one must still start. It ignores the header
  table and profiles 9-14; the stored active profile must then read as valid (keep the old
  per-player key holding a profile 1-8, or check the range as `get_active_profile_id` does).
- **Web app and an older firmware**: the output picker, colour and 14-profile list appear only
  when the firmware advertises the feature (a capability bit); otherwise the web app works as
  today.

Web app extras:

- **Duplicate a profile** ("copy from…"): the #152 case is one profile with the right stick
  inverted; copying avoids redoing every mapping.
- **Export / import profiles** (a JSON file): backup, sharing a setup with someone else, support.
  The profile record does not change, so the file is the record plus the header fields.
- **Warn about colours too close** to another profile's, with the CIEDE2000 check used to choose
  the default palette (on LED linear light).

## Work, in order

1. **Firmware core** (`Firmware/common/UserSettings`): the header table (load / store / defaults /
   version), `MAX_PROFILES` 14, one active profile for the adapter, `ComboCheck` returns a profile
   instead of a driver, the fixed Debug combo. Host tests: defaults equal today's combos and
   colours, each combo's profile, the Debug combo can't be overridden, a v1.5.x / v1.6.0 settings
   flash reads as defaults with its saved profiles 1-8 kept, the entry count fits, a profile with an
   output the board can't drive falls back, a downgrade reads a valid active profile. The
   `NVS_SECTORS` increase and its migration (with the journal and the flash log) in this step.
2. **Apply a profile** (`UserSettings`): same output → mappings applied live to every gamepad, a
   rumble pulse, the active profile written later (about 10 s on it, or idle); other output →
   write and restart. Pico W and standard boards; the
   hybrid (the ESP32 reads the combos: it sends the profile number over I2C, link version bump,
   both chips updated together).
3. **Web app** (`OGX-Mini-improved-WebApp`): the profile page gains the output picker, the colour
   (the LED panel's picker moves here) and the combo that loads it; 14 profiles in the list; the
   fixed Debug combo shown read-only; only the outputs the board advertises; duplicate, export /
   import, the close-colour warning; the new page only with firmware that advertises it; protocol:
   read / write the header table (new message, WebAppProtocol.h and the contract JSON).
4. **LED and docs**: colour from the active profile; README mode table (generated from
   `ModeCombos.h`: `gen_docs_tables.py` learns the profiles), Controller_Mappings, Web_App_Guide,
   CHANGELOG, TESTING.
5. **Hardware**: the five standard test modes (Nintendo, Xbox, KBM, STEAM, D-Input), two profiles
   with the same output switched in game (the #152 case), a profile with another output, two
   controllers on one profile, each profile's colour, the rumble pulse, no input hiccup on a
   switch, the Debug combo after a bad table, settings kept across an update from v1.5.x / v1.6.0
   and a downgrade back to v1.6.0.

## Risks

- Same-output switch without a restart: the host keeps the device, so a profile with other
  analog-button or trigger settings must not leave stale state (held buttons, triggers) — test.
- Users who rely on Start + Left for the web app (v1.6.0 docs): the fixed Debug combo moves; the
  release notes must say so, and Start + Left can stay Debug by default to soften it.
- Upstream: large; offer the idea first (an issue), then the code in parts if they want it.

## Follow-up for users

Tell the #152 author and the Discord user (Mitsuschi) when a test build exists.
