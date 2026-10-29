#!/usr/bin/env python3
"""Build synthetic fixtures for pads we do not have, from their public report formats.

    synth_pad.py dualsense     DualSense over Bluetooth (report 0x31; Linux hid-playstation)
    synth_pad.py switch_pro    Switch Pro Controller (report 0x30; dekuNukem's Switch notes)
    synth_pad.py all

Each one walks capture_pad.py's script of actions as a pad would do them: a button held for the
middle of its step, a trigger at half / full travel, a stick at its edge and round the circle, a
90-degree turn at 90 degrees a second. Output: Firmware/RP2040/tests/fixtures/<name>_synthetic.jsonl.gz
(`source: synthetic`). They test our chain on pads we lack, not a real pad's quirks (noise, dead
zones, how far its stick really goes). Feature report replies a pad sends while it is set up are
rows with `"kind": "feature"` (unlabelled, before the first action).
"""
import argparse
import datetime
import math
import struct
import sys
import zlib

import capture_pad

RATE_HZ = 60           # reports a second, the steps other than motion (as capture_pad keeps them)
MOTION_HZ = 250        # the motion steps, at full rate as recorded
STEP_S = 0.5           # length of a step other than motion
TURN_S = 1.0           # a 90-degree turn
STILL_S = 0.3          # still before and after a turn


class State:
    """What the pad is doing: sticks -1..1 (x right, y up), triggers 0..1, buttons by script label,
    the touch point (0..1, or None), the turn rate in degrees a second (pitch up, yaw right, roll
    right), and the gravity direction in the pad's frame (x right, y forward, z up)."""

    def __init__(self):
        self.ls = [0.0, 0.0]
        self.rs = [0.0, 0.0]
        self.lt = self.rt = 0.0
        self.buttons = set()
        self.dpad = None  # "up", "right", "down", "left"
        self.touch = None
        self.touch_press = False
        self.rate = [0.0, 0.0, 0.0]
        self.gravity = [0.0, 0.0, -1.0]


def steps():
    """(label, [(t_s, State)]) for each step of the script, in order."""
    out = []
    for group, label, _prompt, _secs in capture_pad.SCRIPT:
        frames = []
        if group == "motion":
            n = int((STILL_S * 2 + (TURN_S if label != "flat" else 0)) * MOTION_HZ)
            for i in range(n):
                t = i / MOTION_HZ
                s = State()
                turning = label != "flat" and STILL_S <= t < STILL_S + TURN_S
                axis = {"pitch_up_90": 0, "yaw_right_90": 1, "roll_right_90": 2}.get(label)
                if turning:
                    s.rate[axis] = 90.0 / TURN_S
                done = 0.0 if label == "flat" else min(max((t - STILL_S) / TURN_S, 0.0), 1.0)
                a = math.radians(90.0 * done)
                if label == "pitch_up_90":    # front up: gravity moves to -y
                    s.gravity = [0.0, -math.sin(a), -math.cos(a)]
                elif label == "roll_right_90":  # right side down: gravity moves to +x
                    s.gravity = [math.sin(a), 0.0, -math.cos(a)]
                frames.append((t, s))
            out.append((label, frames))
            continue
        n = int(STEP_S * RATE_HZ)
        for i in range(n):
            t = i / RATE_HZ
            # pressed in the middle of the step, released well before its end: the PS button is held
            # for 8 reports more in PS3 / PS4 mode, and a recording has no reports between steps
            held = 0.2 <= t / STEP_S <= 0.6
            s = State()
            if group == "buttons" and held:
                s.buttons.add(label)
            elif group == "dpad" and held:
                s.dpad = label.split("_")[1]
            elif group == "triggers":
                v = (0.5 if label.endswith("half") else 1.0) if held else 0.0
                if label.startswith("lt"):
                    s.lt = v
                else:
                    s.rt = v
            elif group == "sticks":
                stick = s.ls if label.startswith("ls") else s.rs
                if label.endswith("circle"):  # twice round, clamped to the square the axes allow
                    a = 2 * 2 * math.pi * i / (n - 1)
                    stick[0], stick[1] = math.cos(a) * 1.5, math.sin(a) * 1.5
                elif held:
                    stick[0 if label.endswith("right") else 1] = 1.0
                stick[0] = max(-1.0, min(1.0, stick[0]))
                stick[1] = max(-1.0, min(1.0, stick[1]))
            elif group == "touchpad":
                if label == "touch_drag":
                    s.touch = (0.1 + 0.8 * i / (n - 1), 0.5)
                elif held:
                    s.touch = (0.5, 0.5)
                    s.touch_press = True
            frames.append((t, s))
        out.append((label, frames))
    return out


def u8_axis(v, up_low=False):
    """-1..1 to 0..255, 128 at rest (up = 0 when `up_low`)."""
    if up_low:
        v = -v
    return max(0, min(255, round(127.5 + v * 127.5)))


