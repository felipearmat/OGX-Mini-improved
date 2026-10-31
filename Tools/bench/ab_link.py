#!/usr/bin/env python3
"""Compare the Bluetooth link of firmware builds on the bench hybrid, without playing.

Flashes each build (both chips) in turn, waits for the controllers, then for a fixed time plays a
fixed rumble pattern and collects what the adapter measures: input gaps (events, sampled at most one
per slot every 5 s, the ones between counted), reports per second, lost reports, largest gap. Keep
the controllers in one place for the whole run: only the firmware should change between rounds.
Because the gaps vary a lot from one run to the next, compare several rounds of each build.

    ab_link.py run --builds A=DIR B=DIR C=DIR --rounds 3 --minutes 15 --pads 2 --out DIR
    ab_link.py summary --out DIR
    ab_link.py soak --builds A=DIR B=DIR --hours 2 --out DIR    (STEAM mode, closer to a game)
    ab_link.py soak-summary --out DIR

soak: the bus is in use as in a game (the PC's rumble through Linux force feedback, USB reports all
the time), and the figures come from the flash log. Asks for the mode combos, and waits for them as
long as it takes; run it under systemd-inhibit so the PC does not suspend.

DIR holds the RP2040's .uf2 and an esp32/ folder (the ESP-IDF build). The adapter must be in
D-Input/Debug mode, which is also the mode it is measured in. The idle turn-off is off during the
run and restored at the end.
"""
import argparse
import fcntl
import json
import os
import pathlib
import re
import statistics
import struct
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from adapter import Adapter, IDS  # noqa: E402
from run_bench import flash_esp32, flash_pico  # noqa: E402

IDLE_OFF_OFFSET = 16  # DongleSettings.h, Settings::idle_off_minutes
SETTLE_S = 30
RUMBLE_EVERY_S = 10
RUMBLE = bytes([128, 128]) + (3000).to_bytes(2, "little")  # both motors, 3 s
GAP = re.compile(r"(\d+\.\d+) slot (\d): no input (?:for )?(\d+) ms(?:, \+(\d+))?")
GONE = re.compile(r"slot (\d) (?:gone|disconnected)|link 0x[0-9a-f]+ closed, reason (0x[0-9a-f]+)")


def notify(text):
    print(f"{time.strftime('%H:%M:%S')} {text}", flush=True)
    subprocess.run(["notify-send", "-u", "critical", "OGX A/B", text], capture_output=True)


def pads(adapter):
    return (adapter.diagnostics() or {}).get("controllers") or []


def wait_pads(wanted, label):
    """Waits as long as it takes (nobody may be watching); the reminder repeats."""
    text = f"{label}: press a button on the controllers to connect them"
    notify(text)
    next_reminder = time.monotonic() + 300
    while True:
        adapter = Adapter.open(timeout=60)
        try:
            while True:
                if len(pads(adapter)) >= wanted:
                    return
                if time.monotonic() >= next_reminder:
                    notify(text)
                    next_reminder = time.monotonic() + 300
                time.sleep(2)
        except (OSError, AttributeError):
            pass  # the adapter restarted
        finally:
            adapter.close()


def set_idle_off(adapter, minutes):
    """Returns the value it had."""
    record = bytearray(adapter.dongle_settings())
    old = record[IDLE_OFF_OFFSET]
    if old != minutes:
        record[IDLE_OFF_OFFSET] = minutes
        adapter.set_dongle_settings(record)
        time.sleep(1)
    return old


