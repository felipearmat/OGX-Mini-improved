# Bench tests

Automated tests against a real adapter on the computer's USB port, in **D-Input/Debug** mode
(Start + LB + RB). They cover what unit tests can't: the firmware running on the board, flash
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