# ---- DualSense (Bluetooth report 0x31, 78 bytes) ----------------------------------------------

DS5_GYRO_PER_DEG_S = 1024 / 64   # raw counts a degree/s with the calibration below (Bluepad32: x64)
DS5_ACC_PER_G = 8192


def ds5_setup():
    pairing = bytes([0x09]) + bytes(19)
    fw = bytearray(64)
    fw[0] = 0x20
    fw[1:12] = b"Jun 01 2023"
    fw[12:20] = b"12:00:00"
    struct.pack_into("<I", fw, 24, 0x0115)     # hw_version
    struct.pack_into("<I", fw, 28, 0x0110)     # fw_version
    struct.pack_into("<H", fw, 44, 0x0224)     # update_version 2.36: Vibration2
    cal = bytearray(41)
    cal[0] = 0x05
    # gyro bias, then plus / minus per axis (pitch, yaw, roll), speed plus / minus, accel plus / minus
    struct.pack_into("<3h", cal, 1, 0, 0, 0)
    struct.pack_into("<6h", cal, 7, 8640, -8640, 8640, -8640, 8640, -8640)
    struct.pack_into("<2h", cal, 19, 540, 540)
    struct.pack_into("<6h", cal, 23, DS5_ACC_PER_G, -DS5_ACC_PER_G, DS5_ACC_PER_G, -DS5_ACC_PER_G,
                     DS5_ACC_PER_G, -DS5_ACC_PER_G)
    return [bytes(pairing), bytes(fw), bytes(cal)]


def ds5_report(s, seq, t_us):
    p = bytearray(63)  # ds5_input_report_t
    p[0], p[1] = u8_axis(s.ls[0]), u8_axis(s.ls[1], up_low=True)
    p[2], p[3] = u8_axis(s.rs[0]), u8_axis(s.rs[1], up_low=True)
    p[4], p[5] = round(s.lt * 255), round(s.rt * 255)
    p[6] = seq & 0xFF
    hat = {"up": 0, "right": 2, "down": 4, "left": 6}.get(s.dpad, 8)
    b0 = hat
    b0 |= 0x10 if "x" in s.buttons else 0
    b0 |= 0x20 if "a" in s.buttons else 0
    b0 |= 0x40 if "b" in s.buttons else 0
    b0 |= 0x80 if "y" in s.buttons else 0
    b1 = 0
    for label, bit in (("lb", 0x01), ("rb", 0x02), ("back", 0x10), ("start", 0x20), ("l3", 0x40), ("r3", 0x80)):
        if label in s.buttons:
            b1 |= bit
    b1 |= 0x04 if s.lt > 0 else 0
    b1 |= 0x08 if s.rt > 0 else 0
    b2 = (0x01 if "home" in s.buttons else 0) | (0x02 if s.touch_press else 0) | (0x04 if "capture" in s.buttons else 0)
    p[7], p[8], p[9] = b0, b1, b2
    # gyro x = pitch (up +), y = yaw (right -), z = roll (right -), as the DS4 recording shows them
    gyro = (s.rate[0], -s.rate[1], -s.rate[2])
    struct.pack_into("<3h", p, 15, *(round(v * DS5_GYRO_PER_DEG_S) for v in gyro))
    # accel (not checked by the test): hid-playstation's axes, +1 g on y lying flat face up
    gx, gy, gz = s.gravity
    acc = (-gx, -gz, gy)
    struct.pack_into("<3h", p, 21, *(round(v * DS5_ACC_PER_G) for v in acc))
    struct.pack_into("<I", p, 27, t_us & 0xFFFFFFFF)
    for i in range(2):  # touch points: bit 7 of the first byte set = no finger
        at = 32 + 4 * i
        if i == 0 and s.touch is not None:
            x, y = round(s.touch[0] * 1919), round(s.touch[1] * 1079)
            p[at] = 0x01
            p[at + 1] = x & 0xFF
            p[at + 2] = ((x >> 8) & 0x0F) | ((y & 0x0F) << 4)
            p[at + 3] = y >> 4
        else:
            p[at] = 0x80
    p[52] = 0x08  # battery 80 %, discharging
    r = bytearray([0x31, 0x10]) + p
    r += bytes(78 - 4 - len(r))
    crc = zlib.crc32(bytes([0xA1]) + bytes(r)) & 0xFFFFFFFF
    return bytes(r + struct.pack("<I", crc))


# ---- Switch Pro Controller (report 0x30) -------------------------------------------------------

PRO_STICK_CENTER, PRO_STICK_RANGE = 2048, 1400  # 12-bit, the factory calibration's span
PRO_GYRO_PER_DEG_S = 13371 / 936   # Bluepad32's default IMU calibration (936 dps over 13371)
PRO_ACC_PER_G = 4096


