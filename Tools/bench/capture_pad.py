#!/usr/bin/env python3
"""Record a controller's raw input reports for the end-to-end tests (fixtures).

    capture_pad.py ds4                       through the adapter (D-Input/Debug, Pico W / Pico 2 W
                                             build): the exact reports Bluepad32 parses there
    capture_pad.py ds4 --hidraw /dev/hidraw7 straight from the PC (a pad on USB or the PC's
                                             Bluetooth); the setup may differ from the adapter's
    capture_pad.py ds4 --steps buttons,sticks only some groups of the script
    capture_pad.py --list                    the script

Walks through a script of actions ("press A", "left trigger fully", "turn 90 degrees right"...):
Enter starts each one, it records for its time, then the next. Everything in between (the pad
setting up, idle) is kept too, unlabelled. Output: Firmware/RP2040/tests/fixtures/<name>.jsonl, a
header line then one line per report: time (ms), Bluetooth slot, label, hex. See
Firmware/RP2040/tests/fixtures/README.md.
"""
import argparse
import datetime
import gzip
import glob
import json
import os
import pathlib
import queue
import sys
import threading
import time

HERE = pathlib.Path(__file__).resolve().parent
FIXTURES = HERE.parents[1] / "Firmware/RP2040/tests/fixtures"

# (group, label, prompt, seconds)
SCRIPT = [
    ("idle", "idle", "Leave the controller on the table, hands off", 3),
    ("buttons", "a", "Press and release A (Cross on PlayStation) a few times", 3),
    ("buttons", "b", "Press and release B (Circle)", 3),
    ("buttons", "x", "Press and release X (Square)", 3),
    ("buttons", "y", "Press and release Y (Triangle)", 3),
    ("buttons", "lb", "Press and release L / L1", 3),
    ("buttons", "rb", "Press and release R / R1", 3),
    ("buttons", "back", "Press and release Select / Share / Minus", 3),
    ("buttons", "start", "Press and release Start / Options / Plus", 3),
    ("buttons", "home", "Press and release Home / PS / Guide (briefly, no hold)", 3),
    ("buttons", "capture", "Press and release Capture / Share / Mute (if the pad has it; else skip with Enter)", 3),
    ("buttons", "l3", "Click the left stick", 3),
    ("buttons", "r3", "Click the right stick", 3),
    ("dpad", "dpad_up", "D-pad up, press and release", 2),
    ("dpad", "dpad_down", "D-pad down", 2),
    ("dpad", "dpad_left", "D-pad left", 2),
    ("dpad", "dpad_right", "D-pad right", 2),
    ("triggers", "lt_half", "Left trigger (L2 / ZL) halfway, hold", 3),
    ("triggers", "lt_full", "Left trigger fully, hold", 3),
    ("triggers", "rt_half", "Right trigger (R2 / ZR) halfway, hold", 3),
    ("triggers", "rt_full", "Right trigger fully, hold", 3),
    ("sticks", "ls_right", "Left stick fully right, hold", 2),
    ("sticks", "ls_up", "Left stick fully up, hold", 2),
    ("sticks", "ls_circle", "Left stick slowly round the edge, twice", 5),
    ("sticks", "rs_right", "Right stick fully right, hold", 2),
    ("sticks", "rs_up", "Right stick fully up, hold", 2),
    ("sticks", "rs_circle", "Right stick slowly round the edge, twice", 5),
    ("touchpad", "touch_drag", "Touchpad (DS4 / DualSense): drag one finger left to right (else skip)", 3),
    ("touchpad", "touch_press", "Touchpad: press it down (else skip)", 3),
    ("motion", "flat", "Lay the controller flat, still", 3),
    ("motion", "yaw_right_90", "Flat on the table, turn it 90 degrees to the right (clockwise seen from above), then still", 4),
    ("motion", "pitch_up_90", "Tilt it towards you until it stands upright (90 degrees), then still", 4),
    ("motion", "roll_right_90", "Back flat, then roll it onto its right side (90 degrees), then still", 4),
]


