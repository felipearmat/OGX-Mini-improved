# Plan: profiles instead of modes

Roadmap item 9 ([ROADMAP](ROADMAP.md#9-profiles-instead-of-modes)). Written 2026-11-27, before any
code; check every "today" below against the code when the work starts.

## Why

Upstream issue #152 asked for a quick way to invert the right stick in game (OGX360 does it with
R3 + D-pad). A Discord user wanted Share and the touchpad press swapped in Switch mode only. Both
need a mapping chosen from the controller, per game or per mode. Today neither is possible: the
mapping is one profile per player, for every mode, and only the web app changes it.

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

A **slot table** next to the profiles, instead of putting the output inside `UserProfile`:

- A **slot** is one combo: `{output (DeviceDriverType), profile id}`. Slot *n* is `MODE_COMBOS[n]`;
  the table is one NVS entry (`SLOTS_KEY`, about 16 slots × 2 bytes, plus a version byte).
- **Defaults** = today's behaviour: each slot has its mode's output and the player's active
  profile (a marker value, "follow the active profile"), so nothing changes for anyone who never
  edits the table, and existing settings need no migration.
- A **combo** loads its slot: same output as now → apply the slot's profile at once, no restart
  (`Gamepad::set_profile`, as the web app preview does); a different output → store the output and
  the profile and restart, as a mode change does today.
- **D-Input/Debug fixed** on Start + LB + RB (the original project's web app combo), not in the
  table and not editable, so the web app is always reachable. Start + Left becomes a normal slot.
- `UserProfile` keeps its 190-byte layout: no change for the web app's profile page, BLE, the
  hybrid's I2C profile transfer or the contract tests.

Why not "a profile is an output plus mappings" inside `UserProfile`: it changes the record every
transport carries (web app USB and BLE, the hybrid's I2C link version), needs a settings migration,
and still needs a combo → profile table. The slot table gives the same result to the user.

Options to decide with the maintainer:

1. More profiles than 8? Each is one NVS entry; with 14 slots wanting their own mappings, 8 may be
   short. Check free entries; raising `NVS_SECTORS` moves the journal and the flash log (see the
   CHANGELOG entry on the journal) and needs a migration of the sector layout.
2. The LED colour: the output's colour (today's palette) or a colour per slot. The output's colour
   plus a cue for "not the default profile" (a slow breath on the RGB LED was discussed on
   2026-11-26) keeps the palette meaningful.
3. Slot names in the web app ("Racing", "Inverted camera"), stored in the table (costs bytes) or
   only in the browser.
4. Per-player slots (several pads on one adapter) or one table for all players.

## Work, in order

1. **Firmware core** (`Firmware/common/UserSettings`): `SlotTable` (load / store / defaults /
   version), `ComboCheck` returns a slot instead of a driver, the fixed Debug combo. Host tests:
   defaults equal today's combos, each combo's slot, the Debug combo can't be overridden, an old
   settings flash reads as defaults.
2. **Apply a slot** (`UserSettings`): same output → profile applied live (no write unless the
   active profile is meant to persist: decide); other output → write and restart. Both Pico W and
   standard boards; the hybrid (the ESP32 reads the combos: it sends the slot over I2C, link
   version bump, both chips updated together).
3. **Web app** (`OGX-Mini-improved-WebApp`): a "Combos" page listing each combo with its output and
   profile (pickers), the fixed Debug combo shown read-only; protocol: read / write the table (new
   message, WebAppProtocol.h and the contract JSON); the profile page unchanged.
4. **LED and docs**: colour rule from option 2; README mode table (generated from `ModeCombos.h`:
   `gen_docs_tables.py` learns the slots), Controller_Mappings, Web_App_Guide, CHANGELOG, TESTING.
5. **Hardware**: the five standard test modes (Nintendo, Xbox, KBM, STEAM, D-Input), a slot with
   the same output and another profile switched in game (the #152 case), a slot with another output,
   the Debug combo after a bad table, settings kept across an update from v1.5.x / v1.6.0.

## Risks

- Same-output switch without a restart: the host keeps the device, so a profile with other
  analog-button or trigger settings must not leave stale state (held buttons, triggers) — test.
- Users who rely on Start + Left for the web app (v1.6.0 docs): the fixed Debug combo moves; the
  release notes must say so, and Start + Left can stay Debug by default to soften it.
- Upstream: large; offer the idea first (an issue), then the code in parts if they want it.

## Follow-up for users

Tell the #152 author and the Discord user (Mitsuschi) when a test build exists.
