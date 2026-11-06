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
  (macOS kept a stale BOOTSEL device on its port), so that one may be a separate problem. 2026-11-05, YD-RP2040
  hybrid on Linux, 120 mode changes XInput / D-Input through the probe's command pipe: back on USB
  every time, in 3.5-4.0 s.
- **Web app: "The device has been lost" on connect until the dongle was replugged** (2026-10-28,
  hybrid dev-opt1 at 72ea233, flatpak Chrome on Linux). The web app had connected earlier that
  day; it failed after the bench logger and a Python script had used the port. Meanwhile the
  adapter never left USB (udev quiet, the same device number, uptime unbroken) and Python could
  still open the port the way Chrome does (9600 baud, DTR / RTS) and talk to it; restarting
  Chrome did not help, replugging did. Watch for it: if it comes back, take `chrome://device-log`
  before replugging, and note what used the port before.
- ~~**Settings: a rewrite that a reset cannot cut**~~ — done 2026-11-05: NVSTool journals every sector rewrite in two sectors taken from the top of the flash log (CHANGELOG); a cut at any point keeps the old sector or is finished at the next boot (the log says so). Not yet cut on hardware: a reset in the middle of a write needs the power pulled at the right moment.
- ~~**Hybrid: the ESP32 hears the RP2040's first answer only about 10 s after a restart**~~ — gone:
  2026-11-05, YD-RP2040 hybrid, 122 boots (mode changes through the probe): the settings reached
  the ESP32 504-1257 ms after its start (median 756), 0-4 read errors each, no wrong reply, the
  profiles every time. Most likely the 400 kHz reads (dff4b53) and the cut-reply fix.
