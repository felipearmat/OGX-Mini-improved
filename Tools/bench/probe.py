#!/usr/bin/env python3
"""Reads the adapter's debug probe point (Firmware/RP2040/src/Board/DebugProbe.h) and writes one
timestamped log file per session, with the figures the adapter leaves to the PC.

Wiring: the probe pin (GP4 by default) to the RX of a 3.3 V USB-UART adapter, or to TX0 of an
ESP32 board held in reset (EN to GND, or this tool holds RTS), and GND to GND. The activity log
must be on (web app, Diagnostics).

  probe.py --port /dev/ttyUSB0                 live: events and a line per second; files in --dir
  probe.py --port COM5 --dir logs --quiet      only the files

A session file starts when the adapter's header (once a second) appears and ends when the stream
stops for --idle seconds or the adapter restarts (its clock goes back).
"""
import argparse
import datetime as dt
import os
import re
import struct
import sys
import time

import serial

BAUD = 921600
SYNC = 0xA5
HELLO, TEXT, REPORT, USB_SENT, LATENCY, RUMBLE, COST = 1, 2, 3, 4, 5, 6, 7
GAP_MS = 50  # a controller report later than this is a gap (as the adapter's "no input" event)


class Decoder:
    """Splits the byte stream into frames; skips bytes until a frame's check passes."""

    def __init__(self):
        self.buf = bytearray()
        self.bad = 0

    def feed(self, data):
        self.buf += data
        out = []
        b = self.buf
        i = 0
        while True:
            j = b.find(SYNC, i)
            if j < 0:
                i = len(b)
                break
            if len(b) - j < 8:
                i = j
                break
            n = b[j + 2]
            if len(b) - j < 8 + n:
                i = j
                break
            if sum(b[j + 1:j + 8 + n]) & 0xFF:
                self.bad += 1
                i = j + 1
                continue
            t = struct.unpack_from("<I", b, j + 4)[0]
            out.append((b[j + 1], t, bytes(b[j + 8:j + 8 + n])))
            i = j + 8 + n
        del b[:i]
        return out