def pro_stick(v):
    x = round(PRO_STICK_CENTER + max(-1.0, min(1.0, v[0])) * PRO_STICK_RANGE)
    y = round(PRO_STICK_CENTER + max(-1.0, min(1.0, v[1])) * PRO_STICK_RANGE)
    return bytes([x & 0xFF, ((x >> 8) & 0x0F) | ((y & 0x0F) << 4), (y >> 4) & 0xFF])


def pro_report(s, timer):
    r = bytearray(49)
    r[0], r[1], r[2] = 0x30, timer & 0xFF, 0x8E  # battery full, Pro / charging grip
    # by position: the script's "x" is the left button (Y on a Switch pad, 0x01), "y" the top one
    # (X, 0x02), "a" the bottom one (B, 0x04), "b" the right one (A, 0x08)
    right = 0
    for label, bit in (("x", 0x01), ("y", 0x02), ("a", 0x04), ("b", 0x08), ("rb", 0x40)):
        if label in s.buttons:
            right |= bit
    right |= 0x80 if s.rt > 0 else 0
    shared = 0
    for label, bit in (("back", 0x01), ("start", 0x02), ("r3", 0x04), ("l3", 0x08), ("home", 0x10), ("capture", 0x20)):
        if label in s.buttons:
            shared |= bit
    left = {"down": 0x01, "up": 0x02, "right": 0x04, "left": 0x08}.get(s.dpad, 0)
    left |= 0x40 if "lb" in s.buttons else 0
    left |= 0x80 if s.lt > 0 else 0
    r[3], r[4], r[5] = right, shared, left
    r[6:9] = pro_stick(s.ls)
    r[9:12] = pro_stick(s.rs)
    r[12] = 0x0B  # vibrator input report
    # Measured on the Joy-Con pair recording (its right half; the left half and a Pro controller
    # are turned 180 degrees about x from it): x forward, y right, z up on a Pro controller; pitch
    # up = -y, yaw right = -z, roll right = -x; the accelerometer reads the reaction to gravity
    # (+1 g on z lying flat face up).
    gyro = (-s.rate[2], -s.rate[0], -s.rate[1])
    gx, gy, gz = s.gravity
    acc = (-gy, -gx, -gz)
    for k in range(3):  # three samples 5 ms apart, the same here
        at = 13 + 12 * k
        struct.pack_into("<3h", r, at, *(round(v * PRO_ACC_PER_G) for v in acc))
        struct.pack_into("<3h", r, at + 6, *(round(v * PRO_GYRO_PER_DEG_S) for v in gyro))
    return bytes(r)


# ---- files -------------------------------------------------------------------------------------

PADS = {
    "dualsense": dict(vid="054c", pid="0ce6", controller_type=45, about="DualSense, Bluetooth report 0x31"),
    "switch_pro": dict(vid="057e", pid="2009", controller_type=38, about="Switch Pro Controller, report 0x30"),
}


def build(name):
    meta = PADS[name]
    rows = []
    t_ms = 0.0
    seq = 0
    if name == "dualsense":
        for f in ds5_setup():
            rows.append({"t": round(t_ms), "slot": 0, "label": None, "r": f.hex(), "kind": "feature"})
            t_ms += 10
    rest = State()
    for _ in range(30):  # connected, at rest
        r = ds5_report(rest, seq, round(t_ms * 1000)) if name == "dualsense" else pro_report(rest, seq)
        rows.append({"t": round(t_ms), "slot": 0, "label": None, "r": r.hex()})
        t_ms += 1000 / RATE_HZ
        seq += 1
    for label, frames in steps():
        start = t_ms + 200
        for t_s, s in frames:
            t_ms = start + t_s * 1000
            r = ds5_report(s, seq, round(t_ms * 1000)) if name == "dualsense" else pro_report(s, seq)
            rows.append({"t": round(t_ms), "slot": 0, "label": label, "r": r.hex()})
            seq += 1
    header = {
        "format": "ogxm-fixture", "version": 1, "name": f"{name}_synthetic", "source": "synthetic",
        "captured": datetime.date.today().isoformat(), "vid": meta["vid"], "pid": meta["pid"],
        "controller_type": meta["controller_type"],
        "steps": [label for _g, label, *_ in capture_pad.SCRIPT], "dropped": 0,
        "about": meta["about"] + "; built by Tools/bench/synth_pad.py",
    }
    path = capture_pad.FIXTURES / f"{name}_synthetic.jsonl.gz"
    import gzip
    import json
    with gzip.GzipFile(path, "wb", compresslevel=9, mtime=0) as f:  # mtime 0: same bytes each run
        f.write((json.dumps(header) + "\n").encode())
        for r in rows:
            f.write((json.dumps(r) + "\n").encode())
    print(f"{path}: {len(rows)} reports")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pad", choices=[*PADS, "all"])
    args = ap.parse_args()
    for name in PADS if args.pad == "all" else [args.pad]:
        build(name)


if __name__ == "__main__":
    sys.exit(main())
