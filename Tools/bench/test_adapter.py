"""What the adapter must do on its own (no controller): answer, save settings applied live without
a restart and without hanging, keep the colour table, restart and come back on USB."""
import time

import pytest

from adapter import Adapter, CONTRACT

DS = CONTRACT["dongle_settings"]
CAPS = CONTRACT["capabilities"]
OFF = DS["offsets"]


def test_answers_with_version_capabilities_and_report(adapter, caps):
    version = adapter.version()
    assert version and version[0].startswith("improved-"), version
    report = adapter.diagnostics()
    assert report["output_mode"] == "DINPUT_DEBUG"
    assert "watchdog" not in report.get("last_reset", ""), f"last restart: {report.get('last_reset')}"
    assert report.get("last_crash") is None, report.get("last_crash")


def test_board_capabilities_are_consistent(caps, hybrid):
    if hybrid:
        assert caps & CAPS["STATUS_LED_ESP32"] and caps & CAPS["STATUS_LED_ESP32_EXT"]
        assert caps & CAPS["HIGH_TX_POWER"]
    assert caps & CAPS["MODE_COLORS"]


def test_live_settings_save_without_restart_or_hang(adapter, hybrid):
    """The settings applied live (search times, combos, idle time, transmit power, status LEDs)
    saved many times in a row: the adapter keeps running (no restart, no hang-watchdog reset)
    and, on the hybrid, the ESP32 keeps talking. Restores the original record."""
    original = adapter.dongle_settings()
    assert original and original[0] == DS["version"] and len(original) == DS["length"]
    esp32_start = adapter.wait_companion()["uptime_s"] if hybrid else None
    start = adapter.uptime_s()
    try:
        for i in range(12):
            record = bytearray(original)
            record[OFF["search_times"]] = 30 + i                    # full search 30..41 s
            record[OFF["status_led_pico_ext"]] = i % 2
            record[OFF["status_led_esp32_ext"]] = (i + 1) % 2
            record[OFF["status_led_brightness"]] = 40 + 10 * i
            adapter.set_dongle_settings(record)
            time.sleep(0.4)
            assert adapter.dongle_settings() == bytes(record), f"save {i} not applied"
        time.sleep(6)  # longer than the hang watchdog (5 s)
        now = adapter.uptime_s()
        assert now is not None and now > start + 6, f"the adapter restarted (uptime {start} -> {now})"
        if hybrid:
            companion = adapter.companion()
            assert companion.get("last_heard_s", 99) <= 5, f"the ESP32 went silent: {companion}"
            assert companion.get("uptime_s", 0) >= esp32_start + 6, f"the ESP32 restarted: {companion}"
    finally:
        adapter.set_dongle_settings(original)
        time.sleep(0.5)


def test_colour_table_round_trip_and_preview(adapter):
    mc = CONTRACT["mode_colors"]
    original = adapter.led_colors()
    assert original and original[0] == mc["version"] and len(original) == mc["length"]
    try:
        table = bytearray(original)
        switch = CONTRACT["modes"]["SWITCH"]
        table[1 + 3 * switch:4 + 3 * switch] = bytes([1, 2, 3])
        assert adapter.set_led_colors(table) == bytes(table)
        assert adapter.led_colors() == bytes(table)
        assert adapter.led_preview(True, (10, 20, 30)) is not None
        assert adapter.led_preview(False) is not None
        start = adapter.uptime_s()
        time.sleep(6)
        assert adapter.uptime_s() > start + 5, "restarted after colour saves"
    finally:
        adapter.set_led_colors(original)


@pytest.mark.slow
def test_restart_comes_back_on_usb(adapter):
    adapter.system("SYSTEM_REBOOT")
    back = adapter.wait_back(timeout=30)
    try:
        report = back.diagnostics()
        assert report["uptime_ms"] < 30000
        assert "watchdog timeout" not in report.get("last_reset", "")
    finally:
        back.close()
