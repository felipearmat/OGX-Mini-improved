# Debug mode

How to record what the adapter does while you play, to look into lag, stutter, disconnects or
rumble problems, in any output mode and also with the adapter on a console. There is no separate
debug firmware: the standard firmware records when you turn its log options on, so a debug
session behaves like normal use.

## The log options

Set in the [web app](Web_App_Guide.md), *Diagnostics* panel. The log in flash needs the activity
log; both are off by default, and then the adapter counts and logs nothing.

| Option | What it does | Cost |
|---|---|---|
| **Activity log** | Counters and an event log in RAM (the Diagnostics panel, the log report), each Bluetooth controller's link readings (signal, channels in use, failed contacts, transmit power, its chip: one radio query every 0.7 s), and the **probe point**: the same events plus the time and the controller's own clock of every controller report, the time of every report the host reads, each input-to-output wait and each rumble change, streamed on one pin | Measured on a Pico 2 W in STEAM mode with a Joy-Con pair, with and without rumble: no change in the controllers' timing (on the PC and on the probe) against the log off; 0.78% of one core, the probe 0.32% of it |
| **Keep the log in flash** | The event log and the session summaries kept in flash too, so they outlive unplugging the adapter | A flash page write every 30 s while playing (about 1 ms with the other core parked): may delay a report now and then |

Until 2026-11 the link readings were a third option, "Detailed controller diagnostics"; measured
without a cost, they are now part of the activity log (version 8 firmware still has the option).

With a probe reader attached (below), the activity log alone is enough for a full record: the PC
keeps everything. Keep the log in flash only when no PC can be near the adapter.

## The probe point

While the activity log is on, the adapter sends its records on a UART pin (GP4 on the Pico W,
Pico 2 W and the RP2040 + ESP32 hybrid), as small binary frames sent by DMA: the adapter does not
format text or wait for the wire. Nothing attached costs the same. Each session also records the
probe's own cost, measured by the adapter.

Hardware: a 3.3 V USB-UART adapter (FT232, CP2102, CH340, a few dollars), or a spare ESP32 board
held in reset. Wiring: [hardware/README.md](../../../hardware/README.md#debug-probe-point). The
adapter stays on its own USB port (PC or console); the reader goes to a PC.

## Commands to the adapter (automated tests)

With a second wire, from the reader's TX to GP5 ([hardware/README.md](../../../hardware/README.md#debug-probe-point)),
the PC can drive the adapter in any output mode, which is otherwise only possible in
D-Input/Debug through the web app:

```sh
python Tools/bench/probe.py --send O 1 1     # log options live: level (0 none, 1 activity, 2 flash), detailed
python Tools/bench/probe.py --send M 16      # output mode (16 STEAM, 6 D-Input/Debug): stored, the adapter restarts
```

The log options set this way are not saved: a restart brings back the saved ones. The channel
listens once the probe's UART is up, that is once the activity log was on since the adapter
started. A command is 8 bytes with a check ("OGX!", command, two values, check), anything else is
ignored, and the adapter answers each one on the probe point. Tests can then run whole sessions by
themselves, for example the log options compared with both Joy-Cons connected in STEAM mode.

## Recording on a PC

Once, from a copy of this repository, install the reader as a service of your user. It starts at
login, keeps running (also in SteamOS / Bazzite Game Mode), finds the USB-UART reader by itself
and writes one file per session:

| System | Install | Remove |
|---|---|---|
| Linux (systemd), SteamOS, Bazzite | `Tools/debug/install.sh` | `Tools/debug/install.sh --uninstall` |
| macOS | `Tools/debug/install.sh` | `Tools/debug/install.sh --uninstall` |
| Windows 10 / 11 | `powershell -ExecutionPolicy Bypass -File Tools\debug\install.ps1` | the same with `-Uninstall` |

Both need Python 3.8 or later; they put pyserial in a Python environment of their own. Options:
`--port` / `-Port` to name the reader's port (when more than one is attached), `--dir` / `-Dir` for
the files (default `ogx-probe-logs` in your home folder). On Linux your user may need to be in the
serial ports' group (`dialout` or `uucp`); the script says so.

Without installing anything, the same in a terminal:

```sh
pip install pyserial
python Tools/bench/probe.py                      # Ctrl+C to stop; --port COM5 to pick a port
```

## Reading a session file

`probe-YYYY-MM-DD_HHMMSS.log`, one per session: a restart of the adapter, a mode change or a pause
of a few seconds starts a new one. A shortened example:

```text
# 2026-11-03T16:15:16 probe session, adapter improved-v1.5.0-7-g6f289622 6f289622+ PI_PICO2W
     7.138 [rp] 7.137 slot 1 ready 057e:2007 Classic: Joy-Con (R)
    15.523 rate: usb 252/s, slot 0 67/s, slot 1 67/s, latency avg 210 max 430 us, probe 230 us/s (0.023% of a core), longest frame 4.1 us
    20.004 rumble on 0/128
    42.166 [rp] 42.166 mode change to DINPUT_DEBUG
# latency (input to output driver): avg 222 us, max 450 us, 9120 samples
# slot 1: max gap 45.0 ms, 0 gaps over 50 ms
```

- The first column is the time since the adapter started, in seconds.
- `[rp]` lines are the adapter's events (`[esp]` the hybrid's ESP32), as the web app's log shows them.
- `rate` once a second: reports the host read (`usb`), each controller's reports, the wait from a
  controller's input to the output driver, the strongest rumble, the probe's cost.
- `slot N: report after X ms`: a controller report later than 50 ms (not while it connects).
- `slot N: report X ms later than the controller's best case`: the controller's own clock (DS4,
  DualSense, Joy-Con, Pro Controller) says the report was held back between the controller and the
  adapter (radio retransmissions, interference, the Bluetooth stack). The best case of the session
  is the zero, so this is the delay the link added, not the whole delay.
- `subcommand 0x.. answered in X ms` (Joy-Con, Pro Controller): a command the adapter sent (at
  connection, LEDs, rumble setup) and the controller's answer, the round trip over the radio.
- The end of the file: the session's latency, each controller's link delay (median, 95 %, 99 %,
  max) and longest gap, the subcommand round trips, the probe's cost.

## Without a probe

- **D-Input/Debug mode**: the web app's *Diagnostics* panel and **Generate log report** read the
  activity log over USB ([main README](../../../README.md#diagnostics-and-log-report)).
- **On a console, no PC nearby**: turn on *Keep the log in flash*, play, then bring the adapter to
  a PC in D-Input/Debug mode and read it with the web app or `Tools/bench/logger.py --once`.
