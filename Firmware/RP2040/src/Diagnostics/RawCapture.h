#ifndef _OGXM_RAW_CAPTURE_H_
#define _OGXM_RAW_CAPTURE_H_

#include <cstddef>
#include <cstdint>

/* Custom: the controllers' raw Bluetooth input reports, as Bluepad32 receives them, streamed to the
 * PC over the D-Input/Debug serial port (web app protocol RAW_CAPTURE / RAW_REPORT). Recorded for
 * end-to-end tests (Tools/bench/capture_pad.py, Firmware/RP2040/tests/fixtures) and the debug
 * mode's raw view. Pico W / Pico 2 W only; off unless asked, never stored.
 *
 * A record: version (1), Bluetooth slot, Bluepad32 controller type, VID, PID (uint16 LE each), the
 * adapter's time in ms (uint32 LE), records dropped before this one (uint16 LE: the ring was full),
 * the report length, then the report (its first byte the report ID; Switch subcommand replies
 * 0x21 included, the parser's setup reads them). */
namespace raw_capture {

    constexpr uint8_t kVersion = 1;
    constexpr size_t kHeader = 14;
    constexpr size_t kMaxReport = 100;
    constexpr size_t kMaxRecord = kHeader + kMaxReport;

    void set_enabled(bool on);
    bool enabled();

    /* BT core (Bluepad32's diagnostics hook): one report. Drops it when the ring is full. */
    void on_input_report(uint8_t slot, uint8_t controller_type, uint16_t vid, uint16_t pid,
                         uint32_t now_ms, const uint8_t* report, size_t len);

    /* USB core: the next record into `out` (at least kMaxRecord bytes); its length, 0 if none. */
    size_t take(uint8_t* out);

} // namespace raw_capture

#endif // _OGXM_RAW_CAPTURE_H_