def measure(minutes):
    adapter = Adapter.open(timeout=60)
    gaps, closed = [], []
    tail, since = "", None
    snapshots = []
    try:
        time.sleep(SETTLE_S)
        _, since, _ = adapter.log_since(0)
        end = time.monotonic() + minutes * 60
        next_rumble = next_report = 0.0
        while time.monotonic() < end:
            now = time.monotonic()
            if now >= next_rumble:
                adapter.send(IDS["SET_GP_OUT"], RUMBLE)
                next_rumble = now + RUMBLE_EVERY_S
            if now >= next_report:
                report = adapter.diagnostics()
                if report:
                    snapshots.append(report)
                next_report = now + 60
            log = adapter.log_since(since)
            if log:
                _, since, data = log
                lines = (tail + data.decode(errors="replace")).split("\n")
                tail = lines.pop()
                for line in lines:
                    if "BP32:" in line:
                        continue  # the ESP32's copy of the same event
                    m = GAP.search(line)
                    if m:
                        gaps.append([float(m[1]), int(m[2]), int(m[3]), int(m[4] or 0)])
                    elif GONE.search(line):
                        closed.append(line.strip())
            time.sleep(1)
        final = adapter.diagnostics()
        if final:
            snapshots.append(final)
    finally:
        adapter.close()
    return {"gaps": gaps, "closed": closed, "snapshots": snapshots}


def round_figures(data, minutes):
    """The figures of one round, per hour where it is a count."""
    gaps = data["gaps"]
    hours = minutes / 60
    long_gaps = [g for g in gaps if g[2] >= 200]
    together = sum(1 for g in gaps if g[2] >= 100 and any(
        o[1] != g[1] and abs(o[0] - g[0]) < 1.0 and o[2] >= 100 for o in gaps))
    last = (data["snapshots"] or [{}])[-1]
    pad_figures = [{k: p.get(k) for k in ("name", "reports_per_s", "late_reports_pct", "lost_reports_pct",
                                          "max_gap_ms_since_connect", "rssi_golden_range_db", "channels_in_use",
                                          "failed_contacts")}
                   for p in last.get("controllers") or []]
    latency = last.get("input_to_output_latency") or {}
    return {
        "gaps_per_h": round(sum(1 + g[3] for g in gaps) / hours),
        "sampled": len(gaps),
        "long_200ms": len(long_gaps),
        "max_sampled_ms": max((g[2] for g in gaps), default=0),
        "together_100ms": together,
        "closed": len(data["closed"]),
        "pads": pad_figures,
        "latency": latency,
        "i2c_drops": (last.get("companion") or {}).get("i2c_queue_drops"),
    }


def summary(out):
    rows = {}
    for path in sorted(out.glob("*.json")):
        data = json.loads(path.read_text())
        rows.setdefault(data["build"], []).append((path.stem, round_figures(data, data["minutes"])))
    for build, runs in sorted(rows.items()):
        print(f"== {build}")
        for name, f in runs:
            max_gap = [p["max_gap_ms_since_connect"] for p in f["pads"]]
            lost = [p["lost_reports_pct"] for p in f["pads"]]
            rate = [p["reports_per_s"] for p in f["pads"]]
            print(f"  {name}: gaps/h {f['gaps_per_h']:5d}  >=200ms {f['long_200ms']:3d}  max gap {max_gap}  lost {lost}"
                  f"  r/s {rate}  both>=100ms {f['together_100ms']:3d}  closed {f['closed']}  i2c drops {f['i2c_drops']}")
        print(f"  median gaps/h {statistics.median(f['gaps_per_h'] for _, f in runs):.0f}")


def run(args):
    builds = dict(b.split("=", 1) for b in args.builds)
    names = list(builds)
    args.out.mkdir(parents=True, exist_ok=True)
    adapter = Adapter.open(timeout=60)
    idle = set_idle_off(adapter, 0)
    adapter.close()
    print(f"idle turn-off was {idle} min, off for the run", flush=True)
    try:
        for r in range(args.rounds):
            order = names[r % len(names):] + names[:r % len(names)]  # each build in each position
            for name in order:
                build = pathlib.Path(builds[name])
                label = f"round {r + 1}/{args.rounds} {name}"
                notify(f"{label}: flashing")
                flash_pico(next(build.glob("*.uf2")))
                time.sleep(6)
                flash_esp32(build / "esp32")
                wait_pads(args.pads, label)
                notify(f"{label}: measuring {args.minutes} min, leave the controllers still")
                data = measure(args.minutes)
                data.update(build=name, minutes=args.minutes, round=r + 1)
                (args.out / f"r{r + 1}-{name}.json").write_text(json.dumps(data))
                f = round_figures(data, args.minutes)
                print(f"{label}: gaps/h {f['gaps_per_h']} >=200ms {f['long_200ms']} closed {f['closed']}", flush=True)
    finally:
        try:
            adapter = Adapter.open(timeout=60)
            set_idle_off(adapter, idle)
            adapter.close()
        except Exception as error:  # the run's own error matters more
            print(f"idle turn-off not restored ({idle} min): {error}", flush=True)
    notify("A/B run finished")
    summary(args.out)


