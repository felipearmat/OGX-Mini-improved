"""logger.py against the firmware's virtual adapter (no board needed): its serial port on a
pseudo-terminal, as a USB serial port would be. Needs the virtual adapter built:
OGXM_VIRTUAL_ADAPTER=<binary>, or CMake to build it here."""
import json
import os
import pathlib
import socket
import subprocess
import sys
import time
import tty

import pytest

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
LOGGER = HERE / "logger.py"


@pytest.fixture(scope="session")
def virtual_adapter_binary(tmp_path_factory):
    if os.environ.get("OGXM_VIRTUAL_ADAPTER"):
        return os.environ["OGXM_VIRTUAL_ADAPTER"]
    source = REPO / "Firmware/RP2040/tests/virtual_adapter"
    build = tmp_path_factory.mktemp("virtual-adapter")
    try:
        subprocess.run(["cmake", "-S", source, "-B", build, "-DCMAKE_BUILD_TYPE=Release"], check=True,
                       capture_output=True)
        subprocess.run(["cmake", "--build", build, "--parallel"], check=True, capture_output=True)
    except (OSError, subprocess.CalledProcessError) as error:
        pytest.skip(f"virtual adapter not built: {error}")
    return str(build / "virtual_adapter")


class VirtualPort:
    """The virtual adapter with its serial port on a pty; `device` is the port a host opens."""

    def __init__(self, binary, flash):
        self.flash = flash
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)  # no echo: bytes go through unchanged, as on a USB serial port
        self.device = os.ttyname(self.slave)
        ours, theirs = socket.socketpair()
        self.control = ours
        os.set_inheritable(theirs.fileno(), True)
        self.proc = subprocess.Popen(
            [binary, str(flash)], stdin=self.master, stdout=self.master, pass_fds=[theirs.fileno()],
            env={**os.environ, "OGXM_CONTROL_FD": str(theirs.fileno())})
        theirs.close()
        self.send("D1")  # the port is open (a host keeps it open on a real board too)

    def send(self, command):
        self.control.sendall(f"{command}\n".encode())
        time.sleep(0.05)

    def close(self):
        if self.proc is None:
            return
        self.proc.kill()
        self.proc.wait()
        self.proc = None
        for fd in (self.master, self.slave):
            os.close(fd)
        self.control.close()


@pytest.fixture
def port(virtual_adapter_binary, tmp_path):
    p = VirtualPort(virtual_adapter_binary, tmp_path / "flash.bin")
    time.sleep(0.3)
    yield p
    p.close()


def run_logger(port, log_dir, *extra):
    subprocess.run([sys.executable, str(LOGGER), "--dir", str(log_dir), "--port", port.device, "--once",
                    "--poll", "0", "--report", "0", *extra], check=True, timeout=30)
    text = "".join(p.read_text() for p in sorted(log_dir.glob("*.log")))
    return text


def test_kept_log_and_first_report_are_recorded(port, tmp_path):
    port.send("Lesp32: I (321) bluepad32 started")
    text = run_logger(port, tmp_path / "logs")
    assert "[esp] esp32: I (321) bluepad32 started" in text
    assert "[rp] " in text and "boot DINPUT_DEBUG" in text  # the boot event, from the kept log
    assert "[usb] adapter on USB: D-Input/Debug" in text
    reports = list((tmp_path / "logs/reports").glob("*_boot*.json"))
    assert reports and json.loads(reports[0].read_text())["board"] == "PI_PICO2W_VIRTUAL"


def test_each_line_is_recorded_once_across_runs(port, tmp_path):
    logs = tmp_path / "logs"
    port.send("Lfirst line")
    run_logger(port, logs)
    port.send("Lsecond line")
    text = run_logger(port, logs)
    assert text.count("first line") == 1
    assert text.count("second line") == 1
    assert len(list((logs / "reports").glob("*.json"))) == 1  # the same boot: no new report


