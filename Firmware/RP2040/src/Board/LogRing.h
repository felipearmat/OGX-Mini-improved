#ifndef _OGXM_LOG_RING_H_
#define _OGXM_LOG_RING_H_

#include <stddef.h>
#include <stdint.h>

/* Log text kept in RAM across a reboot that keeps it (mode change, the web app's maintenance
 * commands; not a power cycle), oldest dropped: 32 KB on the RP2350, 8 KB on the RP2040. Every
 * board puts its diagnostics events in it ("[rp] " lines, Diagnostics/Diagnostics.h); the
 * RP2040 + ESP32 hybrid also the ESP32's serial output ("[esp] " lines) and, in Debug builds, the
 * RP2040's own log. Read through the adapter's USB port: the web app protocol's GET_LOG in
 * D-Input/Debug (Tools/bench/logger.py), or the monitor mode of the hybrid's UART bridge. */
#ifdef __cplusplus
extern "C" {
#endif

/* Appends text; `source` ("esp", "rp") starts each new line. Any core, IRQs allowed. */
void log_ring_append(const char* source, const char* text, size_t len);
/* Bytes kept. */
size_t log_ring_size(void);
/* Copies kept bytes from `offset` (0 = oldest); returns the count. */
size_t log_ring_read(size_t offset, uint8_t* out, size_t len);
/* Copies up to `len` bytes from position `since` (bytes ever written, wrapping); `start` gets the
 * position of the first byte copied (later than `since` when bytes were dropped; from the oldest
 * kept one when `since` is ahead, after the ring started over), `total` the position after the
 * newest. Returns the count. */
size_t log_ring_read_since(uint32_t since, uint32_t* start, uint32_t* total, uint8_t* out, size_t len);

/* Host tests: empty, as after a power-on. */
void log_ring_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif // _OGXM_LOG_RING_H_
