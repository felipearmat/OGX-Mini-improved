# RP2040 + ESP32 hybrid — hardware test plan

What to check on hardware for the hybrid (`ESP32_BLUEPAD32_I2C` / `ESP32_BLUEPAD32_I2C_PICO2`)
now that the ESP32 runs the Pico W's Bluetooth code (`Firmware/common/Bluepad32`) and the I2C link
is at version 2 (`Firmware/common/I2C/Esp32Bridge.h`). Everything below builds and has unit tests
for its logic; none of it has been on hardware yet.

Mark each case **pass**, **fail** (with what happened) or **skip** (no controller / host for it).
Save a diagnostics report (web app, Web App mode) after any failure.

## Setup

- **Boards:** Pi Pico 2 W running `ESP32_BLUEPAD32_I2C_PICO2` (or an RP2040 board running
  `ESP32_BLUEPAD32_I2C`) + an original ESP32 (WROOM / WROVER) running the companion firmware.
  Wiring: [hardware/README.md](../hardware/README.md#pi-pico--esp32) (GP18 → 21, GP19 → 22, GND,
  GP8 → EN, 2.2 kΩ pull-ups to 3V3; GP21 not connected). One USB cable to the Pico.
- **Firmware:** both sides from the same commit. Flash the ESP32 first (Pico in BOOTSEL, so it does
  not drive EN; USB-serial adapter on the ESP32), then the Pico. The ESP32's log build prints the
  events below on its serial port (115200 baud) — useful, not required.