def test_dropped_bytes_are_reported(port, tmp_path):
    logs = tmp_path / "logs"
    run_logger(port, logs)
    for i in range(500):  # about 50 KB, more than the 32 KB the RP2350 keeps
        port.control.sendall(f"L{i:04d} {'x' * 90}\n".encode())
    time.sleep(1.0)
    text = run_logger(port, logs)
    assert "were dropped before they could be read" in text
    assert "0499 " in text  # the newest lines arrive


def test_old_files_are_cleaned_up(port, tmp_path):
    logs = tmp_path / "logs"
    logs.mkdir()
    old = logs / "2020-01-01.log"
    old.write_text("old\n")
    stamp = time.time() - 30 * 86400
    os.utime(old, (stamp, stamp))
    run_logger(port, logs, "--keep-days", "14")
    assert not old.exists()
    assert "cleanup: 1 old file(s) removed" in "".join(p.read_text() for p in logs.glob("*.log"))


def test_flash_log_fills_in_what_happened_away_from_the_computer(port, tmp_path, virtual_adapter_binary):
    """With "keep the log in flash" on, what the adapter logged before being unplugged reaches the
    files after it is back, once, and what was read from its RAM is not repeated."""
    import adapter as A
    logs = tmp_path / "logs"
    a = A.Adapter(port.device)
    record = bytearray(a.dongle_settings())
    record[A.CONTRACT["dongle_settings"]["offsets"]["keep_log_in_flash"]] = 1
    a.set_dongle_settings(record)
    a.close()
    port.send("Lread live first")
    run_logger(port, logs)
    port.send("Llogged away from the computer")
    time.sleep(7)  # quiet: written to flash once the log stops growing for 5 s
    # Unplugged and back: a new process on the same flash file (RAM gone).
    flash = port.flash
    port.close()
    again = VirtualPort(virtual_adapter_binary, flash)
    try:
        time.sleep(0.3)
        text = run_logger(again, logs)
    finally:
        again.close()
    assert text.count("read live first") == 1
    assert text.count("logged away from the computer") == 1
    assert "[flash] kept in flash, not read before" in text


class FakeAdapter:
    """Kept log text at ring positions (GET_LOG) and flash pages, without a serial port."""

    def __init__(self, ring, pages, gen=7):
        self.ring = ring            # bytes of the RAM ring from position 0
        self.pages = pages          # [(ring_pos, text)] of this ring generation, in order
        self.gen = gen

    def log_since(self, since):
        data = self.ring[since:since + 256]
        return since, len(self.ring), data

    def flash_log_page(self, seq):
        if seq < 1 or seq > len(self.pages):
            return {"seq": 0, "ring_gen_now": self.gen}
        pos, text = self.pages[seq - 1]
        return {"seq": seq, "boot": 1, "ring_gen": self.gen, "ring_gen_now": self.gen, "ring_pos": pos, "text": text}


def test_a_line_split_between_a_flash_page_and_the_ram_keeps_its_source(tmp_path):
    """A page kept in flash that ends inside a line, its rest still in RAM: one line, its own
    source (it used to end at the page and its rest come out as "[adapter]")."""
    import importlib
    logger = importlib.import_module("logger")
    ring = b"[rp] 1.0 boot\n[esp] BP32: TX power: default (+3 dBm)\n[rp] 2.0 ready\n"
    cut = ring.index(b"default")
    args = type("Args", (), {"dir": str(tmp_path), "echo": False})()
    log = logger.Logger(args)
    adapter = FakeAdapter(ring, [(0, ring[:cut])])
    log.state["ring_gen"] = None
    log.read_flash(adapter)
    log.read_log(adapter)
    text = next(tmp_path.glob("*.log")).read_text()
    assert "[esp] BP32: TX power: default (+3 dBm)" in text
    assert "[adapter]" not in text
    assert text.count("BP32: TX power") == 1
