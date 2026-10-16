#!/usr/bin/env python3
"""Maintain an OGX-Mini adapter through its own USB port.

On the RP2040 + ESP32 hybrid everything goes through the RP2040 / RP2350's USB port: the ESP32 is
flashed and its log read through it. The adapter must be in D-Input/Debug mode (hold Start + RB + Left
about 3 s), which shows up as a USB serial port.

  ogxm_hybrid.py ports                       list OGX-Mini serial ports
  ogxm_hybrid.py bootsel [--uf2 FILE]        RP2040 / RP2350 to its USB bootloader (and load a UF2)
  ogxm_hybrid.py flash-esp32 BUILD_DIR       flash the hybrid's ESP32 (idf.py build output, or a
                                             folder with bootloader.bin, partition-table.bin and
                                             the app .bin)
  ogxm_hybrid.py monitor [--log FILE] [--seconds N]
                                             the kept log (previous session) then the live log of
                                             both boards, for N seconds or until Ctrl+C; the
                                             adapter returns to its mode however the tool ends
  ogxm_hybrid.py reboot

Needs pyserial; flash-esp32 needs esptool (pip install pyserial esptool); --uf2 uses picotool if
it is installed, else copies to the RPI-RP2 / RP2350 drive.
"""
import argparse
import json
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is needed: pip install pyserial")

VID = 0xCAFE
WEBAPP_PRODUCT = "OGX-Mini"
BRIDGE_PRODUCT = "OGX-Mini ESP32 bridge"
MONITOR_PRODUCT = "OGX-Mini ESP32 monitor"
COMPLETE_FLAG = b"PROGRAMMING_COMPLETE"
BRIDGE_GUARD_S = 5.5  # the bridge ignores the serial control lines this long after starting

PACKET_LEN = 64
SYSTEM = 0x75
RESP_ERROR = 0xFF
WEBAPP_DRIVER = 100
COMMANDS = {"bootsel": 1, "reboot": 2, "esp32-flash": 3, "esp32-monitor": 4}


def ogxm_ports():
    return [p for p in list_ports.comports() if p.vid == VID]


def find_port(product, timeout=0.0):
    deadline = time.monotonic() + timeout
    while True:
        for p in ogxm_ports():
            if (p.product or "") == product:
                return p.device
        if time.monotonic() >= deadline:
            return None
        time.sleep(0.25)


def webapp_port():
    port = find_port(WEBAPP_PRODUCT)
    if port is None:
        sys.exit("No adapter in D-Input/Debug mode found. Hold Start + RB + Left about 3 s on a controller "
                 "(or pick D-Input/Debug in the web app), then try again.")
    return port


def system_command(name):
    """Send a SYSTEM command in D-Input/Debug mode and wait for its answer."""
    command = COMMANDS[name]
    port = webapp_port()
    packet = bytearray(PACKET_LEN)
    packet[0:9] = bytes([PACKET_LEN, SYSTEM, WEBAPP_DRIVER, 1, 0, 0, 1, 0, 1])
    packet[9] = command
    # exclusive (an flock): never at the same time as Tools/bench (its logger reads the port for a
    # moment now and then, so a busy port is tried again for a few seconds).
    deadline = time.monotonic() + 5.0
    while True:
        try:
            link = serial.Serial(port, 115200, timeout=0.2, exclusive=True)
            break
        except serial.SerialException as error:
            if time.monotonic() > deadline:
                sys.exit(f"Could not open {port} ({error}). Is the web app (or another program) connected to "
                         "the adapter? Disconnect it and try again.")
            time.sleep(0.3)
    with link:
        link.reset_input_buffer()
        link.write(packet)
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            reply = link.read(PACKET_LEN)
            if len(reply) < 2:
                continue
            if reply[1] == SYSTEM:
                return
            if reply[1] == RESP_ERROR:
                sys.exit(f"The adapter refused '{name}' (not a hybrid build, or an older firmware).")
    sys.exit(f"No answer to '{name}' (older firmware without maintenance commands?).")


def cmd_ports(_args):
    ports = ogxm_ports()
    if not ports:
        print("No OGX-Mini serial port (is the adapter in D-Input/Debug mode?)")
    for p in ports:
        print(f"{p.device}\t{p.product}\t{p.serial_number}")


def cmd_reboot(_args):
    system_command("reboot")
    print("Rebooting.")


def find_uf2_drive():
    for root in ("/Volumes", "/media/" + os.environ.get("USER", ""), "/run/media/" + os.environ.get("USER", "")):
        for name in ("RPI-RP2", "RP2350"):
            path = pathlib.Path(root) / name
            if path.is_dir():
                return path
    return None


def cmd_bootsel(args):
    system_command("bootsel")
    print("The RP2040 / RP2350 is in its USB bootloader.")
    if not args.uf2:
        return
    time.sleep(2.0)
    if shutil.which("picotool"):
        subprocess.run(["picotool", "load", "-v", "-x", args.uf2], check=True)
        return
    drive = None
    for _ in range(40):
        drive = find_uf2_drive()
        if drive:
            break
        time.sleep(0.25)
    if drive is None:
        sys.exit("No RPI-RP2 / RP2350 drive found; copy the UF2 to it by hand.")
    shutil.copy(args.uf2, drive)
    print(f"Copied {args.uf2} to {drive}; the board restarts with it.")


