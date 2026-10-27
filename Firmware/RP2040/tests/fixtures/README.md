# Controller input fixtures

Recordings of what real controllers send, for end-to-end tests: replayed through Bluepad32's
parsers, our mapping and each output mode's driver on the PC, their output is compared with the
golden files kept next to them (the replay test comes next; see `docs/TODO.md`).

## Recording one

```sh
python3 Tools/bench/capture_pad.py --list             # the script of actions
python3 Tools/bench/capture_pad.py ds4                # through the adapter (preferred)
python3 Tools/bench/capture_pad.py ds4 --hidraw /dev/hidraw7   # straight from the PC
```

- **Through the adapter** (`source: adapter`): a Pico W / Pico 2 W build in D-Input/Debug mode,
  the controller connected to it over Bluetooth. The reports are the exact bytes Bluepad32 parses
  there, the setup replies included (Switch `0x21`). Capability `RAW_CAPTURE`, packets
  `RAW_CAPTURE` / `RAW_REPORT` (`Firmware/common/UserSettings/WebAppProtocol.h`).
- **From the PC** (`source: pc-hidraw`): any pad the PC sees (USB or its Bluetooth). Over USB these
  are the bytes the adapter's USB host port gets; over Bluetooth the report format is the same but
  the PC may set the pad up differently (Linux enables both Joy-Cons' IMU, for instance).

Name the file after the pad and its mode, e.g. `ds4`, `joycon_pair`, `nova_lite_switch`,
`nova_lite_android` (`<name>.jsonl.gz`). After each step: Enter for the next, `r` to do it again, `b` to go back to the step before (also before starting a step);
`--redo lb,rb,l3` records only those steps again into the existing file.

What is kept: the first 3 s (a pad connecting there leaves its setup replies) and the motion steps
at full rate (the gyro is integrated), the other steps at about 60 reports a second per
controller, nothing between steps; gzip. A DS4 run: about 250 KB (16 MB as recorded).
`--compact FILE` rewrites an older full recording that way.

## Format

gzip-compressed JSON lines. The first is the header: `format` (`ogxm-fixture`), `version` (1), `name`, `source`,
`captured` (date), `firmware` / `board` (adapter recordings), `vid`, `pid`, `controller_type`
(Bluepad32's), `steps` (the script's labels), `dropped` (reports the adapter could not send),
`redone` (steps recorded again later).
Then one line per report: `t` (ms), `slot` (Bluetooth slot; the two halves of a Joy-Con pair have
their own), `label` (the action being done, `null` between actions and during setup), `r` (the
report in hex, its first byte the report ID).

## Other sources

- `bentiss-hid-devices/`: recordings from [bentiss/hid-devices](https://github.com/bentiss/hid-devices)
  (Benjamin Tissoires), converted. Only `ds3_usb.jsonl` (a DualShock 3 on USB) is a game
  controller there: about 3 s, the pad at rest (stick noise only), so it exercises the wired DS3
  path, not the buttons. Origin and author kept in the header.
- Synthetic reports (pads we do not have: DualSense, Switch Pro, Xbox Series BLE) are built from
  the public formats (Linux `hid-playstation`, dekuNukem's Switch notes) and marked
  `source: synthetic`; they test our chain, not a real pad's quirks.
