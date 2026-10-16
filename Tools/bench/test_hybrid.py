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
    first = adapter.wait_companion()
    assert first.get("firmware"), first
    time.sleep(5)
    later = adapter.companion()
    # write errors only while the ESP32 starts (before the RP2040's I2C slave answers)
    assert later.get("i2c_write_errors", 0) == first.get("i2c_write_errors", 0), f"I2C write errors: {first} -> {later}"


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
