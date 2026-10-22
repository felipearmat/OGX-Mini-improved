#!/usr/bin/env python3
"""Background logger for the adapter on the bench: everything it says, kept in files.

  python Tools/bench/logger.py --dir ~/ogxm-logs

What it records, in one text file per day (<dir>/YYYY-MM-DD.log):
- [usb]  the adapter appearing and leaving USB (mode, port, how long it was gone) and, when it
         does not come back, what the computer's USB tree shows. Watched all the time.
- [rp] / [esp]  the adapter's kept log (Board/LogRing.h: its diagnostics events, the hybrid's
         ESP32 output), read with GET_LOG every --poll seconds; and what the adapter kept in
         flash (Board/FlashLog.h, the "keep the log in flash" option) that was not read from its
         RAM: what happened while it was away from the computer, after a "[flash]" line.
- [diag] a summary of the diagnostics report every --report seconds; the whole report is saved
         (<dir>/reports/) when it shows something new: another boot, a crash, other controllers.

The port is read only in D-Input/Debug, only while no other program has it open (the web app, the
bench tests, Tools/hybrid), and only for a moment, with an flock that Tools/bench and Tools/hybrid
take too. In another output mode there is no serial port: only the [usb] lines, and the kept log
and the previous session's report are read once the adapter is back in D-Input/Debug.

Files older than --keep-days, then the oldest beyond --max-mb, are deleted (checked hourly).
"""
import argparse
import datetime as dt
import json
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import adapter as A  # noqa: E402

import serial  # noqa: E402
from serial.tools import list_ports  # noqa: E402

VID = 0xCAFE
MODE_NAMES = {
    A.PRODUCT: "D-Input/Debug",
    A.MONITOR_PRODUCT: "ESP32 log monitor",
    "OGX-Mini ESP32 bridge": "ESP32 bridge",
}
GONE_SNAPSHOT_S = 20  # still gone after this: record the USB tree once


