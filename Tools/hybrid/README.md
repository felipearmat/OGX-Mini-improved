# Tools/hybrid — maintain the adapter through its own USB port

`ogxm_hybrid.py` does every maintenance step of an OGX-Mini adapter over the USB port it already
plugs into, so the RP2040 + ESP32 hybrid needs no other cable: the ESP32 is flashed and its log read
through the RP2040 / RP2350 ([hardware/README.md](../../hardware/README.md#pi-pico--esp32) has the
wiring).

```
pip install pyserial esptool
python3 ogxm_hybrid.py ports
```

The adapter must be in **Web App mode** (hold Start + LB + RB about 3 s on a controller, or pick it
in the web app): it then shows up as a USB serial port the tool talks to (the web app's `SYSTEM`
command, 0x75).

| Command | What it does |
|---|---|
| `bootsel [--uf2 FILE]` | Restarts the RP2040 / RP2350 into its USB bootloader; with `--uf2`, loads the file (picotool if installed, else a copy to the RPI-RP2 / RP2350 drive). Any board, not only the hybrid. |
| `flash-esp32 BUILD_DIR` | Hybrid: restarts the RP2040 as a USB serial to the ESP32 with the ESP32 held in its ROM bootloader (GP20 → IO0, GP8 → EN), runs esptool through it (`--before no_reset`), then sends the adapter back to its mode. `BUILD_DIR`: the ESP-IDF build folder (`flasher_args.json` gives the offsets) or a folder with `bootloader.bin`, `partition-table.bin` and the app `.bin`. Flashing runs at 460800 baud (about 25 s for the companion, `--baud` to change it); a failed attempt is retried at 115200 after putting the ESP32 back in its bootloader. |
| `monitor [--log FILE] [--seconds N]` | Hybrid: restarts the RP2040 as a USB serial that sends the log kept in RAM from the last session (`[esp]` lines: the ESP32's serial output; `[rp]` lines: the RP2040's log in Debug builds), then the ESP32's live output. The ESP32 keeps running. Stops after `--seconds` or on Ctrl+C; however the tool ends (Ctrl+C, killed, terminal closed) it sends the adapter back to its mode. Run again while the adapter is still in monitor mode (for example after a power loss of the computer), it reads the log and ends it the same way; unplugging the adapter also ends it. |
| `reboot` | Restarts the adapter. |

**First setup of a hybrid:** a blank Pico starts in its USB bootloader by itself — copy the hybrid
UF2 to it. Then, with the adapter in Web App mode, `flash-esp32` the companion firmware.

**Updates:** `bootsel --uf2` for the Pico side, `flash-esp32` for the ESP32 side, both through the
adapter's USB port.

**Logs:** play, switch to Web App mode with the combo (the kept log survives that restart), then
`monitor`. The ESP32 "logs" build prints Bluepad32's log; the release build only its boot messages
and the events also shown in the web app's diagnostics report.
