#ifndef _OGXM_LOG_RING_H_
#define _OGXM_LOG_RING_H_

#include <stddef.h>
#include <stdint.h>

/* Log text kept in RAM across a reboot that keeps it (mode change, the web app's maintenance
 * commands; not a power cycle), oldest dropped. On the RP2040 + ESP32 hybrid it holds the ESP32's
 * serial output ("[esp] " lines) and, in Debug builds, the RP2040's own log ("[rp] " lines), so
 * both can be read through the adapter's USB port (the monitor mode of the UART bridge). */
#ifdef __cplusplus
extern "C" {
#endif

/* Appends text; `source` ("esp", "rp") starts each new line. Any core, IRQs allowed. */
void log_ring_append(const char* source, const char* text, size_t len);
/* Bytes kept. */
size_t log_ring_size(void);
/* Copies kept bytes from `offset` (0 = oldest); returns the count. */
size_t log_ring_read(size_t offset, uint8_t* out, size_t len);

#ifdef __cplusplus
}
#endif

#endif // _OGXM_LOG_RING_H_
