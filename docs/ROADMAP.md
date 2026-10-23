# Roadmap

What this fork plans to add, in priority order. Nothing here is a commitment or a schedule.
Bugs and loose ends of features that already exist are in [TODO.md](TODO.md); finished work is in
the [CHANGELOG](../CHANGELOG.md). The original project's wish list and long research notes (Xbox
One / Series wireless dongle, more retro consoles) stay in
[Planned additions](../Firmware/RP2040/docs/Planned_Additions.md).

| # | Item | Status |
|---|---|---|
| 1 | [Turbo / auto-fire](#1-turbo--auto-fire) | Planned |
| 2 | [Button macros](#2-button-macros) | Planned |
| 3 | [Native mode](#3-native-mode-each-controller-as-itself) | Partly there (STEAM mode for DualSense) |
| 4 | [Several controllers on one adapter](#4-several-controllers-on-one-adapter) | Partly there (Joy-Con pair, Wii U mode) |
| 5 | [Bluetooth out where a system only takes Bluetooth](#5-bluetooth-out-where-a-system-only-takes-bluetooth) | Partly there (Wii mode, wired input only) |
| 6 | [RP2040 + ESP32 hybrid](#6-rp2040--esp32-hybrid) | Builds in CI; not tested on hardware |
| 7 | [Smaller items](#7-smaller-items) | Planned |
| 8 | [ESP32 multi-slave and the 4-channel board](#8-esp32-multi-slave-and-the-4-channel-board) | Last: after features, compatibility and maintainability |

## 0. D-Input/Debug as a full debug mode

Asked for on 2026-10-21: the D-Input/Debug mode should capture everything the adapter can learn
about any supported controller, so a problem can be read without switching modes or tools.

- Input: every button, axis and trigger raw and mapped; motion (the report's `motion` block since
  2026-10-21: the pad's units, g and deg/s, the gyro's recent peak); touchpad; battery; each
  pad's extras (Capture / Home / Mute, back buttons).
- What the host asks for: rumble per motor; Joy-Con / Pro HD Rumble (frequency and amplitude, one
  or both actuators); lightbar and player LEDs; DualSense adaptive triggers.
- Link: what the report already has (RSSI, channels, chip, report rate, losses), plus the time of
  each Joy-Con setup step and the pad's firmware.
- Web app: a live motion view, a box that turns with the controller (orientation from the gyro,
  levelled by gravity) with recentre and axis display, as Yuzu-based emulators show; motion
  streamed over USB in Debug mode, in the units and frame of the chosen output.
  Prototype in the web app since 2026-10-23 (OGX-Mini-improved-WebApp 6c45e65, "Touchpad and Motion"
  panel): the box from the diagnostics report's `motion` block, a few updates per second; the axis
  directions still to be confirmed on hardware.
- One report for the web app and the bench tools, with no mode change.

Decided with the user on 2026-10-23:

1. **What the host asks for** (rumble per motor, Joy-Con / Pro HD Rumble decoded, lightbar, player
   LEDs, DualSense adaptive triggers) is recorded in every output mode, with a short history, and
   read back in D-Input/Debug mode (the host asks for none of it there): a `host_out` block in the
   report. It is part of the advanced report: recorded only with the "Detailed controller
   diagnostics" option (renamed from "Detailed link diagnostics"), kept in flash with the log, and
   that option can only be on while "Keep the log in flash" is on (the web app greys it out
   otherwise). Not chosen: the web app's serial port in every mode (it changes the USB device a
   console sees).
2. **The motion view's frame:** the controller's own movement in the common frame every pad is
   converted to (X right, Y up out of the face, Z toward the player), so the box follows the hand
   with any pad. No toggle to the output's frame for now.
3. **Order:** first the live motion stream (about 100 updates a second, in place of the report
   polling of the prototype), axis directions confirmed on hardware, then raw and mapped input side
   by side; then the host-output record and HD Rumble test patterns.

## 1. Turbo / auto-fire

Hold-to-repeat per button, set in the web app: on / off per button, a few speeds (for example 5,
10, 15, 20 presses per second), and an optional on-controller toggle combo. The repeat runs in the
adapter, so it works in every output mode and on every console. Commercial adapters treat this as
a basic feature; it is the main gap next to them.

## 2. Button macros

A button (or a combo) plays a short recorded sequence: presses, holds and waits, stored in the
adapter's flash and edited in the web app. Same scope as turbo: every output mode, every system.

## 3. Native mode (each controller as itself)

One output mode in which every controller shows up as the controller it is: DualSense as a
DualSense, DualShock 4 as a DualShock 4, Switch Pro / Joy-Cons as a Pro Controller, Xbox pads as
XInput. The adapter converts the reports where the Bluetooth and USB formats differ (DualShock 4
and DualSense use different layouts on each), so the host gets native glyphs, motion, touchpad and
lightbar without picking a mode by hand.

- **Already there:** STEAM mode passes a real DualSense's own report and USB ID through; PS4 mode
  is a complete DualShock 4.
- **Limits:** a DualSense's audio-based haptics do not travel over Bluetooth, so they cannot be
  passed on; Xbox pads keep XInput (their native USB protocol is not HID).
- **Useful for:** PCs without Bluetooth, and PCs you want to wake with the controller (the
  adapter already wakes the PC over USB in every mode; see the README).

## 4. Several controllers on one adapter

- **Up to 4 Bluetooth players per adapter** in the modes where a host accepts several pads on one
  USB device (PC XInput, Switch Pro, DInput). Today `MAX_GAMEPADS` > 1 is only tested in Wii U
  mode. The RP2040 + ESP32 hybrid suits this well (an ESP32 handles 4+ Bluetooth links; the I2C
  link already carries a pad index).
- **Co-pilot:** two controllers merged into one player (accessibility, teaching). The Joy-Con pair
  merge is the existing case to build on.

## 5. Bluetooth out where a system only takes Bluetooth

Most systems take a USB controller, and Bluetooth in → USB out covers them (Switch and Switch Lite
included, through a USB-C OTG adapter with charging pass-through). The exceptions:

- **Wii / Wii U:** Wii mode already emulates a Wiimote with Nunchuk or Classic Controller over
  Bluetooth, but the controller has to be wired (USB). Next: Bluetooth controllers in Wii mode,
  then a **Wii U Pro Controller** for Wii U games that do not take the Classic Controller (the Wii
  U GameCube-adapter mode only works in Super Smash Bros.).
- **Apple TV:** no USB port at all; it accepts DualShock 4, DualSense, Xbox Bluetooth, Switch Pro
  and MFi controllers. Emulating one of those over Bluetooth would bring any other controller.
  Feasibility not checked yet.

Taking controllers and talking to the console on one radio is the hard part (the Pico W radio
already has to pause BLE advertising for a DualShock 4). Two radios avoid it: an ESP32 for the
controllers and a Pico 2 W for the console (item 6).

## 6. RP2040 + ESP32 hybrid

`OGXM_BOARD=ESP32_BLUEPAD32_I2C` (RP2040) or `ESP32_BLUEPAD32_I2C_PICO2` (Pi Pico 2 / Pico 2 W,
RP2350; same pins, the Pico 2 W radio unused): the RP2040 / RP2350 for USB and an original ESP32 (Bluetooth Classic is
needed; the ESP32-S3 / C3 / C6 do not have it) running Bluepad32, linked over I2C. Both firmwares
build in CI and share their common code ([Firmware/common](../Firmware/common/README.md)).

1. **Test on hardware** — first bench test done (Pico 2 W + ESP32 + DualShock 4 on a PC: pairing,
   mode changes over I2C, buttons, rumble). Next: range against a Pico 2 W with the diagnostics
   report (signal, lost reports, input gaps). Prefer an ESP32 with an external antenna (ESP32-WROOM-32U / WROVER-IE); ESP-IDF can
   raise the Classic transmit power to +9 dBm (`esp_bredr_tx_power_set`).
2. **Close the gaps on the ESP32 side** — all of them software, none a hardware limit:
   - ~~shared Bluetooth core~~ — done: the ESP32 runs the Pico W's Bluetooth code
     (`Firmware/common/Bluepad32`: mapping, rumble, Joy-Con pairs, combos, search policy, idle
     turn-off, lightbar, keepalives); ~~motion, touchpad, battery, lightbar, settings and
     diagnostics over I2C~~ — done (I2C link version 2, `Firmware/common/I2C/Esp32Bridge.h`);
     ~~one place for the settings~~ — done (the RP2040 keeps them; the ESP32's BLE web app edits
     them over I2C). Hardware test: [docs/HYBRID_TEST_PLAN.md](HYBRID_TEST_PLAN.md);
   - ~~the link details in the diagnostics~~, ~~the DualSense passthrough in STEAM mode~~,
     ~~DualShock 3 USB pairing~~ (USB host port on GP0 / GP1), ~~one BLE web app~~
     (`Firmware/common/Bluepad32/BLEServer`) — done;
   - ~~flashing and logs through the adapter's USB port~~ — done (`Tools/hybrid`).
3. **Check BLE 4.2 limits:** the original ESP32 has Bluetooth LE 4.2; Switch 2 controllers (BLE)
   may need newer features.

## 7. Smaller items

- **Mouse + Keyboard mode: motion as pointer.** Turn the pad to move the pointer (Joy-Con, DS4,
  DualSense, Switch Pro), as a web app option next to the touchpad one. Needs the axis signs checked
  per pad type on hardware (`MotionImu::remap_to_ds4_playing_frame` gives the DS4 playing frame;
  yaw = pointer X, pitch = pointer Y), a drift deadband and a speed setting. The settings record
  has 5 reserved bytes and flag bit 0x02 free for it.
- **Switch Pro output:** answer subcommands 0x50 (battery voltage) and 0x43 (read IMU registers)
  with data, as GP2040-CE does (0xD0 / 0xC0 replies); today they are only acknowledged.
- **STEAM output:** the DualSense firmware info (0x20) is synthetic; consider a real controller's
  values, as inputtino does.
- **Documentation site:** publish `docs/` as a site (for example MkDocs Material on GitHub Pages)
  once the file layout settles.

## 8. ESP32 multi-slave and the 4-channel board

Last on the list: features, controller compatibility and maintainability come first.

- **One ESP32, one RP2040 per player** (the ESP32 firmware's `CONFIG_MULTI_SLAVE_MODE`, 4 USB
  outputs): the code is there but untested, and the newer link traffic (settings, diagnostics,
  profiles, the ESP32's address) goes to the first RP2040 only. It moves to the shared code
  (an address per output everywhere) as part of this item; until then it is not supported.
- **4-channel I2C board** (`EXTERNAL_4CH_I2C`, four RP2040s, wired controllers): works on its own
  with its own I2C protocol and its own USB host bring-up; moving both to the shared code
  (`Firmware/common/I2C`, `USBHost/PioHostPort`) only if the board stays in use.

## Not planned

- **Native PS4 / PS5 and Xbox One / Series output.** Those consoles only accept controllers with a
  licensed security chip. A PS4 drops an unauthenticated controller after about 8 minutes; PS5
  games need a PS5-licensed device; Xbox One / Series use a signed certificate exchange, and
  Microsoft has blocked unlicensed accessories before. The workable route, as in GP2040-CE, is a
  licensed device plugged into the adapter that only answers the console's authentication
  challenge; keys cannot be read out of a controller over USB or Bluetooth.