def esp32_images(build_dir):
    """(offset, path) pairs: from flasher_args.json when present, else the usual layout."""
    build = pathlib.Path(build_dir)
    args_file = build / "flasher_args.json"
    if args_file.exists():
        flash = json.loads(args_file.read_text())["flash_files"]
        images = []
        for offset, path in sorted(flash.items(), key=lambda i: int(i[0], 16)):
            full = build / path
            if not full.exists():
                full = build / pathlib.Path(path).name  # the files copied into one folder
            images.append((offset, str(full)))
        return images
    apps = [p for p in build.glob("*.bin") if p.name not in ("bootloader.bin", "partition-table.bin")]
    if len(apps) != 1:
        sys.exit(f"{build}: expected one app .bin next to bootloader.bin and partition-table.bin")
    return [("0x1000", str(build / "bootloader.bin")), ("0x8000", str(build / "partition-table.bin")),
            ("0x10000", str(apps[0]))]


def bridge_bootloader(port):
    """Put the ESP32 back in its ROM bootloader through the bridge's serial lines.

    The bridge drives EN low while DTR && !RTS and IO0 low while RTS && !DTR (uart_bridge.c), so
    EN must rise in the same request that pulls IO0 low: both lines are set at once (TIOCMSET);
    pyserial alone sets them one after the other and the ESP32 boots normally in between."""
    try:
        import fcntl
        import struct
        import termios
    except ImportError:
        return False  # not POSIX: the bridge's own bootloader entry at start is all there is
    dtr, rts = 0x002, 0x004
    with serial.Serial() as link:
        link.port, link.baudrate = port, 115200
        link.dtr = False
        link.rts = False
        link.open()

        def lines(value):
            fcntl.ioctl(link.fd, termios.TIOCMSET, struct.pack("I", value))

        lines(dtr)
        time.sleep(0.15)  # EN low: reset
        lines(rts)
        time.sleep(0.10)  # EN high with IO0 low: ROM bootloader
        lines(0)
    return True


def run_esptool(port, baud, images):
    flash_args = [sys.executable, "-m", "esptool", "--chip", "esp32", "--port", port, "-b", str(baud),
                  "--before", "no_reset", "--after", "no_reset", "write_flash", "--flash_mode", "dio",
                  "--flash_size", "4MB", "--flash_freq", "40m"]
    for offset, path in images:
        flash_args += [offset, path]
    return subprocess.run(flash_args).returncode


def cmd_flash_esp32(args):
    images = esp32_images(args.build_dir)
    system_command("esp32-flash")
    print("Waiting for the ESP32 bridge...")
    port = find_port(BRIDGE_PRODUCT, timeout=15.0)
    if port is None:
        sys.exit("The bridge port did not show up.")
    time.sleep(BRIDGE_GUARD_S)
    returncode = run_esptool(port, args.baud, images)
    if returncode != 0 and bridge_bootloader(port):
        # A failed attempt can leave the ESP32 out of its bootloader or at another rate: back to
        # the bootloader, once more at 115200.
        print("Retrying at 115200 baud...")
        returncode = run_esptool(port, 115200, images)
    with serial.Serial(port, 115200, timeout=0.5) as link:
        link.write(COMPLETE_FLAG)  # the RP2040 restarts in its mode and restarts the ESP32
        link.flush()
    if returncode != 0:
        sys.exit("esptool failed; the adapter was sent back to its mode.")
    print("ESP32 flashed; the adapter restarts in its mode.")


class _Stop(Exception):
    pass


def _stop_on_signal(_signum, _frame):
    raise _Stop()


def cmd_monitor(args):
    # Already in monitor mode (an earlier run that could not finish): read it again.
    port = find_port(MONITOR_PRODUCT)
    if port is None:
        system_command("esp32-monitor")
        port = find_port(MONITOR_PRODUCT, timeout=15.0)
    if port is None:
        sys.exit("The monitor port did not show up.")
    out = open(args.log, "ab") if args.log else None
    # Killed or the terminal closed: still send the adapter back to its mode (finally below).
    for sig in (signal.SIGTERM, getattr(signal, "SIGHUP", None)):
        if sig is not None:
            signal.signal(sig, _stop_on_signal)
    until = time.monotonic() + args.seconds if args.seconds else None
    print("Kept log first, then live output. " + (f"{args.seconds} s." if until else "Ctrl+C to stop.") + "\n")
    try:
        with serial.Serial(port, 115200, timeout=0.2) as link:
            try:
                while until is None or time.monotonic() < until:
                    data = link.read(512)
                    if data:
                        sys.stdout.write(data.decode("utf-8", "replace"))
                        sys.stdout.flush()
                        if out:
                            out.write(data)
            except (KeyboardInterrupt, _Stop):
                pass
            finally:
                link.write(COMPLETE_FLAG)  # back to the adapter's mode
                link.flush()
    finally:
        if out:
            out.close()
    print("\nThe adapter restarts in its mode.")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("ports").set_defaults(func=cmd_ports)
    sub.add_parser("reboot").set_defaults(func=cmd_reboot)
    p = sub.add_parser("bootsel")
    p.add_argument("--uf2")
    p.set_defaults(func=cmd_bootsel)
    p = sub.add_parser("flash-esp32")
    p.add_argument("build_dir")
    p.add_argument("--baud", type=int, default=460800,
                   help="esptool's rate through the bridge (default 460800); a failed attempt is retried at 115200")
    p.set_defaults(func=cmd_flash_esp32)
    p = sub.add_parser("monitor")
    p.add_argument("--log")
    p.add_argument("--seconds", type=float, default=0, help="stop after this long (default: until Ctrl+C)")
    p.set_defaults(func=cmd_monitor)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
