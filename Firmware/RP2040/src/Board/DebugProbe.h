#ifndef _OGXM_DEBUG_PROBE_H_
#define _OGXM_DEBUG_PROBE_H_

#include <stdbool.h>
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
 * Its own cost is measured (each core's SysTick, a free-running cycle counter here) and sent once a
 * second, so every session says what the probe took.
 *
 * Frame (little endian): 0xA5, type, payload length, check, time (µs since boot, 32 bits),
 * payload. The check makes the sum of every byte after 0xA5 a multiple of 256. */
#define OGXM_PROBE_BAUD 921600u

enum {
    PROBE_HELLO = 0x01,   // once a second: u32 frames sent, u32 frames dropped, u32 ring bytes, text "version build board"
    PROBE_TEXT = 0x02,    // a piece of the kept log: source letter ('r' rp, 'e' esp), then the text
    PROBE_REPORT = 0x03,  // a controller's input report: u8 slot
    PROBE_USB_SENT = 0x04,  // the host read a report from the adapter (no payload)
    PROBE_LATENCY = 0x05,   // the newest controller input reached the output driver: u32 µs it waited
    PROBE_RUMBLE = 0x06,    // the host's rumble changed: u8 left, u8 right (0-255)
    PROBE_COST = 0x07,      // once a second, the probe's own cost over the last second: u32 cycles
                            // spent recording (both cores), u32 most cycles for one frame, u32 frames,
                            // u32 CPU clock (Hz); then u32 cycles in the activity
                            // log's hooks (their own probe frames included), u32 most for one call
    PROBE_PAD_CLOCK = 0x08, // a controller report's own clock: u8 slot, u8 kind (diag::PadClock), u32 raw
    PROBE_PAD_COMMAND = 0x09,  // a Switch subcommand: u8 slot, u8 subcommand ID, u8 0 sent / 1 reply
    PROBE_ACK = 0x0B,       // a command from the PC was carried out: u8 command, u8 a, u8 b (sent
                            // whatever the log options are)
    PROBE_HD_RUMBLE = 0x0C, // a Switch HD Rumble report on its way: u8 stage (0 from the host, queued;
                            // 1 the oldest dropped, queue full; 2 taken for the pad, or for the
                            // hybrid's ESP32; 3 the adapter's own stop; 4 discarded, stale), u8 queue
                            // depth after it (0xFF unknown), the 8 blocks (left 4, right 4)
    PROBE_LINK = 0x0A,      // a detailed link reading (detailed controller diagnostics): u8 slot (0xFF
                            // not a controller's yet), u8 what (1 RSSI, 2 channels, 3 failed contacts,
                            // 4 transmit power, 5 its maximum, 6 link mode), i16 value, u16 second value
};

/* The command channel: the same UART's RX pin (OGXM_DEBUG_UART_RX_PIN, GP5 beside GP4) takes short
 * commands from the PC (Tools/bench/probe.py), in every output mode, once the UART is up (the
 * activity log was on at some point since the adapter started). A command is 8 bytes: "OGX!",
 * command, a, b, check (the sum of the last four bytes a multiple of 256). Anything else is
 * ignored, so a loose wire or noise does nothing. Carried out from the boards' mode combo check
 * (UserSettings::check_for_driver_change), and acknowledged with PROBE_ACK. */
enum {
    PROBE_CMD_OPTIONS = 'O',  // a = log level (0 none, 1 activity, 2 flash), b = detailed diagnostics:
                              // applied live, in RAM only (a restart brings back the saved options)
    PROBE_CMD_MODE = 'M',     // a = output mode (DeviceDriverType): stored, the adapter restarts in it
};

typedef struct {
    uint8_t cmd, a, b;
} probe_command_t;

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
/* The activity log's own cost: this core's cycle count (counting down), and the cycles one hook
 * call took, added to the next PROBE_COST. */
uint32_t probe_cycles(void);
void probe_log_cost(uint32_t start_cycles);
/* The command the PC sent, if one came; and its acknowledgement. */
bool probe_take_command(probe_command_t* out);
void probe_ack(const probe_command_t* command);
#else
static inline bool probe_take_command(probe_command_t* out) { (void)out; return false; }
static inline void probe_ack(const probe_command_t* command) { (void)command; }
static inline uint32_t probe_cycles(void) { return 0; }
static inline void probe_log_cost(uint32_t start_cycles) { (void)start_cycles; }
static inline void probe_record(uint8_t type, const void* payload, size_t len) { (void)type; (void)payload; (void)len; }
static inline void probe_text(const char* source, const char* text, size_t len) { (void)source; (void)text; (void)len; }
static inline void probe_tick(void) {}
#endif

#ifdef __cplusplus
}
#endif

#endif // _OGXM_DEBUG_PROBE_H_
