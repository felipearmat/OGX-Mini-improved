# Bench tests

Automated tests against a real adapter on the computer's USB port, in **D-Input/Debug** mode
(Start + RB + Left). They cover what unit tests can't: the firmware running on the board, flash
writes, restarts, the RP2040 + ESP32 link. Run them before taking a build to the field.

| File | Checks |
|---|---|
| `test_adapter.py` | answers (version, capabilities, report without a watchdog restart or crash); settings applied live saved 12 times in a row without a restart or a hang, the ESP32 still talking; colour table round trip and preview; a restart comes back on USB (slow) |
| `test_hybrid.py` | the ESP32 on I2C without write errors; leaving the log monitor returns to D-Input/Debug (slow). Skipped on other boards |

```sh
pip install -r Tools/bench/requirements.txt       # pyserial, pytest (picotool for --solo / --hybrid)
python -m pytest Tools/bench -v                     # the firmware on the adapter now
python -m pytest Tools/bench -v -m "not slow"       # without the restarts
Tools/bench/run_bench.py --solo PI_PICO2W.uf2 --hybrid ESP32_BLUEPAD32_I2C_PICO2.uf2 --esp32 esp32-build/
```

| `capture_pad.py` | not a test: records a controller's raw input reports (through the adapter's `RAW_CAPTURE`, or a PC hidraw node) through a script of labelled actions, for the fixture tests ([fixtures](../../Firmware/RP2040/tests/fixtures/README.md)) |
| `hd_rumble.py` | not a test: HD Rumble patterns written to the adapter in Switch mode (hidraw, Linux) for a Joy-Con / Pro Controller to play ([HD_Rumble.md](../../Firmware/RP2040/docs/HD_Rumble.md)) |
| `ab_link.py` | not a test: compares firmware builds' radio link on the hybrid. `run` / `summary`: rounds in D-Input/Debug with the controllers still. `soak` / `soak-summary`: hours in STEAM mode with PC rumble, figures from the flash log; it waits for `<out>/go-<NAME>` because the adapter cannot tell whether both Joy-Cons connected, and for the combos it prints |
| `test_logger.py` | `logger.py` against the firmware's virtual adapter on a pseudo-terminal (no board): kept log and first report recorded, each line once across runs, dropped bytes reported, old files cleaned up |

## Logger

`logger.py` keeps everything the adapter says while you work with it, so a problem can be looked
at afterwards:

```sh
python Tools/bench/logger.py --dir ~/ogxm-logs        # Ctrl+C to stop; --echo prints as well
```

One text file per day (`YYYY-MM-DD.log`): `[usb]` the adapter appearing and leaving USB (mode,
port, how long it was away, the computer's USB tree when it does not come back), `[rp]` / `[esp]`
the adapter's kept log (its diagnostics events and the hybrid's ESP32 output, `GET_LOG` every 10
s), `[diag]` a line when the diagnostics report shows another boot, a crash or other
controllers, with the whole report saved in `reports/`. Files older than 14 days, then the oldest
beyond 200 MB, are deleted (`--keep-days`, `--max-mb`).

With the adapter's "Keep the log in flash" option on (on by default in our own builds,
`OGXM_DEV_DEFAULTS`), what it logged away from the computer (playing on a console) is read from
its flash once it is back, after a `[flash]` line; what was already read from its RAM is left out.

It reads the port only in D-Input/Debug, only while no other program has it open (the web app
keeps it for its whole session) and only for a moment, under an flock that the bench tests and
`Tools/hybrid` take too. In another output mode there is no serial port: the `[usb]` lines go on,
and the kept log (it survives the mode change, not unplugging) is read once the adapter is back.

`run_bench.py` flashes each build (the Pico through its own USB port, the hybrid's ESP32 through
the Pico's bridge) and runs the tests on it. The tests restore the settings and colours they
change. Close the web app first: it holds the serial port.

Packet IDs and the settings layout come from `Firmware/common/contract/webapp_contract.json`, the
file the web app is tested against too, so the bench, the firmware and the web app share one
definition.

Next (when the hardware is on the bench): a USB controller simulated by a second RP2040 on the
adapter's USB host port (input mapping, mode combos, rumble, every output mode read back), a
Bluetooth controller simulated by a Linux board (pairing, reconnection, report rate), and the
GameCube / N64 console side.
