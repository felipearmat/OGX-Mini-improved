#ifndef _OGXM_DEBUG_PROBE_H_
#define _OGXM_DEBUG_PROBE_H_

#include <stddef.h>
#include <stdint.h>

/* Custom: the debug probe point, a UART TX pin (OGXM_DEBUG_UART_TX_PIN, GP4 by default where it is
 * free) that streams the activity log while it is on (the web app's "Activity log" option).
 *
 * The adapter only copies small binary records into a RAM ring; a DMA channel sends them on the
 * UART, so no CPU time goes per byte and nothing waits for the wire. A full ring drops the record
 * and counts it. The PC (Tools/bench/probe.py) decodes the records and works out the rest: gaps,
 * rates, late reports, sessions. Read with any 3.3 V USB-UART adapter (or an ESP32 board held in
 * reset, its USB chip reading the pin on TX0) at OGXM_PROBE_BAUD, 8N1. Nothing attached costs
 * nothing: the records go out either way.
 *
 * Frame (little endian): 0xA5, type, payload length, check, time (µs since boot, 32 bits),
 * payload. The check makes the sum of every byte after 0xA5 a multiple of 256. */
#define OGXM_PROBE_BAUD 921600u

enum {
    PROBE_HELLO = 0x01,   // once a second: u32 frames sent, u32 frames dropped, u32 ring bytes, text "version build board"
    PROBE_TEXT = 0x02,    // a piece of the kept log: source letter ('r' rp, 'e' esp), then the text
    PROBE_REPORT = 0x03,  // a controller's input report: u8 slot
    PROBE_USB_SENT = 0x04,  // the host read a report from the adapter (no payload)
};

#ifdef __cplusplus
extern "C" {
#endif

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE && defined(OGXM_DEBUG_UART_TX_PIN) && OGXM_DEBUG_UART_TX_PIN >= 0
#define OGXM_PROBE 1
/* Each returns at once while the activity log is off. Any core, IRQs allowed. */
void probe_record(uint8_t type, const void* payload, size_t len);
void probe_text(const char* source, const char* text, size_t len);
/* Sends what waits and the once-a-second header; called from the boards' loops. */
void probe_tick(void);
#else
static inline void probe_record(uint8_t type, const void* payload, size_t len) { (void)type; (void)payload; (void)len; }
static inline void probe_text(const char* source, const char* text, size_t len) { (void)source; (void)text; (void)len; }
static inline void probe_tick(void) {}
#endif

#ifdef __cplusplus
}
#endif

#endif // _OGXM_DEBUG_PROBE_H_
