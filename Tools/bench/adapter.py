"""The adapter over its USB serial port (D-Input/Debug mode), for the bench tests.

Packet IDs and lengths come from the firmware's contract file
(Firmware/common/contract/webapp_contract.json), the same one the web app is tested against.
"""
import json
import pathlib
import time

import serial
from serial.tools import list_ports

REPO = pathlib.Path(__file__).resolve().parents[2]
CONTRACT = json.loads((REPO / "Firmware/common/contract/webapp_contract.json").read_text())
IDS = CONTRACT["packet"]["ids"]
SYSTEM_COMMANDS = CONTRACT["packet"]["system"]
PACKET_LEN = CONTRACT["packet"]["length"]
HEADER_LEN = CONTRACT["packet"]["header_length"]
DINPUT = CONTRACT["modes"]["DINPUT_DEBUG"]

VID = 0xCAFE
PRODUCT = "OGX-Mini"                     # D-Input/Debug (the bridge and monitor ports have their own names)
MONITOR_PRODUCT = "OGX-Mini ESP32 monitor"
COMPLETE_FLAG = b"PROGRAMMING_COMPLETE"


def find_port(product=PRODUCT, timeout=0.0):
    deadline = time.monotonic() + timeout
    while True:
        try:
            found = list_ports.comports()
        except (TypeError, ValueError, OSError):
            found = []  # a device left mid-listing (logger.py ports): try again
        for p in found:
            if p.vid == VID and (p.product or "") == product:
                return p.device
        if time.monotonic() >= deadline:
            return None
        time.sleep(0.25)


class Adapter:
    def __init__(self, port):
        self.port = port
        # exclusive: an flock on the port, so the logger (logger.py), the bench and Tools/hybrid
        # never read the same port at once (each one's bytes would go to the other).
        self.link = serial.Serial(port, 115200, timeout=0.05, exclusive=True)

    @classmethod
    def open(cls, timeout=30.0):
        port = find_port(timeout=timeout)
        if port is None:
            raise RuntimeError("no adapter in D-Input/Debug mode on USB")
        deadline = time.monotonic() + 5
        while True:
            try:
                return cls(port)
            except serial.SerialException:
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.3)

    def close(self):
        try:
            self.link.close()
        except Exception:
            pass

    # ---- packets ----

    def send(self, packet_id, data=b"\xff", chunks_total=1, chunk_idx=0, mode=DINPUT):
        packet = bytearray(PACKET_LEN)
        packet[0:HEADER_LEN] = bytes([PACKET_LEN, packet_id, mode, 1, 0, 0, chunks_total, chunk_idx, len(data)])
        packet[HEADER_LEN:HEADER_LEN + len(data)] = data
        self.link.write(packet)

    def request(self, name, data=b"\xff", wait=2.0, reply=None):
        """Send a packet and return the data of its answer (all chunks), or None. The answer has the
        same ID unless `reply` names another (SET_LED_COLORS is answered with GET_LED_COLORS)."""
        self.link.reset_input_buffer()
        self.send(IDS[name], data)
        packet_id = IDS[reply or name]
        buffer = b""
        out = b""
        deadline = time.monotonic() + wait
        while time.monotonic() < deadline:
            buffer += self.link.read(4096)
            while len(buffer) >= PACKET_LEN:
                # a packet starts with its length and has a chunk length that fits
                if buffer[0] != PACKET_LEN or buffer[8] > PACKET_LEN - HEADER_LEN or buffer[6] == 0:
                    buffer = buffer[1:]
                    continue
                pkt, buffer = buffer[:PACKET_LEN], buffer[PACKET_LEN:]
                if pkt[1] == IDS["RESP_ERROR"]:
                    return None
                if pkt[1] != packet_id:
                    continue  # live input and touchpad packets
                chunks_total, chunk_idx, chunk_len = pkt[6], pkt[7], pkt[8]
                out += pkt[HEADER_LEN:HEADER_LEN + chunk_len]
                if chunk_idx + 1 >= max(chunks_total, 1):
                    return out
        return None

    def diagnostics(self):
        data = self.request("GET_DIAGNOSTICS", wait=4.0)
        return json.loads(data) if data else None

    def log_since(self, since):
        """GET_LOG: the kept log from position `since` (Board/LogRing.h). Returns (start, total,
        bytes): `start` > `since` when bytes were dropped; None on a firmware without it."""
        data = self.request("GET_LOG", int(since).to_bytes(4, "little"), wait=3.0)
        if not data or len(data) < 8:
            return None
        return int.from_bytes(data[0:4], "little"), int.from_bytes(data[4:8], "little"), data[8:]

    def flash_log_page(self, from_seq):
        """GET_FLASH_LOG: the oldest page of the log kept in flash with seq >= from_seq. Returns
        a dict (seq 0 = none from there; ring_gen_now = the running ring generation), or None on
        a firmware without it."""
        data = self.request("GET_FLASH_LOG", int(from_seq).to_bytes(4, "little"), wait=3.0)
        if not data or len(data) < 22:
            return None
        u32 = lambda o: int.from_bytes(data[o:o + 4], "little")
        return {"seq": u32(0), "boot": int.from_bytes(data[4:6], "little"), "ring_gen": u32(6),
                "ring_pos": u32(10), "uptime_s": u32(14), "ring_gen_now": u32(18), "text": data[22:]}

    def capabilities(self):
        data = self.request("GET_CAPABILITIES")
        return int.from_bytes(data, "little") if data else None

    def version(self):
        data = self.request("GET_VERSION")
        return data.decode(errors="replace").split("\0") if data else None

    def dongle_settings(self):
        return self.request("GET_DONGLE_SETTINGS")

    def set_dongle_settings(self, record):
        """SET_DONGLE_SETTINGS; a change of live options only is applied without a restart (no answer)."""
        self.send(IDS["SET_DONGLE_SETTINGS"], bytes(record))

    def led_colors(self):
        return self.request("GET_LED_COLORS")

    def set_led_colors(self, table):
        return self.request("SET_LED_COLORS", bytes(table), reply="GET_LED_COLORS")

    def led_preview(self, active, rgb=(0, 0, 0)):
        return self.request("LED_PREVIEW", bytes([1 if active else 0, *rgb]))

    def system(self, name):
        self.send(IDS["SYSTEM"], bytes([SYSTEM_COMMANDS[name]]))
        time.sleep(0.3)

    # ---- state ----

    def companion(self):
        report = self.diagnostics()
        return (report or {}).get("companion") or {}

    def wait_companion(self, timeout=30.0, min_uptime_s=5):
        """Hybrid: wait until the ESP32 talks and has been up a few seconds (after a restart)."""
        deadline = time.monotonic() + timeout
        companion = {}
        while time.monotonic() < deadline:
            companion = self.companion()
            if companion.get("last_heard_s", 99) <= 3 and companion.get("uptime_s", 0) >= min_uptime_s:
                return companion
            time.sleep(1)
        raise AssertionError(f"the ESP32 did not come up: {companion}")

    def uptime_s(self):
        report = self.diagnostics()
        return report["uptime_ms"] / 1000 if report else None

    def wait_back(self, timeout=30.0):
        """After a restart: a new Adapter once the D-Input/Debug port is back."""
        self.close()
        time.sleep(1.5)
        return Adapter.open(timeout=timeout)
