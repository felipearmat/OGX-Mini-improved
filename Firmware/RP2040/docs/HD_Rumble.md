# HD Rumble (Switch mode, Joy-Con and Switch Pro Controller)

In **Switch mode** a Joy-Con, a Joy-Con pair or a Switch Pro Controller connected over Bluetooth
plays the host's **HD Rumble** as it is: the frequencies, the strengths and the 5 ms steps the
game sends, as on a Switch. Every other controller and every other mode keeps the regular rumble
(a strength per motor). Nothing to set: the adapter tells them apart.

| Adapter mode | Controller | Rumble |
|---|---|---|
| Switch | Joy-Con L / R, Joy-Con pair, Switch Pro Controller | HD Rumble: the host's blocks, unchanged |
| Switch | any other (DS4, DualSense, Xbox...) | a strength per motor, decoded from the blocks |
| any other | any | a strength per motor (the host sends nothing else) |

Boards: **Pico W / Pico 2 W** (and RP2354), and the **RP2040 + ESP32** adapter: there the
RP2040 hands the reports to the ESP32 over I2C (`GET_HD_RUMBLE`, polled every 8 ms in Switch mode
while a Joy-Con / Pro Controller is connected), one at a time and resent until the ESP32
acknowledges it, so a read cut short on the link loses nothing. Update both chips.

## How it works

- The Switch Pro protocol has one kind of rumble: per side, a 4-byte motor block with one to
  three samples of a high-band and a low-band frequency and amplitude (dekuNukem's
  `rumble_data_table.md`). The adapter receives them in output reports `0x01` / `0x10`.
- The adapter forwards each report to the pad once and in order (a block can say "one step up
  from the current level", so dropping or repeating one would put the pad out of step), at the
  host's pace (about every 15 ms). A Joy-Con of a pair plays its own side's block, as on a
  Switch; a Pro Controller plays both.
- The host's neutral block (`00 01 40 40`) stops it. If the host stops sending while the pad
  rumbles, the adapter sends one neutral report after 100 ms.
- Hosts that only have two strengths encode them as blocks with fixed frequencies (Steam / SDL's
  regular rumble, Linux `hid-nintendo` force feedback): those blocks are played as they are too,
  like a Joy-Con on a Switch with that host. The full effect shows with hosts that send real HD
  Rumble: a Switch, an emulator's direct Pro Controller driver (Eden / yuzu forks), SDL3
  applications that send the raw `0x10` packet (`SDL_SendJoystickEffect`, e.g. RenHDrum).
- The "Joy-Con pair compatibility" Adapter Option (per side / both Joy-Cons) is for the other
  modes; in Switch mode the pair behaves as on a Switch.

## Trying it on a PC

`Tools/bench/hd_rumble.py` writes HD Rumble patterns to the adapter in Switch mode through
hidraw (Linux), as a Switch would: frequency sweeps of each band, strength steps, a heartbeat,
and `compare` (Steam / SDL's fixed-frequency rumble, then HD Rumble of the same strength).

```sh
python3 Tools/bench/hd_rumble.py list
python3 Tools/bench/hd_rumble.py sweep-high --side left
```

Source: `USBDevice/DeviceDriver/Switch/Switch.cpp` (the reports), `Gamepad/Gamepad.h` (queue),
`Firmware/common/Gamepad/HdRumbleFeed.h` (send / stop), `Bluepad32/Bluepad32.cpp`
(`hd_rumble_cb`), Bluepad32 patch `bluepad32_switch_raw_rumble.diff`.