class Logger:
    def __init__(self, args):
        self.dir = pathlib.Path(args.dir).expanduser()
        (self.dir / "reports").mkdir(parents=True, exist_ok=True)
        self.args = args
        self.state_file = self.dir / "state.json"
        self.state = self._load_state()
        self.present = None        # (product, device, location) or None
        self.gone_since = None
        self.gone_snapshot = False
        self.next_log = 0.0
        self.next_report = 0.0
        self.next_cleanup = 0.0
        self.partial = b""         # an unfinished last line of the kept log
        self.partial_at = None     # (ring generation, position) where it ends: its rest starts there

    # ---- files ----

    def _load_state(self):
        try:
            return json.loads(self.state_file.read_text())
        except (OSError, ValueError):
            return {}

    def _save_state(self):
        tmp = self.state_file.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.state))
        tmp.replace(self.state_file)

    def write(self, source, text):
        now = dt.datetime.now()
        path = self.dir / f"{now:%Y-%m-%d}.log"
        stamp = f"{now:%H:%M:%S}.{now.microsecond // 1000:03d}"
        with path.open("a", encoding="utf-8") as out:
            for line in text.splitlines() or [""]:
                out.write(f"{stamp} [{source}] {line}\n" if source else f"{stamp} {line}\n")
        if self.args.echo:
            for line in text.splitlines():
                print(f"{stamp} [{source}] {line}" if source else f"{stamp} {line}", flush=True)

    def cleanup(self):
        files = [p for p in self.dir.rglob("*") if p.is_file() and p.name != "state.json"]
        cutoff = time.time() - self.args.keep_days * 86400
        removed = 0
        for p in files:
            if p.stat().st_mtime < cutoff:
                p.unlink(missing_ok=True)
                removed += 1
        files = sorted((p for p in files if p.exists()), key=lambda p: p.stat().st_mtime)
        total = sum(p.stat().st_size for p in files)
        limit = self.args.max_mb * 1024 * 1024
        while files and total > limit:
            p = files.pop(0)
            total -= p.stat().st_size
            p.unlink(missing_ok=True)
            removed += 1
        if removed:
            self.write("logger", f"cleanup: {removed} old file(s) removed")

    # ---- USB presence ----

    def ports(self):
        if self.args.port:  # a fixed port (tests: the virtual adapter on a pty)
            return [(A.PRODUCT, self.args.port, "fixed")] if os.path.exists(self.args.port) else []
        try:
            found = list_ports.comports()
        except (TypeError, ValueError, OSError):
            # A device that left while pyserial read its sysfs entry (the adapter switching to
            # BOOTSEL or another mode): its attributes read as missing. Look again next pass.
            return None
        return [((p.product or ""), p.device, p.location or "?") for p in found if p.vid == VID]

    def watch_usb(self):
        ports = self.ports()
        if ports is None:
            return
        now = time.monotonic()
        current = ports[0] if ports else None
        if current and (not self.present or current[0] != self.present[0] or current[1] != self.present[1]):
            mode = MODE_NAMES.get(current[0], current[0] or "?")
            gone = f" after {now - self.gone_since:.1f} s away" if self.gone_since else ""
            self.write("usb", f"adapter on USB: {mode} ({current[1]}, port {current[2]}){gone}")
            self.gone_since = None
            self.gone_snapshot = False
            self.next_log = self.next_report = 0.0  # read what it kept right away
        elif not current and self.present:
            self.write("usb", f"adapter left USB ({MODE_NAMES.get(self.present[0], self.present[0])})")
            self.gone_since = now
        elif not current and self.gone_since and not self.gone_snapshot and now - self.gone_since > GONE_SNAPSHOT_S:
            self.gone_snapshot = True
            self.write("usb", f"still gone after {GONE_SNAPSHOT_S} s; USB tree:\n{usb_tree()}")
        self.present = current

    # ---- the adapter ----

    def port_busy(self, device):
        """Another program has the port open (the web app keeps it for its whole session)."""
        if self.args.port or not shutil.which("lsof"):  # a fixed port: the test holds it open
            return False
        r = subprocess.run(["lsof", "-t", device], capture_output=True, text=True)
        return any(pid and int(pid) != os.getpid() for pid in r.stdout.split())

    def open_adapter(self):
        if not self.present or self.present[0] != A.PRODUCT:
            return None
        device = self.present[1]
        if self.port_busy(device):
            return None
        try:
            return A.Adapter(device)
        except (serial.SerialException, OSError):
            return None  # taken in the meantime (the flock)

    def read_flash(self, adapter):
        """Pages of the log kept in flash not read yet; text already read from the RAM ring (the
        same ring generation, up to the position read) is left out."""
        first = self.state.get("flash_next", 1)
        page = adapter.flash_log_page(first)
        if page is None:
            return None
        gen_now = page["ring_gen_now"]
        if self.state.get("ring_gen") != gen_now:
            if self.state.get("ring_gen") is not None:
                self.state["log_since"] = 0  # a new ring (power-on): read it from its start
            self.state["ring_gen"] = gen_now
        read_upto = self.state.setdefault("read_upto", {})
        shown_header = False
        count = 0
        while page and page["seq"] and count < 512:
            count += 1
            text = page["text"]
            gen = str(page["ring_gen"])
            start, end = page["ring_pos"], page["ring_pos"] + len(text)
            seen = read_upto.get(gen)
            if seen is not None:
                if seen >= end:
                    text = b""
                elif seen > start:
                    text = text[seen - start:]
            read_upto[gen] = max(read_upto.get(gen, 0), end)
            if page["ring_gen"] == gen_now:  # still in RAM too: the RAM read goes on after it
                self.state["log_since"] = max(self.state.get("log_since", 0), end)
            if text:
                pos = max(start, seen) if seen is not None else start
                if not shown_header:
                    self.continue_at(gen, pos)  # a line left open before: ended here unless this is its rest
                    self.write("flash", f"kept in flash, not read before (boot {page['boot']}, from page {page['seq']}):")
                    shown_header = True
                self.emit_at(gen, pos, text)
            self.state["flash_next"] = page["seq"] + 1
            page = adapter.flash_log_page(page["seq"] + 1)
        if shown_header and self.partial and (self.partial_at or ("",))[0] != str(gen_now):
            self.emit(b"\n")  # an older boot's last line: its rest is not in RAM
        self._save_state()
        return gen_now

    def read_log(self, adapter):
        since = self.state.get("log_since", 0)
        reads = 0
        while reads < 64:  # catch up, at most 128 KB per poll
            reads += 1
            got = adapter.log_since(since)
            if got is None:
                if reads == 1 and not self.state.get("no_get_log"):
                    self.state["no_get_log"] = True
                    self.write("logger", "this firmware has no GET_LOG: only reports and USB events")
                return
            self.state.pop("no_get_log", None)
            start, total, data = got
            if start != since and since:
                if (total - since) & 0xFFFFFFFF > 0x7FFFFFFF:  # since is ahead: a new ring
                    self.write("logger", "the adapter's kept log started over (power cycle)")
                    self.partial = b""
                    self.partial_at = None
                else:
                    self.write("logger", f"{(start - since) & 0xFFFFFFFF} bytes of the kept log were dropped "
                                         "before they could be read")
            self.emit_at(str(self.state.get("ring_gen")), start, data)
            since = (start + len(data)) & 0xFFFFFFFF
            if since == total or not data:
                break
        self.state["log_since"] = since
        if self.state.get("ring_gen") is not None:
            self.state.setdefault("read_upto", {})[str(self.state["ring_gen"])] = since
        self._save_state()

    def continue_at(self, gen, pos):
        """A line left unfinished goes in as it is unless the next text starts where it ended (in
        the same ring generation): the RAM ring and the pages kept in flash are one stream."""
        if self.partial and self.partial_at != (gen, pos):
            self.emit(b"\n")

    def emit_at(self, gen, pos, data):
        self.continue_at(gen, pos)
        self.emit(data)
        self.partial_at = (gen, (pos + len(data)) & 0xFFFFFFFF) if self.partial else None

    def emit(self, data):
        data = self.partial + data
        lines = data.split(b"\n")
        self.partial = lines.pop()  # unfinished, until the rest arrives
        for raw in lines:
            line = raw.decode("utf-8", "replace")
            if line.startswith("[") and "] " in line:
                source, text = line[1:].split("] ", 1)
                self.write(source, text)
            else:
                self.write("adapter", line)

    def read_report(self, adapter):
        report = adapter.diagnostics()
        if not report:
            return
        boot = report.get("uptime_ms", 0)
        last = self.state.get("last_report", {})
        controllers = sorted(f"{c.get('slot')}:{c.get('name') or c.get('type')}" for c in report.get("controllers", []))
        reasons = []
        if not last or boot < last.get("uptime_ms", 0):
            reasons.append("boot")
        if report.get("last_crash") and report.get("last_crash") != last.get("last_crash"):
            reasons.append("crash")
        if last and controllers != last.get("controllers"):
            reasons.append("controllers")
        companion = report.get("companion") or {}
        summary = (f"{report.get('board')} {report.get('firmware')} {report.get('output_mode')}, "
                   f"up {boot / 1000:.0f} s, last reset: {report.get('last_reset')}, "
                   f"controllers: {controllers or 'none'}")
        if companion:
            summary += (f", ESP32 up {companion.get('uptime_s')} s, I2C errors "
                        f"{companion.get('i2c_write_errors')}/{companion.get('i2c_read_errors')}")
        if reasons:
            now = dt.datetime.now()
            name = f"{now:%Y-%m-%d_%H-%M-%S}_{'-'.join(reasons)}.json"
            (self.dir / "reports" / name).write_text(json.dumps(report, indent=1))
            self.write("diag", f"{summary} (new: {', '.join(reasons)}; report saved as reports/{name})")
        elif self.args.echo_reports:
            self.write("diag", summary)
        self.state["last_report"] = {"uptime_ms": boot, "last_crash": report.get("last_crash"),
                                     "controllers": controllers}
        self._save_state()

    def poll_adapter(self):
        now = time.monotonic()
        want_log = now >= self.next_log
        want_report = now >= self.next_report
        if not (want_log or want_report):
            return
        adapter = self.open_adapter()
        if adapter is None:
            return
        try:
            if want_log:
                self.read_flash(adapter)  # first: it tells the ring generation
                self.read_log(adapter)
                self.next_log = now + self.args.poll
            if want_report:
                self.read_report(adapter)
                self.next_report = now + self.args.report
        except (serial.SerialException, OSError) as error:
            self.write("logger", f"read failed: {error}")
        finally:
            adapter.close()

    def run(self):
        self.write("logger", f"started (log every {self.args.poll} s, report every {self.args.report} s, "
                             f"keep {self.args.keep_days} days / {self.args.max_mb} MB)")
        try:
            while True:
                if time.monotonic() >= self.next_cleanup:
                    self.cleanup()
                    self.next_cleanup = time.monotonic() + 3600
                self.watch_usb()
                self.poll_adapter()
                if self.args.once:
                    return
                time.sleep(0.5)
        except KeyboardInterrupt:
            pass
        finally:
            if self.partial:
                self.emit(b"\n")
            self.write("logger", "stopped")


def usb_tree():
    if platform.system() == "Darwin":
        out = subprocess.run(["ioreg", "-p", "IOUSB", "-w0"], capture_output=True, text=True).stdout
        return "\n".join(line for line in out.splitlines() if "@" in line)
    if shutil.which("lsusb"):
        return subprocess.run(["lsusb", "-t"], capture_output=True, text=True).stdout
    return "(no USB tree tool)"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dir", default=os.environ.get("OGXM_LOG_DIR", "~/ogxm-logs"))
    parser.add_argument("--poll", type=float, default=10.0, help="seconds between kept-log reads")
    parser.add_argument("--report", type=float, default=60.0, help="seconds between diagnostics reports")
    parser.add_argument("--keep-days", type=float, default=14.0)
    parser.add_argument("--max-mb", type=float, default=200.0)
    parser.add_argument("--echo", action="store_true", help="also print what is written")
    parser.add_argument("--echo-reports", action="store_true", help="a [diag] line for every report, not only new ones")
    parser.add_argument("--port", help="this serial port instead of finding the adapter (tests)")
    parser.add_argument("--once", action="store_true", help="one pass, then exit (tests)")
    Logger(parser.parse_args()).run()


if __name__ == "__main__":
    main()
