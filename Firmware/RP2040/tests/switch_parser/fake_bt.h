// Fake Bluetooth environment for running Bluepad32's Switch parser on the host:
// a manual-clock BTstack run loop, a small device table, and a Joy-Con emulator that
// answers subcommands like the real pad.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "uni_hid_device.h"

#define FAKE_MAX_SENT 256
#define FAKE_MAX_PKT 64

typedef struct {
    int dev_idx;
    uint8_t bytes[FAKE_MAX_PKT];
    int len;
} fake_sent_t;

void fake_reset(void);
uni_hid_device_t* fake_device(int idx);  // idx < CONFIG_BLUEPAD32_MAX_DEVICES
void fake_advance_ms(uint32_t ms);       // fire run-loop timers that became due
int fake_timer_count(void);
bool fake_timer_pending_for_context(const void* context);

int fake_sent_count(void);
const fake_sent_t* fake_sent(int i);
// Subcommand of a sent packet (report 0x01), or -1 for other reports (e.g. rumble-only 0x10).
int fake_sent_subcmd(int i);
// Number of subcommand packets with this id sent by a device (-1 = any device).
int fake_count_subcmd(int dev_idx, uint8_t subcmd);
bool fake_device_ready(int idx);

// Joy-Con emulator: answer the last subcommand this device sent (0x21 reply).
void fake_joycon_reply_last(int dev_idx, uint8_t controller_type);
// Send an arbitrary 0x21 reply for `subcmd` (used to inject stale replies).
void fake_joycon_reply(int dev_idx, uint8_t subcmd, uint32_t spi_addr, uint8_t spi_len);
// Answer every request until the device is ready (or `max_steps` replies).
void fake_joycon_run_setup(int dev_idx, uint8_t controller_type, int max_steps);