- **Host:** a PC with a gamepad tester (e.g. gamepad-tester.com, `evtest` / `jstest-gtk` on Linux,
  Steam's controller settings). Motion and touchpad need PS4 or STEAM mode and a host that shows
  them (Steam, or `evtest` on the motion / touchpad devices).
- **Controllers:** DualShock 4 (required), Switch Pro Controller, a Joy-Con pair; optional:
  DualSense, Wii Remote (+ Nunchuk), Wii U Pro, Xbox Series (BLE), 8BitDo.

## 0. Before: Pico 2 W alone (regression)

The Pico W path was moved onto the same shared code. With `PI_PICO2W` on the Pico (no ESP32):

| # | Steps | Expected |
|---|---|---|
| 0.1 | Pair a DS4; buttons, sticks, triggers, rumble in the tester | as before |
| 0.2 | Joy-Con pair: both halves merge into one pad; motion in Switch mode | as before |
| 0.3 | Start + Select 3 s | pad disconnects, adapter restarts, pad reconnects |
| 0.4 | Hold a mode combo (e.g. Start + Down) 3 s | mode changes; extra buttons held do not block it |
| 0.5 | Start + L3 3 s with a pad connected | one short rumble; LED solid (search stopped) |
| 0.6 | Web app: save a profile with a trigger deadzone, change mode twice | the deadzone still applies after both changes (was lost on the second apply) |
| 0.7 | DS4 in PS4 mode with a game / tool that sets the lightbar | lightbar follows the host |

## 1. Link and boot

| # | Steps | Expected |
|---|---|---|
| 1.1 | Power the hybrid with no controller | ESP32 LED: the mode's blink code, then fast blink (searching) |
| 1.2 | Pair a DS4 | connection rumble; ESP32 LED solid (search off with the slot in use) |
| 1.3 | Disconnect the DS4 (hold PS), wait | ESP32 restarts about 0.5 s later; LED back to fast blink; the PC still sees the USB pad (neutral, no stuck buttons) |
| 1.4 | Turn the DS4 on again | reconnects without pairing again |

## 2. Input

| # | Steps | Expected |
|---|---|---|
| 2.1 | DS4, DInput / Switch / XInput modes: every button, D-pad, sticks, triggers half and full | all reach the host; triggers analog |
| 2.2 | DS4 touchpad press | Misc button |
| 2.3 | DualSense mute button | not Misc |
| 2.4 | Wii U Pro: ZL / ZR | full trigger (digital fallback) |
| 2.5 | Wiimote alone, then with Nunchuk | held upright (TV style) layout; Nunchuk stick = left stick; Z = LB, not a trigger |
| 2.6 | Web app (USB, Web App mode): remap A ↔ B, add a stick deadzone and curve, save; back to a game mode | remap, deadzone and curve applied (from the RP2040's profile) |
| 2.7 | Web app over Bluetooth (ESP32): change a mapping, save | the Pico restarts; the change shows in the USB web app too and applies |
| 2.8 | XInput mode, no stick settings in the profile | stock Xbox 360 stick feel (same as the Pico W) |

## 3. Rumble

| # | Steps | Expected |
|---|---|---|
| 3.1 | Tester: left motor only, then right motor only (DS4) | left = strong (big motor), right = weak — not swapped |
| 3.2 | Long rumble (3 s) | steady, not chopped |
| 3.3 | Short pulses (100–200 ms) | felt |
| 3.4 | DS4 right after connecting, host asks for rumble at once | no rumble for the first ~6 s, link stays up |
| 3.5 | Joy-Con pair, left-only and right-only rumble | per side by default (left motor on the left Joy-Con); "both" option in the web app = both halves |
| 3.6 | Switch Pro rumble at 25 / 50 / 100 % | strength follows |

## 4. Motion, touchpad, battery (new over I2C)

| # | Steps | Expected |
|---|---|---|
| 4.1 | DS4 in PS4 mode, Steam / evtest: tilt and rotate | accelerometer and gyro follow, no axis swapped or stuck |
| 4.2 | DS4 in STEAM mode: touchpad | touch points move the cursor / show in the tester; press = click |
| 4.3 | Switch Pro in Switch mode: gyro aim (e.g. a Switch emulator's motion test) | follows |
| 4.4 | Joy-Con pair: motion; change the pair IMU side and orientation in the web app | follows the chosen half / orientation |
| 4.5 | Lone Joy-Con, solo orientation horizontal | rotated for sideways use |
| 4.6 | Wiimote in PS3 / PS4 mode | accelerometer reported (Wiimote asked for accel reports) |
| 4.7 | Diagnostics report (Web App mode) with a DS4 | battery level shown |

## 5. Lightbar

| # | Steps | Expected |
|---|---|---|
| 5.1 | Connect a DS4 / DualSense in each mode | lightbar takes the mode's colour ~1.5 s after connecting |
| 5.2 | PS4 mode, host sets a colour | lightbar follows the host |
| 5.3 | Change mode by combo with the DS4 connected | new mode's colour |

## 6. Combos

| # | Steps | Expected |
|---|---|---|
| 6.1 | Each mode combo in the [mode table](../README.md#output-modes) for the hybrid's modes | mode changes (Pico restarts in it); ESP32 LED shows the new blink code |
| 6.2 | Mode combo with an extra button held (e.g. Start + LB + Up + A) | the longest matching combo wins (STEAM here) |
| 6.3 | Web app: turn one mode's combo off, try it | nothing happens; Web App mode combo always works |
| 6.4 | Lone Joy-Con: Start (+) with the stick held at the edge | acts as the D-pad for the combo |
| 6.5 | Start + Select 3 s (L3 + R3 on an OUYA pad) | disconnects (a Joy-Con is asked to sleep), ESP32 restarts, PC sees a neutral pad |
| 6.6 | Start + Select pressed within 3.5 s of connecting | ignored |
| 6.7 | Start + L3 3 s | one short rumble; search stops (LED solid) until a pad disconnects |

## 7. Joy-Con pairs and the search for controllers

| # | Steps | Expected |
|---|---|---|
| 7.1 | Pair the left Joy-Con, then the right | they merge into one pad; LED solid once merged |
| 7.2 | Lone Joy-Con for over a minute (default search times) | full search for 60 s (fast blink), then reduced (slow blink); the other half still pairs |
| 7.3 | Web app: full search 0 s, reduced 0 s | no search once a pad is connected |
| 7.4 | DS4 connected, second DS4 tries to pair | not accepted (one output) |
| 7.5 | DS4 connected 2 min | diagnostics: about 250 reports/s, no long gaps (the search no longer competes with it) |

## 8. Idle turn-off

| # | Steps | Expected |
|---|---|---|
| 8.1 | Web app: idle time 1 min; leave the DS4 on the table | turns off after ~1 min |
| 8.2 | Same, keep tilting it slowly without pressing anything | stays on (motion counts as use) |
| 8.3 | Same, with the host rumbling it on the table | its own shaking does not keep it on |
| 8.4 | Joy-Con pair idle | both halves turn off (asked to sleep) |

## 9. Diagnostics (Web App mode report)

| # | Steps | Expected |
|---|---|---|
| 9.1 | Play a while with a DS4, switch to Web App mode, save the report | the controller (name, IDs, Classic), its input rate and gaps, battery, events (ready, search changes) |
| 9.2 | Disconnect / idle turn-off / Start + L3 during the session | each shows as an event |

## 10. BLE keepalives (if the controllers are available)

| # | Steps | Expected |
|---|---|---|
| 10.1 | Xbox Series controller (BLE), no rumble, 5 min | stays connected |
| 10.2 | Switch 2 Pro (BLE), 5 min | stays connected (or: does not pair on the ESP32's BLE 4.2 — note it) |

## Report

For each failure: case number, controller, mode, host, what happened, the diagnostics report, and
the ESP32 log build's serial output if it was connected.
