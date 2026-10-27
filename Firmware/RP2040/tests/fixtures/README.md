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

Name the file after the pad and its mode, e.g. `ds4.jsonl`, `joycon_pair.jsonl`,
`nova_lite_switch.jsonl`, `nova_lite_android.jsonl`.

## Format

JSON lines. The first is the header: `format` (`ogxm-fixture`), `version` (1), `name`, `source`,
`captured` (date), `firmware` / `board` (adapter recordings), `vid`, `pid`, `controller_type`
(Bluepad32's), `steps` (the script's labels), `dropped` (reports the adapter could not send).
Then one line per report: `t` (ms), `slot` (Bluetooth slot; the two halves of a Joy-Con pair have
their own), `label` (the action being done, `null` between actions and during setup), `r` (the
report in hex, its first byte the report ID).

## Other sources

- `bentiss-hid-devices/`: recordings from [bentiss/hid-devices](https://github.com/bentiss/hid-devices)
  (Benjamin Tissoires), mostly older USB pads (DualShock 3...), for the USB host drivers. Kept as
  they were published, with their origin.
- Synthetic reports (pads we do not have: DualSense, Switch Pro, Xbox Series BLE) are built from
  the public formats (Linux `hid-playstation`, dekuNukem's Switch notes) and marked
  `source: synthetic`; they test our chain, not a real pad's quirks.
