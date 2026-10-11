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

`OGXM_BOARD=ESP32_BLUEPAD32_I2C`: an RP2040 for USB and an original ESP32 (Bluetooth Classic is
needed; the ESP32-S3 / C3 / C6 do not have it) running Bluepad32, linked over I2C. Both firmwares
build in CI and share their common code ([Firmware/common](../Firmware/common/README.md)).

1. **Test on hardware**, against a Pico 2 W with the diagnostics report (signal, lost reports,
   input gaps). Prefer an ESP32 with an external antenna (ESP32-WROOM-32U / WROVER-IE); ESP-IDF can
   raise the Classic transmit power to +9 dBm (`esp_bredr_tx_power_set`).
2. **An RP2350 / Pico 2 W variant** of the board target (today it builds for the RP2040).
3. **Close the gaps on the ESP32 side** — all of them software, none a hardware limit:
   - motion and battery over I2C (they fit in the packet's spare bytes), touchpad and the raw
     DualSense report in a larger packet;
   - the newer mode combos (PS4, STEAM, Wii U, Mouse + Keyboard) by sharing `UserSettings`;
   - the fork's Bluetooth features from `Firmware/RP2040/src/Bluepad32` (search policy,
     diagnostics, idle turn-off, rumble timing) through a shared core with a platform layer;
   - DualShock 3 USB pairing (the ESP32 sends its Bluetooth address to the RP2040).
4. **Check BLE 4.2 limits:** the original ESP32 has Bluetooth LE 4.2; Switch 2 controllers (BLE)
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

## Not planned

- **Native PS4 / PS5 and Xbox One / Series output.** Those consoles only accept controllers with a
  licensed security chip. A PS4 drops an unauthenticated controller after about 8 minutes; PS5
  games need a PS5-licensed device; Xbox One / Series use a signed certificate exchange, and
  Microsoft has blocked unlicensed accessories before. The workable route, as in GP2040-CE, is a
  licensed device plugged into the adapter that only answers the console's authentication
  challenge; keys cannot be read out of a controller over USB or Bluetooth.
