#!/usr/bin/env python3
"""HD Rumble patterns through the adapter in Switch mode (OGX-Mini-improved, Linux).

The adapter in Switch mode is a Pro Controller (057e:2009) on the PC; this writes HD Rumble
output reports (0x10: packet number, left block, right block) to its hidraw node, as a Switch or
an emulator's direct Pro Controller driver would. With a Joy-Con / Pro Controller connected to the
adapter the pad plays the blocks as they are (Firmware/RP2040/docs/HD_Rumble.md); another pad gets
the intensity rumble decoded from them.

Block encoding as dekuNukem's rumble_data_table.md and SDL's EncodeRumble (SDL_hidapi_switch.c):
frequency code round(log2(f / 10) * 32), high band (code - 0x60) * 4 in 9 bits, low band
code - 0x40 in 7 bits; amplitude as an index 0-100 into the 101-step table (high band code 2 * i,
low band 0x40 + i / 2 with bit 15 for odd i).

    hd_rumble.py list                       patterns
    hd_rumble.py sweep-high [--side left]   play one (Ctrl+C stops; a neutral report ends it)
    hd_rumble.py compare                    the same strength as SDL's fixed-frequency rumble, then HD
"""
import argparse
import glob
import math
import os
import sys
import time

VID, PID = 0x057E, 0x2009
PERIOD_S = 0.015  # a Switch sends a rumble report about every 15 ms
NEUTRAL = bytes([0x00, 0x01, 0x40, 0x40])


def find_hidraw():
    for node in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        try:
            with open(os.path.join(node, "device", "uevent")) as f:
                uevent = f.read()
        except OSError:
            continue
        for line in uevent.splitlines():
            if line.startswith("HID_ID="):
                _, vid, pid = line.split("=")[1].split(":")
                if int(vid, 16) == VID and int(pid, 16) == PID:
                    return "/dev/" + os.path.basename(node)
    return None


def freq_code(hz):
    return round(math.log2(hz / 10.0) * 32.0)


def block(hf_hz=160.0, hf_amp=0, lf_hz=320.0, lf_amp=0):
    """One 4-byte motor block. Amplitudes are indexes 0-100 (0 = silent)."""
    if hf_amp <= 0 and lf_amp <= 0:
        return NEUTRAL
    hf = min(max((freq_code(hf_hz) - 0x60) * 4, 0), 0x1FC)
    lf = min(max(freq_code(lf_hz) - 0x40, 0), 0x7F)
    hf_amp = min(max(int(hf_amp), 0), 100)
    lf_amp = min(max(int(lf_amp), 0), 100)
    hfa = 2 * hf_amp
    lfa = ((lf_amp & 1) << 15) | (0x40 + lf_amp // 2)
    return bytes([hf & 0xFF, hfa | ((hf >> 8) & 0x01), lf | ((lfa >> 8) & 0x80), lfa & 0xFF])


def sweep_high():
    for i in range(80):
        hz = 82 * (1252 / 82) ** (i / 79)
        yield block(hf_hz=hz, hf_amp=70)


def sweep_low():
    for i in range(80):
        hz = 41 * (626 / 41) ** (i / 79)
        yield block(lf_hz=hz, lf_amp=70)


def amp_steps():
    for amp in list(range(0, 101, 10)) + list(range(100, -1, -10)):
        for _ in range(10):
            yield block(hf_hz=320, hf_amp=amp, lf_hz=160, lf_amp=amp)


def heartbeat():
    for _ in range(6):
        for _ in range(6):
            yield block(lf_hz=80, lf_amp=90)
        for _ in range(4):
            yield NEUTRAL
        for _ in range(4):
            yield block(lf_hz=60, lf_amp=60)
        for _ in range(30):
            yield NEUTRAL


def compare():
    # SDL's RumbleJoystick: fixed codes (high 0x74, low 0x3D, both about 150 Hz), only the strength.
    for _ in range(100):
        yield bytes([0x74, 2 * 60, 0x3D | 0x00, 0x40 + 30])
    for _ in range(40):
        yield NEUTRAL
    # The same strength as HD Rumble: a low thump then a high buzz.
    for _ in range(50):
        yield block(lf_hz=60, lf_amp=80)
    for _ in range(50):
        yield block(hf_hz=900, hf_amp=60)


PATTERNS = {
    "sweep-high": (sweep_high, "high band 82-1252 Hz at a fixed strength"),
    "sweep-low": (sweep_low, "low band 41-626 Hz at a fixed strength"),
    "amp-steps": (amp_steps, "both bands, strength 0-100 % and back"),
    "heartbeat": (heartbeat, "two low thumps, a pause, six times"),
    "compare": (compare, "SDL's fixed-frequency rumble, then HD Rumble of the same strength"),
}


def play(dev, blocks, side):
    seq = 0
    with open(dev, "wb", buffering=0) as f:
        try:
            for b in blocks:
                left = b if side in ("left", "both") else NEUTRAL
                right = b if side in ("right", "both") else NEUTRAL
                f.write(bytes([0x10, seq & 0x0F]) + left + right)
                seq += 1
                time.sleep(PERIOD_S)
        finally:
            f.write(bytes([0x10, seq & 0x0F]) + NEUTRAL + NEUTRAL)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pattern", choices=["list", *PATTERNS])
    ap.add_argument("--side", choices=["both", "left", "right"], default="both",
                    help="which block carries the pattern (a Joy-Con plays its own side's)")
    ap.add_argument("--repeat", type=int, default=1)
    args = ap.parse_args()
    if args.pattern == "list":
        for name, (_, text) in PATTERNS.items():
            print(f"{name:11} {text}")
        return
    dev = find_hidraw()
    if dev is None:
        sys.exit("No adapter in Switch mode (057e:2009) found: hold Start + D-pad Down about 3 s first.")
    gen, text = PATTERNS[args.pattern]
    print(f"{dev}: {args.pattern} ({text}), side {args.side}")
    try:
        for _ in range(args.repeat):
            play(dev, gen(), args.side)
    except PermissionError:
        sys.exit(f"No write access to {dev} (udev rule for 057e:2009, e.g. Steam's steam-devices)")
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