def adapter_source(out_q, stop):
    """Records from the adapter's RAW_REPORT stream (needs pyserial: Tools/bench/requirements.txt)."""
    sys.path.insert(0, str(HERE))
    import adapter as A
    link = A.Adapter.open(timeout=10)
    caps = link.capabilities() or 0
    if not caps & A.CONTRACT["capabilities"]["RAW_CAPTURE"]:
        sys.exit("This adapter's firmware has no raw capture (needs a Pico W / Pico 2 W build with RAW_CAPTURE).")
    version = link.version()
    if link.request("RAW_CAPTURE", bytes([1])) is None:
        sys.exit("The adapter did not answer RAW_CAPTURE.")
    out_q.put(("meta", {"firmware": version[0] if version else "", "board": version[1] if version else ""}))
    buf, rec = b"", b""
    try:
        while not stop.is_set():
            buf += link.link.read(4096)
            while len(buf) >= A.PACKET_LEN:
                if buf[0] != A.PACKET_LEN or buf[8] > A.PACKET_LEN - A.HEADER_LEN or buf[6] == 0:
                    buf = buf[1:]
                    continue
                pkt, buf = buf[:A.PACKET_LEN], buf[A.PACKET_LEN:]
                if pkt[1] != A.IDS["RAW_REPORT"]:
                    continue
                total, idx, clen = pkt[6], pkt[7], pkt[8]
                if idx == 0:
                    rec = b""
                rec += pkt[A.HEADER_LEN:A.HEADER_LEN + clen]
                if idx + 1 == total and len(rec) >= 14:
                    n = rec[13]
                    out_q.put(("report", {
                        "slot": rec[1], "type": rec[2],
                        "vid": rec[3] | rec[4] << 8, "pid": rec[5] | rec[6] << 8,
                        "ms": int.from_bytes(rec[7:11], "little"),
                        "dropped": rec[11] | rec[12] << 8,
                        "hex": rec[14:14 + n].hex(),
                    }))
    finally:
        try:
            link.request("RAW_CAPTURE", bytes([0]), wait=0.5)
        finally:
            link.close()


def hidraw_ids(dev):
    node = "/sys/class/hidraw/" + os.path.basename(dev) + "/device/uevent"
    vid = pid = 0
    name = ""
    with open(node) as f:
        for line in f:
            if line.startswith("HID_ID="):
                _, v, p = line.strip().split("=")[1].split(":")
                vid, pid = int(v, 16), int(p, 16)
            elif line.startswith("HID_NAME="):
                name = line.strip().split("=", 1)[1]
    return vid, pid, name


def hidraw_source(dev, out_q, stop):
    vid, pid, name = hidraw_ids(dev)
    out_q.put(("meta", {"hidraw_name": name}))
    t0 = time.monotonic()
    fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
    try:
        while not stop.is_set():
            try:
                data = os.read(fd, 512)
            except BlockingIOError:
                time.sleep(0.001)
                continue
            out_q.put(("report", {"slot": 0, "type": None, "vid": vid, "pid": pid,
                                  "ms": int((time.monotonic() - t0) * 1000), "dropped": 0, "hex": data.hex()}))
    finally:
        os.close(fd)


MOTION_LABELS = {label for group, label, *_ in SCRIPT if group == "motion"}
SETUP_MS = 3000       # unlabelled reports kept at the start (a pad connecting: its setup replies)
DECIMATE_MS = 16      # other actions: about 60 reports a second per controller


def compact(rows):
    """What the file keeps: the start (setup) and the motion steps at full rate (the gyro is
    integrated), the other actions at about 60 reports a second; nothing else between actions."""
    if not rows:
        return rows
    t0 = rows[0]["t"]
    kept, last = [], {}
    for r in rows:
        label = r["label"]
        if label is None:
            if r["t"] - t0 <= SETUP_MS:
                kept.append(r)
            continue
        if label in MOTION_LABELS:
            kept.append(r)
            continue
        key = (label, r["slot"])
        if key not in last or r["t"] - last[key] >= DECIMATE_MS:
            kept.append(r)
            last[key] = r["t"]
    return kept


def read_fixture(path):
    opener = gzip.open if str(path).endswith(".gz") else open
    with opener(path, "rt") as f:
        lines = f.read().splitlines()
    return json.loads(lines[0]), [json.loads(l) for l in lines[1:]]


