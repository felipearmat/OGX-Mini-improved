#!/usr/bin/env python3
"""Reads the adapter's debug probe point (Firmware/RP2040/src/Board/DebugProbe.h) and writes one
timestamped log file per session, with the figures the adapter leaves to the PC.

Wiring: the probe pin (GP4 by default) to the RX of a 3.3 V USB-UART adapter, or to TX0 of an
ESP32 board held in reset (EN to GND, or this tool holds RTS), and GND to GND. The activity log
must be on (web app, Diagnostics).

  probe.py                                     the one USB-UART reader found; files in ./logs
  probe.py --port /dev/ttyUSB0                 live: events and a line per second; files in --dir
  probe.py --port COM5 --dir logs --quiet      only the files (as the service runs it)

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
from serial.tools import list_ports

BAUD = 921600
SYNC = 0xA5
HELLO, TEXT, REPORT, USB_SENT, LATENCY, RUMBLE, COST, PAD_CLOCK, PAD_COMMAND, LINK = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
# The controllers' own clocks (diag::PadClock): nominal µs per count, counter size, and the longest
# pause the counter still bridges (half a wrap); the real rate is fitted per session.
CLOCKS = {1: ("Switch timer", 5000.0, 1 << 8), 2: ("DS4", 16 / 3, 1 << 16), 3: ("DualSense", 1 / 3, 1 << 32)}
LINK_LATE_MS = 20  # a report this much later than the controller's best case is logged
GAP_MS = 50  # a controller report later than this is a gap (as the adapter's "no input" event)


def say(text, **kw):
    try:
        print(text, flush=True, **kw)
    except OSError:  # no terminal (a service whose output went away): the files are what counts
        pass


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


class PadClock:
    """One controller's delay to the adapter beyond its best case: arrival time against the clock
    the controller writes in each report. The controller's clock rate is fitted (least squares)
    over the fastest report of each second, so the line follows the best case and the rest is
    delay added between the controller and the adapter (radio, retransmissions, the stack)."""

    def __init__(self, kind):
        self.name, self.nominal, self.mod = CLOCKS[kind]
        self.reset()
        self.hist = []  # excess delays (ms) counted for the session, once the fit holds

    def reset(self):
        self.last_raw = None
        self.count = 0        # unwrapped controller clock, in counts
        self.bins = {}        # second of arrival -> (residual, count, arrival) of its fastest report
        self.fit = None       # (offset_us, us_per_count)
        self.start = None

    def add(self, t_us, raw):
        """Returns the excess delay in ms, or None while the fit is not settled yet."""
        if self.last_raw is not None:
            step = (raw - self.last_raw) % self.mod
            pause = t_us - self.last_t
            if pause > self.mod * self.nominal / 2 or step > self.mod // 2:
                self.reset()  # the counter may have wrapped unseen: start over
        if self.last_raw is not None:
            self.count += (raw - self.last_raw) % self.mod
        else:
            self.start = t_us
        self.last_raw, self.last_t = raw, t_us
        key = int(t_us // 1_000_000)
        resid = t_us - self.count * self.nominal
        best = self.bins.get(key)
        if best is None or resid < best[0]:
            self.bins[key] = (resid, self.count, t_us)
        if self.fit is None or t_us - self.start < 10_000_000:
            return None
        off, rate = self.fit
        return (t_us - off - rate * self.count) / 1000.0

    def refit(self):
        pts = [(c, a) for _, c, a in self.bins.values()]
        if len(pts) < 3:
            self.fit = None
            return
        n = len(pts)
        mc = sum(c for c, _ in pts) / n
        ma = sum(a for _, a in pts) / n
        var = sum((c - mc) ** 2 for c, _ in pts)
        rate = sum((c - mc) * (a - ma) for c, a in pts) / var if var else self.nominal
        if not 0.9 * self.nominal < rate < 1.1 * self.nominal:
            rate = self.nominal  # not this clock after all: keep the nominal rate
        off = min(a - rate * c for c, a in pts)  # the line through the fastest reports
        self.fit = (off, rate)


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
        self.log_cost = None  # the activity log's hooks: % of a core, longest call µs
        self.log_cost_max = [0.0, 0.0]
        self.linkinfo = {}  # slot -> {what: (a, b)} from the detailed link readings
        self.rssi = {}      # slot -> RSSI readings over the session
        self.clocks = {}  # slot -> PadClock
        self.link = {}    # slot -> excess delays (ms) this second
        self.commands = {}  # (slot, subcommand) -> times sent (µs)
        self.rtt = {}     # slot -> round trips (ms)
        self.second = None
        self.id = "?"
        self.drops = 0
        self.pending = []

    def out(self, line, show=True):
        if self.file:
            self.file.write(line + "\n")
            self.file.flush()
        if show and not self.quiet:
            try:
                print(line, flush=True)
            except OSError:  # the terminal went away: keep writing the files
                self.quiet = True

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
            say(f"-> {os.path.join(self.dir, name)}")

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
        if self.log_cost:
            self.out(f"# activity log hooks (their probe frames included): at most {self.log_cost_max[0]:.3f}% "
                     f"of a core in a second, longest call {self.log_cost_max[1]:.1f} us")
        for slot, xs in sorted(self.rssi.items()):
            self.out(f"# slot {slot} rssi: min {min(xs):+d}, median {sorted(xs)[len(xs) // 2]:+d}, "
                     f"max {max(xs):+d} ({len(xs)} readings)")
        for slot, clk in sorted(self.clocks.items()):
            h = sorted(clk.hist)
            if h:
                pick = lambda q: h[min(len(h) - 1, int(q * len(h)))]
                late = sum(1 for x in h if x >= 10)
                self.out(f"# slot {slot} link delay beyond the best case ({clk.name} clock, {len(h)} reports): "
                         f"median {pick(0.5):.1f} ms, 95% {pick(0.95):.1f} ms, 99% {pick(0.99):.1f} ms, "
                         f"max {h[-1]:.1f} ms, {late} reports 10 ms or more")
        for slot, xs in sorted(self.rtt.items()):
            m = sorted(xs)[len(xs) // 2]  # the median: the first replies of a setup wait far longer
            self.out(f"# slot {slot} subcommand round trip: min {min(xs):.1f} ms, median {m:.1f} ms, "
                     f"max {max(xs):.1f} ms, {len(xs)} answered")
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
        elif ftype == PAD_CLOCK and len(payload) >= 6 and payload[1] in CLOCKS:
            slot, kind = payload[0], payload[1]
            raw = struct.unpack_from("<I", payload, 2)[0]
            clk = self.clocks.get(slot)
            if clk is None or clk.name != CLOCKS[kind][0]:
                clk = self.clocks[slot] = PadClock(kind)
            if slot in self.down:
                clk.reset()
                return
            excess = clk.add(t_us, raw)
            if excess is not None:
                clk.hist.append(excess)
                self.link.setdefault(slot, []).append(excess)
                if excess >= LINK_LATE_MS:
                    self.out(f"{self.stamp(t_us)} slot {slot}: report {excess:.1f} ms later than the controller's best case")
        elif ftype == PAD_COMMAND and len(payload) >= 3:
            slot, sub, reply = payload[0], payload[1], payload[2]
            key = (slot, sub)
            if not reply:
                self.commands.setdefault(key, []).append(t_us)
            elif self.commands.get(key):
                sent = self.commands[key].pop(0)
                rtt = (t_us - sent) / 1000.0
                if rtt < 2000:
                    self.rtt.setdefault(slot, []).append(rtt)
                    self.out(f"{self.stamp(t_us)} slot {slot}: subcommand 0x{sub:02x} answered in {rtt:.1f} ms")
        elif ftype == COST and len(payload) >= 16:
            cycles, most, frames, hz = struct.unpack_from("<IIII", payload)
            if hz:
                us_s = cycles * 1e6 / hz
                self.cost = (us_s, us_s / 1e4, most * 1e6 / hz, frames)
                self.cost_max[0] = max(self.cost_max[0], self.cost[1])
                self.cost_max[1] = max(self.cost_max[1], self.cost[2])
                if len(payload) >= 24:
                    log_cycles, log_most = struct.unpack_from("<II", payload, 16)
                    self.log_cost = (log_cycles * 100 / hz, log_most * 1e6 / hz)
                    self.log_cost_max[0] = max(self.log_cost_max[0], self.log_cost[0])
                    self.log_cost_max[1] = max(self.log_cost_max[1], self.log_cost[1])
        elif ftype == LINK and len(payload) >= 6:
            slot, what = payload[0], payload[1]
            a, b = struct.unpack_from("<hH", payload, 2)
            self.linkinfo.setdefault(slot, {})[what] = (a, b)
            if what == 1:
                self.rssi.setdefault(slot, []).append(a)

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
            for slot, xs in sorted(self.link.items()):
                parts.append(f"slot {slot} link +{max(xs):.1f} ms max")
            self.link = {}
            for clk in self.clocks.values():
                clk.refit()
            for slot, info in sorted(self.linkinfo.items()):
                bits = []
                if 1 in info:
                    bits.append(f"rssi {info[1][0]:+d}")
                if 2 in info:
                    bits.append(f"channels {info[2][0]}/{info[2][1]}")
                if 3 in info:
                    bits.append(f"failed contacts {info[3][0] & 0xFFFF}")
                if 4 in info:
                    bits.append(f"tx {info[4][0]} dBm")
                if bits:
                    parts.append(f"slot {slot if slot != 0xFF else '?'} " + " ".join(bits))
            if self.lat:
                parts.append(f"latency avg {sum(self.lat) // len(self.lat)} max {max(self.lat)} us")
            if self.rumble_max:
                parts.append(f"rumble max {self.rumble_max}")
            if self.cost:
                parts.append(f"probe {self.cost[0]:.0f} us/s ({self.cost[1]:.3f}% of a core), "
                             f"longest frame {self.cost[2]:.1f} us")
            if self.log_cost:
                parts.append(f"log hooks {self.log_cost[0]:.3f}% of a core, longest {self.log_cost[1]:.1f} us")
            self.out(f"{self.stamp(t_us)} rate: " + ", ".join(parts), show=not self.quiet)
        self.usb = 0
        self.lat = []
        self.rumble_max = max(self.rumble)  # still on: still counts
        self.second = t_us


ADAPTER_VID = 0xCAFE  # the adapter's own USB port is never the reader
# USB-UART chips: FTDI, Silicon Labs CP210x, WCH CH340 / CH9102, Prolific
READER_VIDS = (0x0403, 0x10C4, 0x1A86, 0x067B)


def find_reader():
    """The port of the one USB-UART reader attached, or None (none, or more than one)."""
    found = [p for p in list_ports.comports() if p.vid in READER_VIDS]
    return found[0].device if len(found) == 1 else None


def reader_ports():
    return [f"{p.device} ({p.vid:04x}:{p.pid:04x} {p.product or ''})".strip()
            for p in list_ports.comports() if p.vid is not None and p.vid != ADAPTER_VID]


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
    ap.add_argument("--port", default="auto",
                    help="serial port of the USB-UART reader (e.g. /dev/ttyUSB0, COM5); auto (default): the one "
                         "FTDI / CP210x / CH340 / Prolific port attached")
    ap.add_argument("--dir", default="logs", help="where the session files go (default: logs)")
    ap.add_argument("--idle", type=float, default=5.0, help="seconds without data that end a session")
    ap.add_argument("--quiet", action="store_true", help="no output on the terminal")
    ap.add_argument("--seconds", type=float, default=0, help="stop after this long (0 = run until stopped)")
    args = ap.parse_args()

    dec = Decoder()
    ses = Session(args.dir, args.quiet)
    start = time.monotonic()
    port = None
    warned = False
    last_data = time.monotonic()
    try:
        while not args.seconds or time.monotonic() - start < args.seconds:
            if port is None:
                path = find_reader() if args.port == "auto" else args.port
                if path is None:
                    if not warned and not args.quiet:
                        say("waiting for one USB-UART reader (--port to pick one); serial ports now: "
                            + (", ".join(reader_ports()) or "none"))
                    warned = True
                    time.sleep(2)
                    continue
                try:
                    port = open_port(path)
                except serial.SerialException:
                    time.sleep(2)
                    continue
                if not args.quiet:
                    say(f"reading {path}")
                warned = False
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
            say(f"{dec.bad} bad frames skipped", file=sys.stderr)


if __name__ == "__main__":
    main()
