#include "fake_bt.h"

#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

#include "bt/uni_bt_conn.h"
#include "btstack_run_loop.h"
#include "parser/uni_hid_parser_switch.h"
#include "uni_hid_device.h"

const int AXIS_NORMALIZE_RANGE = 1024;

static uni_hid_device_t s_devices[CONFIG_BLUEPAD32_MAX_DEVICES];
static bool s_ready[CONFIG_BLUEPAD32_MAX_DEVICES];
static uint32_t s_now_ms;

#define FAKE_MAX_TIMERS 32
static btstack_timer_source_t* s_timers[FAKE_MAX_TIMERS];
static int s_timer_count;

static fake_sent_t s_sent[FAKE_MAX_SENT];
static int s_sent_count;

// ---------------------------------------------------------------- fake API

void fake_reset(void) {
    memset(s_devices, 0, sizeof(s_devices));
    memset(s_ready, 0, sizeof(s_ready));
    memset(s_timers, 0, sizeof(s_timers));
    s_timer_count = 0;
    s_sent_count = 0;
    s_now_ms = 1000;
    for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; i++) {
        s_devices[i].conn.handle = (hci_con_handle_t)(0x40 + i);
        s_devices[i].report_parser.setup = uni_hid_parser_switch_setup;
    }
}

uni_hid_device_t* fake_device(int idx) {
    return &s_devices[idx];
}

static int timer_index(btstack_timer_source_t* ts) {
    for (int i = 0; i < s_timer_count; i++)
        if (s_timers[i] == ts)
            return i;
    return -1;
}

void fake_advance_ms(uint32_t ms) {
    const uint32_t end = s_now_ms + ms;
    for (;;) {
        int due = -1;
        for (int i = 0; i < s_timer_count; i++)
            if (s_timers[i]->timeout <= end && (due < 0 || s_timers[i]->timeout < s_timers[due]->timeout))
                due = i;
        if (due < 0)
            break;
        btstack_timer_source_t* ts = s_timers[due];
        s_now_ms = ts->timeout > s_now_ms ? ts->timeout : s_now_ms;
        s_timers[due] = s_timers[--s_timer_count];
        ts->process(ts);
    }
    s_now_ms = end;
}

int fake_timer_count(void) {
    return s_timer_count;
}

bool fake_timer_pending_for_context(const void* context) {
    for (int i = 0; i < s_timer_count; i++)
        if (s_timers[i]->context == context)
            return true;
    return false;
}

int fake_sent_count(void) {
    return s_sent_count;
}

const fake_sent_t* fake_sent(int i) {
    return &s_sent[i];
}

int fake_sent_subcmd(int i) {
    const fake_sent_t* p = &s_sent[i];
    // [0] transaction, [1] report id, [2] packet num, [3..10] rumble, [11] subcmd, [12..] data
    if (p->len < 12 || p->bytes[1] != 0x01)
        return -1;
    return p->bytes[11];
}

int fake_count_subcmd(int dev_idx, uint8_t subcmd) {
    int n = 0;
    for (int i = 0; i < s_sent_count; i++)
        if ((dev_idx < 0 || s_sent[i].dev_idx == dev_idx) && fake_sent_subcmd(i) == subcmd)
            n++;
    return n;
}

bool fake_device_ready(int idx) {
    return s_ready[idx];
}

void fake_joycon_reply(int dev_idx, uint8_t subcmd, uint32_t spi_addr, uint8_t spi_len) {
    uint8_t r[64];
    memset(r, 0, sizeof(r));
    r[0] = 0x21;  // subcommand reply
    r[2] = 0x8E;  // battery full, Joy-Con
    r[13] = 0x80 | subcmd;  // ack (bit 7 = success)
    r[14] = subcmd;
    if (subcmd == 0x10) {
        r[15] = spi_addr & 0xff;
        r[16] = (spi_addr >> 8) & 0xff;
        r[17] = (spi_addr >> 16) & 0xff;
        r[18] = (spi_addr >> 24) & 0xff;
        r[19] = spi_len;
    }
    uni_hid_parser_switch_parse_input_report(&s_devices[dev_idx], r, 50);
}

static void reply_to_packet(int dev_idx, const fake_sent_t* p, uint8_t controller_type) {
    const uint8_t subcmd = p->bytes[11];
    if (subcmd == 0x02) {
        uint8_t r[64];
        memset(r, 0, sizeof(r));
        r[0] = 0x21;
        r[2] = 0x8E;
        r[13] = 0x82;
        r[14] = 0x02;
        r[15] = 3;     // firmware hi
        r[16] = 0x8B;  // firmware lo
        r[17] = controller_type;
        uni_hid_parser_switch_parse_input_report(&s_devices[dev_idx], r, 50);
        return;
    }
    if (subcmd == 0x10) {
        const uint32_t addr = p->bytes[12] | p->bytes[13] << 8 | p->bytes[14] << 16 | (uint32_t)p->bytes[15] << 24;
        fake_joycon_reply(dev_idx, subcmd, addr, p->bytes[16]);
        return;
    }
    fake_joycon_reply(dev_idx, subcmd, 0, 0);
}

