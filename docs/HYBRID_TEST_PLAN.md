# RP2040 + ESP32 hybrid — hardware test plan

What to check on hardware for the hybrid (`ESP32_BLUEPAD32_I2C` / `ESP32_BLUEPAD32_I2C_PICO2`)
now that the ESP32 runs the Pico W's Bluetooth code (`Firmware/common/Bluepad32`) and the I2C link
is at version 2 (`Firmware/common/I2C/Esp32Bridge.h`). Everything below builds and has unit tests
for its logic; what has been on hardware so far is under [Results](#results).

Mark each case **pass**, **fail** (with what happened) or **skip** (no controller / host for it).
Save a diagnostics report (web app, D-Input/Debug mode) after any failure.

## Setup

- **Boards:** Pi Pico 2 W running `ESP32_BLUEPAD32_I2C_PICO2` (or an RP2040 board running
  `ESP32_BLUEPAD32_I2C`) + an original ESP32 (WROOM / WROVER) running the companion firmware.
  Wiring: [hardware/README.md](../hardware/README.md#pi-pico--esp32) (GP18 → 21, GP19 → 22, GND,
  GP8 → EN, 2.2 kΩ pull-ups to 3V3; GP21 not connected). One USB cable to the Pico.
- **Wiring:** the final pinout, including GP28 (or GP16) → RX0, GP17 ← TX0 and GP20 → IO0, so flashing and
  logs go through the Pico; the USB host port (GP0 / GP1) for groups 12 and 13.
- **Firmware:** both sides from the same commit. The very first time, the ESP32 is flashed through
  its own USB port (or through a Pico that already runs this firmware: group 11), then the Pico.
  [Tools/hybrid/ogxm_hybrid.py](../Tools/hybrid/README.md) needs `pip install pyserial esptool`.
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
| 2.6 | Web app (USB, D-Input/Debug mode): remap A ↔ B, add a stick deadzone and curve, save; back to a game mode | remap, deadzone and curve applied (from the RP2040's profile) |
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
| 4.7 | Diagnostics report (D-Input/Debug mode) with a DS4 | battery level shown |

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
| 6.3 | Web app: turn one mode's combo off, try it | nothing happens; D-Input/Debug mode combo always works |
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

## 9. Diagnostics (D-Input/Debug mode report)

| # | Steps | Expected |
|---|---|---|
| 9.1 | Play a while with a DS4, switch to D-Input/Debug mode, save the report | the controller (name, IDs, Classic), its input rate and gaps, battery, events (ready, search changes) |
| 9.2 | Disconnect / idle turn-off / Start + L3 during the session | each shows as an event |

## 10. BLE keepalives (if the controllers are available)

| # | Steps | Expected |
|---|---|---|
| 10.1 | Xbox Series controller (BLE), no rumble, 5 min | stays connected |
| 10.2 | Switch 2 Pro (BLE), 5 min | stays connected (or: does not pair on the ESP32's BLE 4.2 — note it) |

## 11. Everything through the Pico's USB port

| # | Steps | Expected |
|---|---|---|
| 11.1 | D-Input/Debug mode; `ogxm_hybrid.py ports` | one port, product "OGX-Mini" |
| 11.2 | `ogxm_hybrid.py flash-esp32 <ESP32 build>` | bridge port "OGX-Mini ESP32 bridge" appears, esptool writes and verifies, the adapter restarts in its mode and the pad reconnects |
| 11.3 | `flash-esp32 --baud 115200` | same, slower (the default is 460800) |
| 11.4 | `ogxm_hybrid.py bootsel --uf2 <hybrid UF2>` | the Pico restarts with it; mode and profiles kept |
| 11.5 | Play 2 min with the ESP32 log build, combo to D-Input/Debug mode, `ogxm_hybrid.py monitor --log run.txt` | "OGX-Mini ESP32 monitor" port: the kept `[esp]` lines from the session (and `[rp]` lines with a Debug Pico), then live ESP32 output; Ctrl+C returns to the mode |
| 11.6 | Power-cycle, then `monitor` | the kept log is empty (RAM only), live output works |
| 11.7 | `ogxm_hybrid.py bootsel` on a Pico 2 W (`PI_PICO2W`) | works there too |

## 12. USB host port (GP0 / GP1)

| # | Steps | Expected |
|---|---|---|
| 12.1 | Plug a wired controller (Xbox 360, DS4 on USB, Switch Pro on USB) | it works on the output like on the Pico |
| 12.2 | Bluetooth DS4 connected, then plug a wired controller | the wired one takes the output; unplug it: the Bluetooth one takes it back |
| 12.3 | Rumble on the wired controller | works |

## 13. DualShock 3 pairing

| # | Steps | Expected |
|---|---|---|
| 13.1 | With the adapter on for 3 s or more, plug a DualShock 3 into the USB host port, then unplug it | it is now paired with the ESP32 |
| 13.2 | Press PS on the DS3 | it connects over Bluetooth to the ESP32 and works |

## 14. Link details in the diagnostics

| # | Steps | Expected |
|---|---|---|
| 14.1 | DS4 (Classic), play 1 min, report | RSSI, channels in use (of 79), failed contacts, transmit power, remote chip vendor / version, lost reports by its counter |
| 14.2 | A BLE pad (Xbox Series / 8BitDo in BLE mode) | LE interval / latency / timeout, channels (of 37), device information (manufacturer, model, firmware) |
| 14.3 | Move away with the DS4 until it drops | RSSI falls, lost reports rise, the disconnect shows as an event |
| 14.4 | Same distance test on a Pico 2 W (`PI_PICO2W`) | compare range with the ESP32 (external antenna if available) |

## 15. Web app over Bluetooth (ESP32)

| # | Steps | Expected |
|---|---|---|
| 15.1 | Connect the web app over Bluetooth | name, version, mode, active profile shown |
| 15.2 | Read and save a profile | the Pico restarts with it; the USB web app shows the same profile |
| 15.3 | Dongle options (search times, idle time, Joy-Con options): change and save | the Pico restarts with them; the ESP32 uses them (e.g. idle time 1 min turns the pad off) |
| 15.4 | Mouse + keyboard mapping: change and save | applied without a restart; reading it back shows the change (allow ~2 s) |
| 15.5 | Live input view | buttons, sticks, triggers move |

## 16. STEAM mode passthrough

| # | Steps | Expected |
|---|---|---|
| 16.1 | DualSense in STEAM mode, Steam's controller test | all buttons incl. mute, touchpad (cursor and click), sticks and triggers as the pad sends them |
| 16.2 | DS4 in STEAM mode | works as on the Pico W (synthesized report, raw sticks / triggers) |

## 17. Measurements, advertising, transmit power, options per hardware

| # | Steps | Expected |
|---|---|---|
| 17.1 | DS4 connected 1 min, then the report | `companion` (ESP32 firmware, uptime, I2C errors, queue drops); the controller has `esp32_reports_per_s` and `esp32_lost_reports_pct` next to the RP2040's figures |
| 17.2 | Compare the two | ESP32 rate ≈ DS4 rate and RP2040 rate lower → losses between the boards; both low → radio |
| 17.3 | With a DS4 connected, look for the adapter from the web app's "Connect via Bluetooth" | not listed (advertising off, every slot in use); disconnect the DS4: listed again within seconds |
| 17.4 | Lone Joy-Con for over a minute | listed during the full search; still listed, slower to appear, in the reduced search |
| 17.5 | Web app: Higher Bluetooth transmit power on, save | applied without a restart; report: transmit power high, `tx_power_max_dbm` 9 |
| 17.6 | Same option on a Pico 2 W (`PI_PICO2W`) | greyed out, note "Not supported on this adapter's hardware" on hover |
| 17.7 | MAC per controller on, PS4 mode, DS4 connected | the host sees the DS4's address (Steam keeps its settings per controller) |
| 17.8 | Turn off controllers before mode change on, change mode by combo | the DS4 turns off, then the mode changes |
| 17.9 | Play 1 min, change mode, report | `previous_session.input_to_output_latency` has samples |
| 17.10 | Repeat the 56% loss test (DS4, 1-2 m) | compare with the first test: lost %, late %, largest gap |

## Results

**2026-10-20** — Pi Pico 2 W `ESP32_BLUEPAD32_I2C_PICO2` (Debug, I2C on GP26 / GP27, IO0 on GP22)
+ ESP32 38-pin DevKit (WROOM), jumper wires, 2.2 kΩ pull-ups; Joy-Con pair; Linux host (Bazzite).

| # | Result | Notes |
|---|---|---|
| 6.5 | pass | Joy-Cons asked to sleep (link closed, reason 0x13), ESP32 restarts; a button press brings them back (they call the adapter) |
| 7.1 | pass | the pair merges into one pad, search off with the slot in use |
| 9.1 | pass (Joy-Cons) | controllers, input timing, events, `companion` with the ESP32's I2C errors |
| 11.2 | pass | many times; the ESP32 enters its bootloader through GP22, no BOOT button |
| 11.4 | pass | |
| 14.1 | pass (Joy-Cons) | remote chip (`Broadcom v7 s2105`), poll interval granted (`poll 5000 us`) |
| D-Input ZL / ZR | pass | after the fix: buttons 7 / 8 on evdev (CHANGELOG) |
| I2C link | pass, with errors | about 10 read errors per 30 s at 1 MHz on these jumpers, 1 per 30 s at 400 kHz: signal, see [TODO](TODO.md) |
| 1.3, 1.4 (DS4, 2026-10-21) | pass | DS4 off: the host keeps a neutral pad (no USB reconnect); back on with PS, no pairing |
| 2.1 (DS4, Switch, XInput, D-Input) | pass | every button by position, D-pad, both sticks full range (Switch right stick after the SPI fix), triggers to 254 in XInput (Bluepad32 widens 8-bit triggers by 4: accepted) |
| 3.1-3.4 (DS4, STEAM mode) | pass | strong = left grip, weak lighter; 6 s continuous; 5 of 5 150 ms pulses; host rumble waits about 6 s after the DS4 connects. Seen: rumble in regular pulses when it was already on as the DS4 reconnected (TODO) |
| 4.2 / 16.2 (DS4 touchpad, STEAM) | pass | one and two fingers, 21 clicks |
| 5.1-5.3 (DS4) | pass | mode colour on connect (STEAM lilac, Switch red), the host's colour after (blue from hid-playstation) |
| 6.1 (STEAM, Switch, XInput, D-Input combos) | pass | |
| 4.7 (DS4) | pass | battery 0 % when flat (the pad then turned off), 39 % charging |
| 4.7 (DS4 charging state, 2026-10-22) | pass | from the pad's status byte: on the charger 100 % `full`, off the cable 95 % `discharging` (level 9) |
| 7.5 / 17.1 / 17.2 (DS4) | pass | about 770 reports/s on the RP2040 (after the per-second counter fix), 884/s at the ESP32's radio, 0 % lost, largest gap 18 ms |
| 9.2, 14.1 (DS4) | pass | link, key and ready events; MediaTek BT 4.1 chip, RSSI in the golden range, 76 / 79 channels, 0 failed contacts |
| I2C with a DS4 | pass, with errors | 9-21 packets rejected per minute (corrupted, wrong length) at about 770 reports/s: CRC needed (TODO) |
| 8.1-8.3 (DS4, idle time 1 min, 2026-10-22) | pass | off after a minute still; slow tilting kept it on (off 104 s after connecting, a minute after it was put down); a continuous rumble on the table did not (off about 60 s after the last press) |
| 6.2, 6.3, 6.6, 6.7 (DS4, 2026-10-22) | pass | Start + LB + Up + A went to STEAM; with STEAM's combo turned off nothing happened; Start + Select ignored right after connecting, turned the pad off later; Start + L3: one short rumble, "search stopped (controller)" (logged twice: TODO) |
| 4.4 (pair IMU side, 2026-10-22) | pass | right chosen: only the right half moves the output (743 deg/s, left half nothing); left chosen: the reverse (1106 deg/s / nothing). Orientation part not yet |
| 8.4 (Joy-Con pair idle, 2026-10-22) | pass | both halves off together, each asked to sleep (reason 0x13); took about 97 s with a 1 min idle time (motion noise on the table may restart the minute: watch) |
| 7.2 (lone Joy-Con, 2026-10-22) | pass | search reduced to 10 % exactly 60 s after the Joy-Con was ready; LED from fast to slow blink |
| 4.5 (lone Joy-Con held sideways) | pass | stick up / right from the player's view read as up (Y 33) / right (X 232) |
| 17.3, 17.4 (BLE advertising, 2026-10-22) | pass | lone Joy-Con (reduced search): "OGX-Mini" found by the PC's Bluetooth; pair merged (slot full): not found in 30 s |
| 17.5 (higher transmit power) | pass | applied live (no restart): "TX power: high (+9 dBm)" then back, report `high_tx_power` follows; links already open stayed at +3 dBm (power control raises it only when needed) |
| 17.8 (pads off before a mode change) | pass | Joy-Cons disconnected before the combo's mode change (dev default on) |
| 11.3 (`flash-esp32 --baud 115200`) | pass | 38 s for the app (137.9 kbit/s), hashes verified; the I2C link back right after |
| 11.5 (`monitor`) | pass | the kept log (ESP32 boot and RP2040 lines in order), then live output; back to the mode at the end |
| 17.9 (latency in the stored session) | pass | STEAM session of 447 s: 312953 samples, average 510 us, max 1.5 ms |
| Motion, STEAM mode (2026-10-21) | pass | Joy-Con pair: +0.99 g on Y lying flat, a 360 degree turn integrates to about 351 degrees (bias removed), Steam's calibration page and Eden follow it |
| Motion, STEAM mode, DS4 (2026-10-21) | pass | +0.98 g on Y lying flat, a 360 degree turn integrates to about 349 degrees, Steam's gyro as mouse right |
| Motion reference, DS4 on USB (2026-10-22) | pass | the same DS4 straight on the PC's USB (its own calibration 0x02, nothing of ours): a hand turn integrates to 348 degrees on Y, as through the adapter (349): the adapter's motion path keeps the pad's scale |
| 17.7 (DS4, PS4 mode, 2026-10-22) | pass after a fix | the host kept the dongle's address (no USB reconnect on the hybrid); after the fix one reconnect as the DS4 connects, Linux `HID_UNIQ` = the DS4's address |
| 4.1 (DS4, PS4 mode, 2026-10-22) | pass | +0.98 g on Y still; a hand turn integrates to 349.8 degrees on Y, as the DS4 on USB (348) |
| 2.2 (DS4 touchpad, PS4 mode, 2026-10-22) | pass | 9 clicks in 3 groups of 3, each counted once; touches seen |
| 11.6 (2026-10-22) | pass, with a note | live output fine; the kept log still held the previous session after the cable was out 9 s (SRAM kept its contents; TODO) |
| I2C link with controllers (2026-10-21) | pass, with errors | 58 min up, no silence, no restart, no mode change; about 1 read in 4 still cut short (signal) |
| Joy-Con turned off with its sync button | not reconnecting (expected) | the pad calls no host back after a sync tap (same with Windows and macOS); a sync press pairs it again. Start + Select keeps the button-press reconnection (README) |

## Report

For each failure: case number, controller, mode, host, what happened, the diagnostics report, and
the ESP32 log build's serial output if it was connected.
