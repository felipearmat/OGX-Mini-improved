"""RP2040 + ESP32 hybrid: the ESP32 link, and leaving the log monitor back to D-Input/Debug (the
adapter once stayed off USB after a bridge or monitor session)."""
import time

import pytest
import serial

from adapter import Adapter, find_port, MONITOR_PRODUCT, COMPLETE_FLAG


@pytest.fixture(autouse=True)
def only_hybrid(hybrid):
    if not hybrid:
        pytest.skip("needs the RP2040 + ESP32 hybrid")


def test_esp32_talks_and_reports_its_version(adapter):
    deadline = time.monotonic() + 15
    companion = {}
    while time.monotonic() < deadline:
        companion = adapter.diagnostics().get("companion") or {}
        if companion.get("last_heard_s", 99) <= 3 and companion.get("firmware"):
            break
        time.sleep(1)
    assert companion.get("last_heard_s", 99) <= 3, f"no ESP32 on I2C: {companion}"
    assert companion.get("i2c_write_errors", 0) == 0, companion


@pytest.mark.slow
def test_log_monitor_returns_to_dinput(adapter):
    adapter.system("SYSTEM_ESP32_MONITOR")
    adapter.close()
    port = find_port(MONITOR_PRODUCT, timeout=20)
    assert port, "the monitor port did not show up"
    with serial.Serial(port, 115200, timeout=0.2) as link:
        time.sleep(2)
        link.read(65536)
        link.write(COMPLETE_FLAG)
        link.flush()
    back = Adapter.open(timeout=40)
    try:
        assert back.diagnostics()["output_mode"] == "DINPUT_DEBUG"
    finally:
        back.close()