FF_RUMBLE = 0x50
EV_FF = 0x15
EVIOCSFF = 0x40304580  # sizeof(struct ff_effect) == 48 on x86-64
NODE_NAME = "Sony Interactive Entertainment Wireless Controller"
REMIND_S = 300


def event_node(suffix=""):
    for path in pathlib.Path("/sys/class/input").glob("event*"):
        try:
            if (path / "device/name").read_text().strip() == NODE_NAME + suffix:
                return f"/dev/input/{path.name}"
        except OSError:
            pass
    return None


def wait_for(check, text):
    """The value of check() once it has one; asks again now and then (nobody may be watching)."""
    notify(text)
    next_reminder = time.monotonic() + REMIND_S
    while True:
        value = check()
        if value:
            return value
        if time.monotonic() >= next_reminder:
            notify(text)
            next_reminder = time.monotonic() + REMIND_S
        time.sleep(2)


def motion_alive():
    """A pad is connected: its motion readings change (the neutral report's do not)."""
    node = event_node(" Motion Sensors")
    if not node:
        return False
    try:
        fd = os.open(node, os.O_RDONLY)
        seen = set()
        try:
            for _ in range(30):
                row = []
                for code in range(6):
                    buf = bytearray(24)
                    fcntl.ioctl(fd, 0x80184540 + code, buf)  # EVIOCGABS
                    row.append(struct.unpack("6i", buf)[0])
                seen.add(tuple(row))
                time.sleep(0.1)
        finally:
            os.close(fd)
        return len(seen) > 5
    except OSError:
        return False


def steam_rumble(hours):
    """Rumble from the PC for `hours`, as a game would: 3 s every 10 s. Returns the USB losses."""
    end = time.monotonic() + hours * 3600
    fd, effect, lost = None, None, []
    while time.monotonic() < end:
        try:
            if fd is None:
                node = event_node()
                if not node:
                    raise OSError("no node")
                fd = os.open(node, os.O_RDWR)
                buf = bytearray(struct.pack("HhHHHHHxx", FF_RUMBLE, -1, 0, 0, 0, 3000, 0)
                                + struct.pack("HH", 0x8000, 0x8000) + bytes(28))
                fcntl.ioctl(fd, EVIOCSFF, buf)
                effect = struct.unpack("h", buf[2:4])[0]
            os.write(fd, struct.pack("llHHi", 0, 0, EV_FF, effect, 1))
        except OSError as error:
            if fd is not None:
                os.close(fd)
                fd = None
            lost.append([round(time.monotonic()), str(error)])
        time.sleep(10)
    if fd is not None:
        os.close(fd)
    return lost


def debug_port():
    try:
        return Adapter.open(timeout=2)
    except (RuntimeError, OSError):
        return None