- ~~**Hybrid: I2C link checksum**~~ — done 2026-11-05: every ESP32 write carries a CRC-8 (`Esp32Bridge::Trailer`, CHANGELOG); `i2c_slave.check_failed` in the report and an event every 10 s with drops show what the bus corrupts.
- ~~**Hybrid: about 0.6 % of the ESP32's writes arrived with a wrong length under traffic**~~ — 2026-11-05, YD-RP2040 with a Joy-Con pair: 52 of 8186 writes in 30 s (58 of 7100 with 1 MHz reads), none failing the CRC. The rejected lengths came in pairs, 34 and 1: the Pico SDK's I2C slave interrupt handles a stop before the byte that came with it. Fixed the same day (CHANGELOG): 1 of 15 827 in 60 s after, the one write the ESP32 itself saw fail. To report to the Pico SDK (pico_i2c_slave finishes before draining the RX FIFO).
- **Fixture tests (end-to-end from real input)** — capture is in (`RAW_CAPTURE`,
  `Tools/bench/capture_pad.py`). Recorded and checked step by step (2026-10-27): `ds4`,
  `joycon_pair` (right half's IMU only, the pair-IMU default). Still to record (Pico W build,
  D-Input/Debug, web app closed):
  - Joy-Con setup: start the tool, then connect the pair (`joycon_pair_setup --steps idle`), so the
    Switch `0x21` setup replies (SPI stick calibration, firmware) are in a file; the main
    recording was made with the pads already connected.
  - GameSir Nova Lite, one file per mode: Switch (`nova_lite_switch`, also start before
    connecting), Android (`nova_lite_android`), iOS / Xbox-like if it has one, and wired USB
    (XInput, Switch) with `--hidraw` from the PC.
  - Optional: a lone Joy-Con L and R (`joycon_left`, `joycon_right`, sideways), the pair with the
    left half's IMU (Adapter Option), the DS4 on USB with `--hidraw` (wired path).
  Replay test `test_fixtures` in (2026-10-27): `ds4` and `joycon_pair` through PS4 / STEAM /
  D-Input / Switch / XInput, PS3 / Xbox OG / Wii U (2026-10-28), every other mode (2026-10-29,
  c30b1c8: it found the Xbox OG remote's L3 and the PS1/PS2 right stick Y, both fixed). The Joy-Cons' stick calibration there comes from the recording's own
  range (the setup recording above will give the real one); then: the Nova Lite files once
  recorded. Synthetic DualSense and Switch Pro in (2026-10-29, ae64ba8: they found Wii mode's
  stick fully up sent as fully down); an Xbox pad still to do (its HID report descriptor from a
  real pad). Fixes not checked on a console yet: the PS1/PS2 right stick (a PS2 game), the Xbox
  OG remote's L3 / R3 (the DVD player), the Wii stick fully up (a wired pad in Wii mode).
  To check on hardware: the roll sign. Rolling onto the right side gives the opposite roll in
  PS4 mode with the DS4 (-1369) and the Joy-Con pair (+1873) recordings, while yaw and pitch
  agree. Either one recording rolled the other way or the Switch pads' motion path flips roll;
  roll each pad right in PS4 mode in a gyro tester (or Steam's) and compare. A DS4 stick pushed fully up or
  left reaches 1, not 0, in PS4 / D-Input (Bluepad32 centres it on 127: `(v - 127) * 4`), a
  one-step loss to look at in the Bluepad32 re-sync. The DS4 calibration feature report is not
  in the recordings yet. Spec: `.ignore/spec-input-fixtures.md`.
- **HD Rumble (Switch mode), next steps** — phase 1 (Pico W) passed the pattern test with a
  Joy-Con pair (2026-10-27). Still to try: a game / Eden with the direct Pro Controller driver, a
  lone Joy-Con, a Pro Controller if we get one, and the lost / late input reports with HD Rumble
  on (two links for a pair; the diagnostics are read in D-Input/Debug, so play in Switch mode,
  then switch and read the kept session). Phase 2 (hybrid, `GET_HD_RUMBLE`) passed the same
  patterns (2026-10-27). Missing: the link figures during a Switch-mode session (about 125 extra
  transfers a second): the ESP32's counters restart with the mode change, so keep them in the
  session summary (or log them as an event) before measuring.
- **Pico SDK 2.3.1 (since 2026-10-27; released in v1.5.0 by the user's decision 2026-10-29)** — the fork
  moved from 2.1.0 (with four patches) to 2.3.1. Still to check on hardware: a Pico W, the PIO USB
  host port, Wii mode, a BLE pad. Done 2026-10-27 on the bench hybrid (RP2040 side): DS4 and the Joy-Con pair in the web
  app, STEAM mode in Steam (touchpad as mouse), mode combos, disconnect combo, ESP32 flashing and
  restarts (settings 0.5 s later), no crash. **Every bug found from now on: check whether the bump causes it** (does it happen on
  v1.4.1, the last 2.1.0 build?). Hardware pass before offering it upstream: Pico 2 W and Pico W
  over Bluetooth (DS4, Joy-Con pair, a BLE pad; connect / reconnect, rumble, idle turn-off, mode
  combos, disconnect combo, Wii mode), a wired pad on the USB host port (PIO USB), the hybrid's
  RP2040 side (I2C link, ESP32 flashing and log bridge), settings saved to flash, the web app,
  a long session (over 71 min: timer wrap). Then the upstream PR (UPSTREAM, Features).
  Done 2026-10-28 evening on a Pico 2 W (CYW43, dev build fe3bfa0, read from its flash log):
  the Joy-Con pair in STEAM mode over five boots (about 5 slot-hours of play), a DS4 too; idle
  turn-off of both halves after 15 min, USB suspend / resume with the PC, three sessions past
  71 min (74, 86, 87 min: timer wrap) with no crash; input-to-USB latency 0.2 ms average, under
  5 ms max. Input gaps (60-150 ms) 78 an hour per Joy-Con against 147 an hour on the same board
  with v1.2.0 (SDK 2.1.0, 2026-10-20), both varying 14-350 an hour between sessions: no sign the
  bump made them worse. One event to watch: 30 min into a session both halves dropped on the
  supervision timeout (reason 0x08, 0.9 s apart) after a quiet minute, the right one alone again
  14 s after reconnecting (its link was clean: 0 % lost, 46 ms max gap); then both ran 74 min.
  3 such drops in 5 slot-hours against 2 in 19 on 2.1.0: too few to tell. The user walked to the
  kitchen with the Joy-Cons in hand about then: out of range, not the bump. Another session ended
  by the adapter turning both halves off (reason 0x13) with no idle event: most likely the
  disconnect combo, which the Pico W did not log; it does now ("slot N: off combo", as the ESP32),
  and every controller and session leaves a summary line in the kept log (the flash log had only
  the last two summaries, so these sessions' figures were lost). The user's play sessions stop
  the PC logger (game mode), so the flash log is the source for them.
- **Pico W: Pico SDK timer backports (6473d8c) with an Xbox Series pad over BLE** — the case #146
  saw (connect rumble until a button press). Done with a DS4 on a Pico 2 W 2026-10-26; the stall
  needs a pad that sends nothing while idle. Asked in upstream #147 (UPSTREAM item 0).
- **Hybrid: I2C reads at 1 MHz (found 2026-10-26)** — most read errors were the RP2040's replies
  longer than its 16-byte TX FIFO stopping after their first byte at 1 MHz (85-99 % of settings
  replies); the ESP32 now reads at 400 kHz and writes at 1 MHz (`dff4b53`). Still about 1 settings
  read in 20 cut with a DS4 connected. Why exactly the first byte (the ESP32's master ends the read
  about 45 us in) is not known; check on the perfboard build whether 1 MHz reads work there before
  raising `CONFIG_I2C_READ_BAUDRATE`. The entries below predate it. 2026-11-05: the cut at the first or 16th byte was the ESP32's bus timeout (10 bus cycles: 10 us at 1 MHz) while the RP2040 held SCL low to fill its FIFO; with the ESP32 waiting up to 1 ms, 1 MHz reads work: 2026-11-05 on the YD-RP2040, 30 s with a Joy-Con pair, no reply cut and no ESP32 read error at 1 MHz, the same as at 400 kHz. Kept at 400 kHz: the gain is about 0.5 ms on a 37-byte reply.
- **Hybrid: I2C read errors about 12 times higher with controllers since the evening of 2026-10-20**
  — about 4 per second with a Joy-Con pair (120 per 30 s), against 10.7 per 30 s that morning on
  the same jumpers; the same with and without the DMA receive (A/B), about 0.25 per second with no
  controller as before. Check the jumpers first (the board was handled for BOOTSEL around 18:08).
- **Hybrid: I2C read errors on the ESP32 grow with traffic** — 2026-10-20, Pico 2 W hybrid with I2C
  moved to GP26 / GP27 (jumper wires): `companion.i2c_read_errors` 11 at boot, then about 1 every
  5 s with no controller and 77 a few minutes after a Joy-Con pair connected; write errors 1-2,
  no queue drops, no lost reports. No earlier figure to compare (the bench test only checks write
  errors). Not the log kept in flash: with that option off the count kept rising, about 10 every
  30 s with the pair connected. The ESP32's I2C at 400 kHz (test build, same wires): 1.0 per 30 s
  instead of 10.7, 2 at boot instead of about 11, and the right Joy-Con's late reports 1-3 % instead
  of up to 16 %: the 1 MHz edges are too slow on these longer jumpers with 2.2 kΩ pull-ups (signal,
  not damaged pins). Expected to go away on the perfboard build (short SDA / SCL traces, layout in
  hardware/README): count `i2c_read_errors` there at 1 MHz before changing anything. Only if they
  stay: 1 kΩ pull-ups (or a second 2.2 kΩ in parallel), then a configurable ESP32 I2C rate.
- ~~**Debug builds: about 330 compiler warnings in our code**~~ — done 2026-11-06: 0 on every board (CHANGELOG; three real bugs among them). Was: (`-Wconversion` /
  `-Wsign-conversion`, implicit numeric conversions; about 1,900 more from the libraries).
  Release builds have none and CI keeps them at zero.
- **Hybrid: the Joy-Con (R) had to be paired again after a few mode changes and ESP32 flashes** — 2026-11-04 on the YD-RP2040 (the L reconnected by itself). Flashing the ESP32 does not write its NVS (the keys); see whether the R's key was dropped or the R lost its own.
- **Hybrid: the second Joy-Con's HD Rumble report waits for the radio** — 2026-11-04, after the output order fix: the R half waited on 956 of about 1050 reports (the L on 37), none lost. The ESP32's VHCI transport takes one packet at a time (`esp_vhci_host_check_send_available`) and each report goes to the L half first. Not felt so far; to measure the wait (time from queued to sent) or send the halves' reports alternately if it shows.
- **YD-RP2040 probe: commands from the PC got through 3 times in 10** — 2026-11-04, the probe board with the brownout ESP32 (FT232R): the log stream to the PC was fine but only 3 of 10 option commands were acknowledged. Solved by swapping the probe board: 10 of 10. The old board's RX0 path is unreliable; do not use it as a probe.
- **Bluepad32 v5.0 re-sync (`bp32-resync-2`): long gaps in some sessions** — 2026-11-05/06, YD-RP2040 hybrid, the second Joy-Con pair at 100 %, Switch mode, 3 phases of 150 s (quiet / HD Rumble / quiet) per round, log in flash on and off (`~/git/ogxm-build/ab/resync-2026-11-05`, `bench-scripts/ab_compare.py`). Master (A1-A3) 0-11 gaps over 50 ms per round; the re-sync 1-273 (B1 37, B3 273: one link silent 60-77 ms every 1.28 s, the radio side, at the ESP32's own arrival; the I2C hop normal). The log in flash is not the cause (A3 with it: 0 gaps). Not the scan: inquiry off and no LE reports during the gaps, page scan 11.25 ms every 1.28 s and inquiry scan off in both builds, unchanged all session (LinkDiag now reads them). The split is who opened the links: every round where the ESP32 paged both pads was clean on both builds (A3, C1, B5-B8: 0-6), the bad ones had a Joy-Con paging the ESP32 ("link request"): A1 / A2 11, B1 37, B3 273 (B2 1 the exception). Lead: the ESP32 left slave on that link (a scatternet), worse with the new BTstack. Since 2026-11-06 LinkDiag asks our role 3 s after each Classic link is up and asks for master where we are slave (both branches); not yet seen acting (no pad-started link since). A round with the re-sync and the old BTstack (C1) was clean but was ESP32-paged, so it proves nothing. Next: the user's play session on the re-sync with the probe; a pad-started link shows "up as slave: asking to be master" and the role change. Also seen: often the Joy-Cons' button press is not heard (minutes, or a sync needed) on both builds, the old reconnection item.
- **Hybrid: Joy-Con link delay far above the Pico W's** — 2026-11-04, YD-RP2040, Joy-Con pair in Switch mode, 2.5 min (about 12 000 reports a half): beyond the pad's best case, L median 8.3 ms, 95 % 15.8, 99 % 22.1, max 40.7; R median 13.3, 95 % 25.6, 99 % 33.6, max 60.0, 16 gaps over 50 ms (the Pico 2 W's long session: median 0.1-0.2 ms, 95 % under 5 ms). The probe's clock frame is recorded when the RP2040 gets the report, so this is the ESP32's radio plus the I2C hop: **Split 2026-11-05** (the I2C trailer's age, Switch mode, 3 min a phase): it is the radio. ESP32 to RP2040 (queue + I2C) median 1.5 ms (L) / 2.3 ms (R), 99 % under 3.3 ms; the delay at the ESP32's own arrival is the whole of it (quiet: R median 13.0 ms, 95 % 21-24; L 7.6-15.4, 95 % 18-34, max 132; with HD Rumble the L half 95 % 45.8 ms). The ESP32's links stay active with a 5 ms QoS poll (`link_diag::set_poll_latency_us(5000)`, against 90 ms gaps seen in sniff once), while the Pico W's go to sniff; the Joy-Cons then send about 89 reports a second instead of 67, so the pad's own queue may be what waits. A/B the same day, 3 min quiet / HD Rumble / quiet each: with no QoS the links stayed active (no sniff asked by the pads) and got worse, median 23-33 ms, 95 % 53-69 ms; sniff at 15 ms asked by the ESP32 (`link_diag::set_sniff_slots(24)`, off by default) gave 57-61 reports a second and swung from near the Pico W's (median 0.0 ms) to far worse (95 % 344 ms, gaps up to 1.5 s, 358 over 50 ms in 3 min): the 5 ms QoS poll stays. The user feels the L half's HD Rumble lose reports and lag the R's (heartbeat): checked on the Pico 2 W the same day: the same L half lost and lagged there too, and a second Joy-Con pair played the patterns right on both sides (the user's check): **a fault of that Joy-Con (L)**, not the adapter. The hybrid's radio delay above stays to compare with the second pair. The ESP32 also takes about 89 reports a second from each Joy-Con against 67 on the Pico W.
- ~~**YD-RP2040: the onboard RGB LED breaks the I2C link to the ESP32**~~ — not the LED's power: 2026-11-05 the settings replies (37 bytes) were cut at byte 16 without RGB too (127 of 128 in 30 s, D-Input/Debug), whenever the RP2040's interrupt topped its 16-byte TX FIFO up later than the ESP32's bus timeout (10 bus cycles, 25 us at 400 kHz); the LED's work only made that more frequent. With the ESP32 waiting up to 1 ms (CHANGELOG): 0 of 135 cut without RGB, 0 of 128 with the RGB build. The user saw it go red in Switch mode; on by default since (CHANGELOG).
- **Soldered hybrid (YD-RP2040 + 38-pin ESP32, default wiring): test it** — built 2026-11-04 on the new default pins; checked so far: every wire (pin scan), the ESP32 flashed through the RP2040, the I2C link up (266 replies in the first 30 s, with 8 ESP32 read errors, 10 cut replies and 4 rejected packets, all from the boot; see whether they grow with a controller, as in the jumper item above). Still to do: the hybrid test plan with a controller on it (pairing, every mode, rumble, the combos), the ESP32's I2C errors under traffic against the jumper bench, the probe point on GP8 / GP9 (not wired yet), the RGB LED on GP23 (solder pad, `OGXM_YD_RP2040_RGB`).

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
- **PS1 / PS2 mode: Core1 outside the flash lockout (Standard board).** `psx_device_main()`
  (`PS1PS2/controller_simulator.c`), which runs on Core1, never calls
  `multicore_lockout_victim_init()`, so settings written while it runs (Start + Select held 2.5 s
  to change the input source, `store_input_source()`, no restart) fall back to "interrupts off on
  Core0" with Core1 executing from flash. Registering it is not enough: Core0's SEL interrupt
  resets and relaunches Core1 on every console transaction (`restart_pio_sm()`), which can run
  during the lockout handshake (both use the SIO FIFO) and brings Core1 back after a mode change
  has halted it (`usb::disconnect_all()`, then the writes run as if Core1 were stopped). Proposed:
  register in `psx_device_main()`; a "hold Core1" state in the PSX simulator (SEL restarts the PIO
  only); weak `flash_op_begin()` / `flash_op_end()` hooks called by NVSTool, used by the Standard
  board to hold it during writes and from the mode change on; `check_firmware_rules.py` extended to
  every function given to `multicore_launch_core1()`. Found reading the code (2026-10-19), not seen
  on hardware; changes the protocol's timing, so test on a PS1 / PS2 before relying on it. The Pico
  W boards run PS1 / PS2 from Core0 (poll mode) and are not affected. Not for upstream yet.
- **Web app over USB on Android.** Chrome on Android has no Web Serial; the web-serial-polyfill
  (Web Serial over WebUSB) would reach the D-Input/Debug serial port from the same web app code.

## Motion

- **DS4 motion calibration in Bluepad32 differs from Linux / DS4Windows** — compared 2026-10-22: (1) the accelerometer bias is computed but not subtracted (Linux: `raw - bias`); (2) the gyro pitch divisor uses `|minus + bias|` where Linux uses `minus - bias` (as for yaw / roll); (3) over Bluetooth it asks feature report 0x02 (the USB id) where Linux and DS4Windows ask 0x05, read in the Bluetooth field order. Measured on the test DS4 (v2, calibration 0x02 read over USB): biases -5 / 8 / 6, plus / minus ±8800-8867, speed 540 / 540 → gyro gain 62.4-62.7 (1/1024 deg/s per count) by Linux's formula; the same bytes in Bluepad32's order and pitch sign give 0.2-0.3 % off, and the uncalibrated default (62.5) is as close. Accelerometer biases 30-68 counts (4-8 mg). So all three are small on this pad (a pad with large biases or asymmetric ranges would show more), and none explains the 360 degree turn integrating to about 349 (3 %): the same pad on a USB cable, with its own calibration and nothing of ours in between, integrated a hand turn to 348 degrees, so that shortfall is the pad or the hand, not the adapter. Fix (1)-(3) by a Bluepad32 patch, also for upstream; low priority.
- **Linux evdev motion (hid-playstation) did not follow a turn** — 2026-10-21, STEAM mode, Joy-Con
  pair: integrating the "Motion Sensors" evdev device gave only the bias drift while the same
  reports' raw gyro (hidraw) integrated to the turn. Either the integration script or how
  hid-playstation reads the synthesized report (calibration, sensor timestamp); matters for Linux
  games reading motion without Steam. Check with `evtest` on the motion device.
- **PS3 mode: Joy-Con / Pro motion still in the Sixaxis-style frame** (gravity on Z), as PS3.cpp
  expects; a DS4 source arrives in the DS4 report frame (gravity on Y) and goes through the same
  conversion, so one of the two is wrong for a DS3 host. Check on a PS3 (or the DS3 test in
  Linux hid-sony) which frame the Sixaxis report wants.

- ~~**"search stopped (controller)" logged twice**~~ for one Start + L3 (hybrid, 2026-10-22): the ESP32 logged it next to ScanControl; once since 2026-11-06.

## Rumble

- **Switch mode: Joy-Con (L) reports held back one interval more often** — (2026-11-05: most likely that pad's own fault, see the hybrid link delay item: its HD Rumble also failed on the Pico 2 W, a second pair did not; recheck with the second pair) — 2026-11-03, Pico 2 W, probe point with the Joy-Con clock (median delay beyond the best case 0.1 ms, the timer stepping 3 counts of 5.000 ms every report, so the measure is sharp): in D-Input/Debug 1 % of the L half's reports came 15 ms late (one sniff interval) and the R half's fewer; in Switch mode with the host's rumble 5 % of the L half's (95th percentile 15.1 ms, max 60 ms) against the R half's 95th percentile 5 ms. The user felt some lag in Switch Pro mode before (2026-11) and not in STEAM. Lead: the host's rumble / HD Rumble output reports to the pad share its sniff slots with its input. Next: a Switch-mode session with and without rumble, then STEAM, compared on the probe's link delay.
- **Hybrid: DS4 rumble in regular pulses after a reconnection** — 2026-10-21, STEAM mode: host
  rumble already on while the DS4 reconnected came as a long buzz with short, evenly spaced
  pauses; the same rumble with the DS4 already connected was continuous. Suspect the lightbar
  update sharing the DS4 output report each feedback tick (mode colour vs host colour after the
  reconnection). Check with the full debug mode (what the host asks, what the ESP32 sends).
- **Hybrid: Joy-Cons not reconnecting for minutes after being turned off a few times** — **2026-11-04: the cause of most cases found and fixed.** A Joy-Con whose link drops by timeout (reason 0x08: the ESP32 reset with the RP2040, or flashed) lights its LEDs when pressed but does not page the adapter, and had to be paired again; one asked to sleep (0x13) pages it back. The hybrid's RP2040 reset the ESP32 on every restart it asked for itself (mode or settings changes from the web app or the probe, the web app's system commands): only a combo's mode change, decided on the ESP32, put the pads to sleep first. Since `SLEEP_PADS` / `sleep_bt_pads_before_restart` (CHANGELOG) every such restart closes the links with 0x13, and the pads came back by themselves after an RP2040 flash and a mode change. The other case, the ESP32's first boot after flashing it (no pad pages it, keys kept), is worked around by restarting it once more (CHANGELOG); why that boot takes no connection is open. Still open below: the sync-button tap case. 2026-10-20,
  Pico 2 W hybrid, Joy-Con pair already paired with the ESP32, turned off directly (link closed,
  reason 0x08) several times in a row: each time the last one left, the ESP32 restarted as
  designed (the white LED blink) and they came back by pressing a button, except twice: once only
  the left one came back (the right one only on the next try), and once neither did for 7.5 min
  (ESP32 restart at 477 s, both back at 926 s). Nothing was logged in between: no connection
  attempt, pairing failure or rejected key. The log now has those events (connection requests,
  failed connections, key requests, scan state; see CHANGELOG): repeat with it.
  **2026-10-20, with the log:** two different ways of turning them off. Start + Select (the
  adapter's sleep request, link closed with reason 0x13): a button press brings the pad back (a
  "link request" from it). A tap on the sync button (reason 0x08): a button press lights its LEDs
  but no connection request ever reaches the ESP32, also after the adapter freed the link (750 ms
  supervision timeout) and restarted. The pad's pairing info (SPI 0x2000) already held the ESP32
  as its host with a link key, the second entry was empty, and writing it again through the
  rail's pairing subcommand (0x01) changed nothing (tried, reverted). The same pads behave the same
  with Windows and macOS: a Joy-Con trait, not an adapter bug. Documented in the README (combos).
  The comebacks above after a sync tap were the ESP32 connecting to the pads (a "key request"
  without a "link request"), not the pads calling: still open is why that took 7.5 min once.
- **Sync tap: page the pad back (planned 2026-11-05, postponed by the user for the Bluepad32 re-sync)** — On the YD-RP2040 hybrid, a sync tap on the Joy-Con (R) closed its link with 0x08 and no link or key request ever came after a button press (the documented Joy-Con trait). Plan: a shared `Bluepad32/Repage` that, after a Classic pad's 0x08 drop (not 0x13 / 0x16), sends a remote name request (a page, no L2CAP) every 5 s for 2 min, and on an answer creates the device with its stored class and name and runs Bluepad32's connect; plus a Bluepad32 patch keeping the link key when an L2CAP open fails for other than security (today any failure drops it, so a page to an absent pad would cost a pairing). Both boards; test with sync taps on the hybrid. Also port to the re-sync: the four Bluepad32 patches made after it (`diag_output_hook`, `diag_output_queued`, `output_keep_order`, `switch_set_report_mode`).
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

- **Pico 2 W did not start after a flash** (2026-11-03, `debug-probe` build with the controller clock frames): `picotool load -x` from BOOTSEL wrote and verified the image, the board left the bootloader and then stayed off USB with its LED dark until the cable was unplugged and plugged again; that same image then booted normally. Once in about six flashes that day, with the probe's ESP32 reader on GP4 (held in reset). Not seen before; watch whether it comes back, and whether it follows the warm reboot (cyw43 not power-cycled?) rather than the image.
- **Log options compared, 2026-11-03** (Pico 2 W, STEAM mode, a Joy-Con pair resting, the options changed live through the probe's command channel; none / activity / + detailed / + flash, twice each, 110 s a round): on the PC the reports came alike in every round (median 16.0 ms, 99 % within 16.0-16.4 ms, at most 48 ms; the count over 25 ms 13-64 a round with no order by option); the probe's link delay beyond the best case had a median of 0.1 ms and a 95th percentile of 0.2 ms in every round, the input-to-output latency 214 µs on average in each; the activity log's hooks cost 0.78 % of one core with both halves reporting (the probe 0.32 % of it), the longest call 40-70 µs. Detailed diagnostics: no difference seen. Log in flash: the only repeated sign, both passes, one report of the L half 30 ms late (45 ms gap) against at most 16 ms in the other rounds. Then again with the PC's rumble (1.5 s every 3 s, force feedback), twice each, and a control round without: the same (the first round after the Joy-Cons connect is the worse one in every run, the log on or off). Decided: the detailed diagnostics merged into the activity log. Not yet: Switch mode (HD Rumble), an RP2040 board, the hybrid, longer rounds.
- ~~**Hybrid: HD Rumble still waits for the ESP32's 8 ms poll**~~ — the reports go over the UART since 2026-11-04 (`I2C/HdRumbleUart.h`, CHANGELOG), sent as they come; the I2C poll stays only for an RP2040 that predates it.
- **Settings reset to defaults once (2026-11-03, Pico 2 W)** — between 21:59 and 23:11 the saved Adapter Options (search times, Pico LED off, brightness 13, activity log) came back as the build defaults; the mode, the flash image and two mode changes through the probe's command channel kept them in a check afterwards, and the probe log shows no settings write. Since 2026-11-04 the table is read past a hole (the likely cause: reading stopped at the first erased entry) and the report's `settings` says how they were found; if it comes back, the report tells whether a hole was there, and what cut a sector rewrite is still to find.
- **One Bluetooth layer for the Pico W and the hybrid's ESP32** (decided 2026-11-04: every Bluetooth → USB feature and fix on every board that can run it, tested or not, and the other way round). Today `RP2040/src/Bluepad32/Bluepad32.cpp` (Pico W, ~1600 lines) and `ESP32/main/BTManager` (~1350) each have their own connect / ready / input / disconnect callbacks, feedback and LED timers; `Firmware/common/Bluepad32` holds the pieces. Steps, each built for every board and tested on the Pico 2 W and the YD-RP2040 hybrid: (1) battery on the host, LED alert, quiet-pad recovery — done 2026-11-04, hybrid not tested on hardware yet; (2) rumble: the hybrid's HD Rumble goes over the UART and the output reports keep their order since 2026-11-04 (the user: much better, close to the Pico 2 W); the feedback tick's keepalive and host rumble are one function since 2026-11-05 (`bt_pad::feedback_rumble`; it found the Pico W giving pads other than a Joy-Con pair the tick's own length, CHANGELOG), the Pico W not run with it yet; (3) checked 2026-11-04: the "MAC address per controller" option and the PS4 rumble grace are on both; the 8 s input-stall disconnect was dead code (its time was written, never read) and is removed; the DualSense adaptive-trigger toggle (touchpad click) removed on the user's decision, to come back as a macro action; (4) the search / pairing policy: done 2026-11-06 (`bt_pad::scan_pads` / `is_ble_hogp`, Start + L3's stop and rumble the same on both, CHANGELOG; the user's check on the hybrid; the Pico W not run with it yet), the CYW43 workarounds — BR/EDR inquiry and LE scan restarts, Xbox BLE, incomplete LE links after a USB suspend — stay Pico W only, as the radio's; (5) the probe's controller clock and subcommand frames from the ESP32: done 2026-11-04 (PacketIn v3 clock, `SLOT_COMMAND`); (6) the LED tick: one `status_led::Indicator` for the Pico W, the hybrid's RP2040 and the ESP32 since 2026-11-05 (the user's check on the YD-RP2040: boot code, mode change, web app preview, brightness; the Pico W not run with it yet); (7) the transmit power option stays hybrid only (checked 2026-11-06): the Pico W's CYW43439 has no documented way to raise its Bluetooth power; the vendor `Set_Transmit_Power` (OGF 0x3F, OCF 0x026) is reported only by forum posts with mixed results, and BTstack's maintainers put the ceiling in the chip's firmware / regulatory data. The web app hides it there (capability). **Unification: all steps done**; what each board keeps alone is its radio's (the Pico W's CYW43 workarounds, the ESP32's I2C / UART link). Other boards: the probe point is off where its pins were not checked (RP2040 / RP2350 boards without Bluetooth): pick a free pin per board.
- **Probe point: still to do** (2026-11-03, `debug-probe`) — run it on the RP2040 + ESP32 hybrid (bench build compiled) and on an RP2040 board (4 KB ring: check no frames dropped with four pads); check whether the activity log itself (the counters, not the probe) moves the input-to-output latency, Off vs On in the same session; the controller clock and subcommand frames are not forwarded by the hybrid's ESP32 (no-ops there); the ESP32's own reports reach the probe only as the RP2040 sees them; the logger service (`Tools/bench/logger.py`) still logs STEAM / other modes as "left USB" and fights the web app for the serial port; the install scripts were run on Linux only (Windows script parsed in PowerShell, macOS not run).
- ~~**Hybrid: the mode colour preview (and its Cancel) reaches the ESP32's LED late with a controller connected**~~ — fixed by the ESP32 reading at 400 kHz (dff4b53): the 37-byte GET_EXT reply now arrives whole. 2026-10-27, DS4 connected, the user's check: the LED follows the preview and Cancel almost at once. The planned GET_PAD colour bytes are dropped.
- **ESP32 LED options applied late with a controller connected** — 2026-10-23, Pico 2 W hybrid: turning the ESP32's external LED off or changing the brightness in the web app took effect much later (the user saw it off a while after). With no controller, the ESP32 logs the new options 1-2 s after the change ("LED options: ...", added for this). With a controller the link showed 42 % of the RP2040's replies cut and about 1800 packets rejected (16 % and 3 with none): the 37-byte GET_EXT reply, the longest on the link, rarely arrives whole then. Same jumper signal problem as the I2C corruption above; check on the soldered board, CRC planned. Since 2026-10-23 the LED options restart the adapter instead (CHANGELOG); the other live options sent to the ESP32 (search times, idle time, transmit power, combo mask, controller diagnostics) still take the slow path, accepted: they matter after a mode change, which restarts the ESP32.
- **ESP32 "status LED" option drives GPIO15** (upstream's pin, `CONFIG_LED_PIN_1`): the 38-pin DevKit has no LED there; its blue LED that flickers with activity is the serial activity LED (not ours). Decide a pin with the final board.
- ~~**The kept log (LogRing) survives a power cut**~~ — done 2026-11-06: the ring is dropped on a power-on reset (and an RP2350 brown-out), kept on watchdog and warm reboots (CHANGELOG); the user's check, the cable out, still to do. Was: 2026-10-22, Pico 2 W hybrid: with the USB cable out 9 s the ring in `.uninitialized_data` still passed its magic and bounds check and `monitor` showed the previous session (intact this time). Only the magic is checked, not the reset cause, so after a longer cut a half-decayed ring could show garbage as the "kept log". Low priority: drop the ring on a power-on / brown-out reset (RP2040 `vreg_and_chip_reset`, RP2350 `powman` chip reset bits), keep it on watchdog reboots.
- **DebugLite hangs at boot in Switch Pro mode** (seen after f259b0f, which shifts
  boot timing slightly). The Switch driver's USB-init logging (core 0, OGXM_LOG with its
  own mutex) collides with Bluepad32's banner/printf (core 1): both stop mid-line and the
  board freezes. Release builds are fine. Make logging safe across cores (one lock for
  every printf, or a ring buffer drained by one core).