void fake_joycon_reply_last(int dev_idx, uint8_t controller_type) {
    for (int i = s_sent_count - 1; i >= 0; i--) {
        if (s_sent[i].dev_idx == dev_idx && fake_sent_subcmd(i) >= 0) {
            fake_sent_t copy = s_sent[i];
            reply_to_packet(dev_idx, &copy, controller_type);
            return;
        }
    }
}

void fake_joycon_run_setup(int dev_idx, uint8_t controller_type, int max_steps) {
    for (int step = 0; step < max_steps && !s_ready[dev_idx]; step++)
        fake_joycon_reply_last(dev_idx, controller_type);
}

// ------------------------------------------------ BTstack run loop (manual clock)

void btstack_run_loop_set_timer(btstack_timer_source_t* ts, uint32_t timeout_in_ms) {
    ts->timeout = s_now_ms + timeout_in_ms;
}
void btstack_run_loop_set_timer_handler(btstack_timer_source_t* ts, void (*process)(btstack_timer_source_t*)) {
    ts->process = process;
}
void btstack_run_loop_set_timer_context(btstack_timer_source_t* ts, void* context) {
    ts->context = context;
}
void* btstack_run_loop_get_timer_context(btstack_timer_source_t* ts) {
    return ts->context;
}
void btstack_run_loop_add_timer(btstack_timer_source_t* ts) {
    if (timer_index(ts) >= 0)
        return;
    if (s_timer_count < FAKE_MAX_TIMERS)
        s_timers[s_timer_count++] = ts;
}
int btstack_run_loop_remove_timer(btstack_timer_source_t* ts) {
    const int i = timer_index(ts);
    if (i < 0)
        return 0;
    s_timers[i] = s_timers[--s_timer_count];
    return 1;
}
uint32_t btstack_run_loop_get_time_ms(void) {
    return s_now_ms;
}

// ---------------------------------------------------------- Bluepad32 device API

int32_t uni_hid_device_get_idx_for_instance(const uni_hid_device_t* d) {
    const long idx = d - s_devices;
    return (idx >= 0 && idx < CONFIG_BLUEPAD32_MAX_DEVICES) ? (int32_t)idx : -1;
}
uni_hid_device_t* uni_hid_device_get_instance_for_idx(int idx) {
    return (idx >= 0 && idx < CONFIG_BLUEPAD32_MAX_DEVICES) ? &s_devices[idx] : NULL;
}
void uni_hid_device_send_intr_report(uni_hid_device_t* d, const uint8_t* report, uint16_t len) {
    if (s_sent_count >= FAKE_MAX_SENT)
        return;
    fake_sent_t* p = &s_sent[s_sent_count++];
    p->dev_idx = uni_hid_device_get_idx_for_instance(d);
    p->len = len > FAKE_MAX_PKT ? FAKE_MAX_PKT : len;
    memcpy(p->bytes, report, (size_t)p->len);
}
bool uni_hid_device_set_ready_complete(uni_hid_device_t* d) {
    s_ready[uni_hid_device_get_idx_for_instance(d)] = true;
    return true;
}
void uni_hid_device_process_controller(uni_hid_device_t* d) {
    (void)d;
}
void uni_hid_device_set_vendor_id(uni_hid_device_t* d, uint16_t vendor_id) {
    d->vendor_id = vendor_id;
}
void uni_hid_device_set_product_id(uni_hid_device_t* d, uint16_t product_id) {
    d->product_id = product_id;
}
uni_bt_conn_state_t uni_bt_conn_get_state(const uni_bt_conn_t* conn) {
    (void)conn;
    return UNI_BT_CONN_STATE_DEVICE_READY;
}
uint8_t uni_hid_parser_hat_to_dpad(uint8_t hat) {
    (void)hat;
    return 0;
}

// ------------------------------------------------------------ radio / scan no-ops

void uni_bt_bredr_scan_start(void) {}
void uni_bt_bredr_scan_stop(void) {}
void uni_bt_le_scan_stop(void) {}
void uni_bt_enable_new_connections_unsafe(bool enabled) {
    (void)enabled;
}
void gap_connectable_control(uint8_t enable) {
    (void)enable;
}
void printf_hexdump(const void* data, int size) {
    (void)data;
    (void)size;
}