def soak_figures(log_text):
    """From the flash log: the last STEAM boot's gaps, per 10 minutes."""
    lines = [l for l in log_text.splitlines() if "[rp]" in l]
    starts = [i for i, l in enumerate(lines) if " boot STEAM" in l]
    if not starts:
        return None
    rows = lines[starts[-1]:]
    gaps, other, names = [], [], {}
    for line in rows:
        m = GAP.search(line)
        if m:
            gaps.append([float(m[1]), int(m[2]), int(m[3]), int(m[4] or 0)])
        elif re.search(r"summary|closed, reason|gone|crash|mode change", line):
            other.append(line.split("[rp] ", 1)[1])
            # The halves take slots in the order they connect: only the product id tells them apart.
            s = re.search(r"slot (\d) summary 057e:(2006|2007)", line)
            if s:
                names[int(s[1])] = "L" if s[2] == "2006" else "R"
    for gap in gaps:
        gap[1] = names.get(gap[1], "RL"[gap[1] == 0])
    bins = {}
    for t, half, size, more in gaps:
        row = bins.setdefault(int(t // 600) * 10, {"L": 0, "R": 0, "max": 0})
        row[half] += 1 + more
        row["max"] = max(row["max"], size)
    return {"gaps": gaps, "other": other, "bins": bins}


def soak_summary(out):
    for path in sorted(out.glob("*/*.log")):
        f = soak_figures(path.read_text(errors="replace"))
        if not f:
            continue
        g = f["gaps"]
        print(f"== {path.parent.name}: gaps L {sum(1 + x[3] for x in g if x[1] == 'L')} "
              f"R {sum(1 + x[3] for x in g if x[1] == 'R')}, sampled >=100 ms {sum(x[2] >= 100 for x in g)}, "
              f">=200 ms {sum(x[2] >= 200 for x in g)}, max sampled {max((x[2] for x in g), default=0)}")
        for minute, row in sorted(f["bins"].items()):
            print(f"  {minute:4d} min: L {row['L']:4d} R {row['R']:4d} max {row['max']}")
        for line in f["other"]:
            print(f"  {line[:150]}")


def soak(args):
    builds = dict(b.split("=", 1) for b in args.builds)
    args.out.mkdir(parents=True, exist_ok=True)
    idle = args.idle
    for name, directory in builds.items():
        build = pathlib.Path(directory)
        go = args.out / f"go-{name}"  # motion cannot tell one Joy-Con from two: someone confirms
        if not (args.already_running and name == next(iter(builds))):
            go.unlink(missing_ok=True)
            adapter = wait_for(debug_port, f"{name}: put the adapter in D-Input/Debug (Start + RB + Left, 3 s)")
            adapter.close()
            notify(f"{name}: flashing")
            flash_pico(next(build.glob("*.uf2")))
            time.sleep(6)
            flash_esp32(build / "esp32")
            adapter = Adapter.open(timeout=60)
            old = set_idle_off(adapter, 0)
            idle = old if idle is None else idle
            adapter.close()
        wait_for(go.exists, f"{name}: combo for STEAM mode, connect both Joy-Cons, then confirm ({go.name})")
        notify(f"{name}: running {args.hours} h, leave the controllers still")
        lost = steam_rumble(args.hours)
        wait_for(debug_port_free, f"{name}: done. Combo for D-Input/Debug (Start + RB + Left, 3 s)")
        time.sleep(5)
        subprocess.run([sys.executable, str(pathlib.Path(__file__).parent / "logger.py"), "--dir",
                        str(args.out / name), "--once"], capture_output=True)
        (args.out / name / "usb_lost.json").write_text(json.dumps(lost))
    if idle is not None:
        adapter = Adapter.open(timeout=60)
        set_idle_off(adapter, idle)
        adapter.close()
    notify("Soak finished")
    soak_summary(args.out)


def debug_port_free():
    adapter = debug_port()
    if adapter:
        adapter.close()
        return True
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("run")
    p.add_argument("--builds", nargs="+", required=True, metavar="NAME=DIR")
    p.add_argument("--rounds", type=int, default=3)
    p.add_argument("--minutes", type=float, default=15)
    p.add_argument("--pads", type=int, default=2)
    p.add_argument("--out", type=pathlib.Path, required=True)
    p = sub.add_parser("summary")
    p.add_argument("--out", type=pathlib.Path, required=True)
    p = sub.add_parser("soak")
    p.add_argument("--builds", nargs="+", required=True, metavar="NAME=DIR")
    p.add_argument("--hours", type=float, default=2)
    p.add_argument("--already-running", action="store_true",
                   help="the first build is flashed and in STEAM mode: wait only for the confirmation")
    p.add_argument("--idle", type=int, help="the idle turn-off to restore at the end, if it is already off")
    p.add_argument("--out", type=pathlib.Path, required=True)
    p = sub.add_parser("soak-summary")
    p.add_argument("--out", type=pathlib.Path, required=True)
    args = parser.parse_args()
    {"run": run, "soak": soak, "summary": lambda a: summary(a.out), "soak-summary": lambda a: soak_summary(a.out)}[
        args.command](args)


if __name__ == "__main__":
    main()