def write_fixture(path, meta, rows):
    with gzip.open(path, "wt", compresslevel=9) as f:
        f.write(json.dumps(meta) + "\n")
        for r in rows:
            f.write(json.dumps({"t": r["t"], "slot": r["slot"], "label": r["label"], "r": r["r"]}) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name", nargs="?", help="fixture name, e.g. ds4, joycon_pair, nova_lite_switch")
    ap.add_argument("--hidraw", help="record from this hidraw node on the PC instead of the adapter")
    ap.add_argument("--steps", help="comma-separated script groups (" + ",".join(dict.fromkeys(g for g, *_ in SCRIPT)) + ")")
    ap.add_argument("--redo", help="comma-separated step labels to record again; they replace those in the existing file")
    ap.add_argument("--compact", metavar="FILE", help="rewrite an older recording (.jsonl) in the kept form (.jsonl.gz) and exit")
    ap.add_argument("--out", help="output file (default: Firmware/RP2040/tests/fixtures/<name>.jsonl.gz)")
    ap.add_argument("--list", action="store_true", help="print the script")
    args = ap.parse_args()
    if args.list:
        for group, label, prompt, secs in SCRIPT:
            print(f"{group:9} {label:14} {secs}s  {prompt}")
        return
    if args.compact:
        meta, rows = read_fixture(args.compact)
        rows = compact(rows)
        out = pathlib.Path(args.out) if args.out else pathlib.Path(str(args.compact).removesuffix(".jsonl") + ".jsonl.gz")
        write_fixture(out, meta, rows)
        print(f"{len(rows)} reports -> {out} ({out.stat().st_size // 1024} KB)")
        return
    if not args.name:
        ap.error("a fixture name is needed")
    out = pathlib.Path(args.out) if args.out else FIXTURES / f"{args.name}.jsonl.gz"
    out.parent.mkdir(parents=True, exist_ok=True)
    steps = SCRIPT
    if args.steps:
        groups = set(args.steps.split(","))
        steps = [s for s in SCRIPT if s[0] in groups]
    previous = None
    if args.redo:
        redo = args.redo.split(",")
        unknown = [l for l in redo if l not in {s[1] for s in SCRIPT}]
        if unknown:
            ap.error(f"unknown steps: {', '.join(unknown)} (see --list)")
        if not out.exists():
            ap.error(f"{out} does not exist yet: record it first")
        previous = read_fixture(out)
        steps = [s for s in SCRIPT if s[1] in redo]

    q, stop = queue.Queue(), threading.Event()
    source = (lambda: hidraw_source(args.hidraw, q, stop)) if args.hidraw else (lambda: adapter_source(q, stop))
    reader = threading.Thread(target=source, daemon=True)
    reader.start()

    meta = {"format": "ogxm-fixture", "version": 1, "name": args.name,
            "source": "pc-hidraw" if args.hidraw else "adapter",
            "captured": datetime.date.today().isoformat(), "steps": [s[1] for s in steps]}
    lines, label, counts = [], None, {}

    def drain():
        while True:
            try:
                kind, item = q.get_nowait()
            except queue.Empty:
                return
            if kind == "meta":
                meta.update(item)
            else:
                lines.append({"t": item["ms"], "slot": item["slot"], "label": label, "r": item["hex"],
                              "vid": item["vid"], "pid": item["pid"], "type": item["type"], "dropped": item["dropped"]})
                counts[label] = counts.get(label, 0) + 1

    print(f"Recording {args.name} ({meta['source']}). Connect the controller now if it is not; Enter starts each step.")
    print("After each step: Enter for the next one, r + Enter to do it again.")
    try:
        input("Enter when the controller is connected and idle... ")
        drain()
        i = 0
        while i < len(steps):
            _, step_label, prompt, secs = steps[i]
            input(f"\n[{step_label}] {prompt} - Enter, then do it ({secs} s) ")
            drain()
            lines[:] = [r for r in lines if r["label"] != step_label]  # a redo replaces it
            counts[step_label] = 0
            label = step_label
            end = time.monotonic() + secs
            while time.monotonic() < end:
                time.sleep(0.05)
                drain()
            label = None
            if input(f"   {counts.get(step_label, 0)} reports. Enter = next, r = again: ").strip().lower() != "r":
                i += 1
    except KeyboardInterrupt:
        print("\nStopped; writing what was recorded.")
    stop.set()
    reader.join(timeout=2)
    drain()
    first = next((r for r in lines if r["vid"]), None)
    if first:
        meta.update({"vid": f"{first['vid']:04x}", "pid": f"{first['pid']:04x}", "controller_type": first["type"]})
    dropped = sum(r["dropped"] for r in lines)
    rows = compact(lines)
    if previous is not None:
        old_meta, old_rows = previous
        redone = {s[1] for s in steps}
        kept = [r for r in old_rows if r["label"] not in redone]
        new = [r for r in rows if r["label"] in redone]
        rows = sorted(kept + new, key=lambda r: (r["label"] is not None, r["t"]))
        old_meta["redone"] = sorted(set(old_meta.get("redone", [])) | redone)
        old_meta["dropped"] = old_meta.get("dropped", 0) + dropped
        meta = old_meta
    else:
        meta["dropped"] = dropped
    write_fixture(out, meta, rows)
    print(f"\n{len(rows)} reports kept ({dropped} dropped by the adapter) -> {out} ({out.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