class Session:
    def __init__(self, directory, quiet):
        self.dir = directory
        self.quiet = quiet
        self.file = None
        self.reset()

    def reset(self):
        self.last_t = None
        self.t_hi = 0  # the adapter's µs clock wraps every 71 min
        self.text = {}
        self.slots = {}  # slot -> [count this second, last µs, max gap µs, gaps]
        self.down = set()  # slots between the adapter's "slot N gone" and "slot N ready" (no gaps then)
        self.usb = 0
        self.lat = []  # input-to-output waits this second (µs)
        self.lat_all = [0, 0, 0]  # count, sum, max over the session
        self.rumble = (0, 0)
        self.rumble_max = 0
        self.cost = None  # the adapter's last COST frame: µs/s, % of one core, max µs
        self.cost_max = [0.0, 0.0]  # session: most % in a second, longest frame µs
        self.second = None
        self.id = "?"
        self.drops = 0
        self.pending = []

    def out(self, line, show=True):
        if self.file:
            self.file.write(line + "\n")
            self.file.flush()
        if show and not self.quiet:
            print(line, flush=True)

    def open(self, ident):
        self.close()
        os.makedirs(self.dir, exist_ok=True)
        stem = dt.datetime.now().strftime("probe-%Y-%m-%d_%H%M%S")
        name, n = stem + ".log", 1
        while os.path.exists(os.path.join(self.dir, name)):  # a restart within the same second
            n += 1
            name = f"{stem}-{n}.log"
        self.file = open(os.path.join(self.dir, name), "x", encoding="utf-8")
        self.out(f"# {dt.datetime.now().isoformat(timespec='seconds')} probe session, adapter {ident}")
        if not self.quiet:
            print(f"-> {os.path.join(self.dir, name)}", flush=True)

    def close(self):
        if self.file:
            self.summary()
            self.out(f"# {dt.datetime.now().isoformat(timespec='seconds')} end")
            self.file.close()
            self.file = None
        self.reset()

    def summary(self):
        if self.lat_all[0]:
            self.out(f"# latency (input to output driver): avg {self.lat_all[1] // self.lat_all[0]} us, "
                     f"max {self.lat_all[2]} us, {self.lat_all[0]} samples")
        if self.cost:
            self.out(f"# probe cost: at most {self.cost_max[0]:.3f}% of a core in a second, "
                     f"longest frame {self.cost_max[1]:.1f} us")
        for slot, s in sorted(self.slots.items()):
            self.out(f"# slot {slot}: max gap {s[2] / 1000:.1f} ms, {s[3]} gaps over {GAP_MS} ms")

    def stamp(self, t_us):
        return f"{t_us / 1e6:10.3f}"

    def frame(self, ftype, t, payload):
        if self.last_t is not None and t < self.last_t:
            if self.last_t - t > 0x80000000:
                self.t_hi += 1 << 32  # the clock wrapped
            else:
                self.out("# the adapter restarted")
                self.close()
        t_us = self.t_hi + t
        self.last_t = t
        if ftype == HELLO and len(payload) >= 12:
            frames, dropped, ring = struct.unpack_from("<III", payload)
            ident = payload[12:].decode("ascii", "replace")
            if self.file is None or ident != self.id:
                pending = [f for f in self.pending if f[1] <= t]  # open() starts afresh
                self.open(ident)
                self.id = ident
                # Just booted: the controllers connect again; no gaps until each is ready.
                if t < 5_000_000:
                    self.down = set(range(8))
                for f in pending:  # what came before this header, in order
                    self.last_t = None
                    self.frame(*f)
                self.last_t = t
            if dropped != self.drops:
                self.out(f"{self.stamp(t_us)} probe: {dropped - self.drops} frames dropped (ring {ring} bytes full)")
                self.drops = dropped
            self.per_second(t_us)
        elif self.file is None:
            self.pending.append((ftype, t, payload))  # before the first header: kept for the file
            del self.pending[:-4000]
        elif ftype == TEXT and payload:
            src = chr(payload[0])
            text = self.text.get(src, "") + payload[1:].decode("utf-8", "replace").replace("\r", "")
            *lines, rest = text.split("\n")
            self.text[src] = rest
            for line in lines:
                self.out(f"{self.stamp(t_us)} [{'rp' if src == 'r' else 'esp' if src == 'e' else src}] {line}")
                self.slot_event(line)
        elif ftype == REPORT and payload:
            s = self.slots.setdefault(payload[0], [0, None, 0, 0])
            s[0] += 1
            if s[1] is not None and payload[0] not in self.down:  # not while it connects again
                gap = t_us - s[1]
                s[2] = max(s[2], gap)
                if gap > GAP_MS * 1000:
                    s[3] += 1
                    self.out(f"{self.stamp(t_us)} slot {payload[0]}: report after {gap / 1000:.1f} ms")
            s[1] = t_us
        elif ftype == USB_SENT:
            self.usb += 1
        elif ftype == LATENCY and len(payload) >= 4:
            us = struct.unpack_from("<I", payload)[0]
            self.lat.append(us)
            self.lat_all[0] += 1
            self.lat_all[1] += us
            self.lat_all[2] = max(self.lat_all[2], us)
        elif ftype == RUMBLE and len(payload) >= 2:
            new = (payload[0], payload[1])
            if (new == (0, 0)) != (self.rumble == (0, 0)):  # start and stop as lines; strength per second
                self.out(f"{self.stamp(t_us)} rumble {'on' if new != (0, 0) else 'off'}"
                         + (f" {new[0]}/{new[1]}" if new != (0, 0) else ""))
            self.rumble = new
            self.rumble_max = max(self.rumble_max, *new)
        elif ftype == COST and len(payload) >= 16:
            cycles, most, frames, hz = struct.unpack_from("<IIII", payload)
            if hz:
                us_s = cycles * 1e6 / hz
                self.cost = (us_s, us_s / 1e4, most * 1e6 / hz, frames)
                self.cost_max[0] = max(self.cost_max[0], self.cost[1])
                self.cost_max[1] = max(self.cost_max[1], self.cost[2])

    def slot_event(self, line):
        m = re.search(r"\bslot (\d+) (ready|gone)\b", line)
        if not m:
            return
        slot = int(m.group(1))
        if m.group(2) == "ready":
            self.down.discard(slot)
            if slot in self.slots:
                self.slots[slot][1] = None
        else:
            self.down.add(slot)

    def per_second(self, t_us):
        if self.second is not None:
            parts = [f"usb {self.usb}/s"]
            for slot, s in sorted(self.slots.items()):
                parts.append(f"slot {slot} {s[0]}/s")
                s[0] = 0
            if self.lat:
                parts.append(f"latency avg {sum(self.lat) // len(self.lat)} max {max(self.lat)} us")
            if self.rumble_max:
                parts.append(f"rumble max {self.rumble_max}")
            if self.cost:
                parts.append(f"probe {self.cost[0]:.0f} us/s ({self.cost[1]:.3f}% of a core), "
                             f"longest frame {self.cost[2]:.1f} us")
            self.out(f"{self.stamp(t_us)} rate: " + ", ".join(parts), show=not self.quiet)
        self.usb = 0
        self.lat = []
        self.rumble_max = max(self.rumble)  # still on: still counts
        self.second = t_us


def open_port(path):
    port = serial.Serial()
    port.port = path
    port.baudrate = BAUD
    port.timeout = 0.2
    # An ESP32 board as the reader: RTS on and DTR off holds it in reset (its EN low), so its TX0
    # does not fight the probe pin. Harmless on a plain USB-UART adapter.
    port.rts = True
    port.dtr = False
    port.open()
    return port


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="serial port of the USB-UART reader (e.g. /dev/ttyUSB0, COM5)")
    ap.add_argument("--dir", default="logs", help="where the session files go (default: logs)")
    ap.add_argument("--idle", type=float, default=5.0, help="seconds without data that end a session")
    ap.add_argument("--quiet", action="store_true", help="no output on the terminal")
    ap.add_argument("--seconds", type=float, default=0, help="stop after this long (0 = run until stopped)")
    args = ap.parse_args()

    dec = Decoder()
    ses = Session(args.dir, args.quiet)
    start = time.monotonic()
    port = None
    last_data = time.monotonic()
    try:
        while not args.seconds or time.monotonic() - start < args.seconds:
            if port is None:
                try:
                    port = open_port(args.port)
                except serial.SerialException:
                    time.sleep(2)
                    continue
            try:
                data = port.read(65536)
            except serial.SerialException:
                port = None
                ses.close()
                continue
            now = time.monotonic()
            if data:
                last_data = now
                for f in dec.feed(data):
                    ses.frame(*f)
            elif ses.file and now - last_data > args.idle:
                ses.out("# no data from the adapter")
                ses.close()
    except KeyboardInterrupt:
        pass
    finally:
        ses.close()
        if dec.bad and not args.quiet:
            print(f"{dec.bad} bad frames skipped", file=sys.stderr)


if __name__ == "__main__":
    main()
