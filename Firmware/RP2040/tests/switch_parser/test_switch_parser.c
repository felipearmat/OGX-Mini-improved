// Bluepad32 Switch parser (with the OGX-Mini-improved patch series) against a fake Joy-Con.
// Regressions covered:
//  - Joy-Cons never vibrated: setup must enable vibration (subcommand 0x48, arg 0x01);
//  - a lost reply stalled setup forever (single one-shot timeout) -> per-step retries;
//  - late replies advanced the setup twice -> stale replies are ignored;
//  - two Joy-Cons reconnecting together collided -> one pad in setup at a time;
//  - wiped parser timers left linked in the run loop -> cleanup unlinks them;
//  - request_sleep sends subcommand 0x06 with arg 0x00.
#include <stdio.h>
#include <string.h>

#include "controller/uni_controller_type.h"
#include "fake_bt.h"
#include "parser/uni_hid_parser_switch.h"

static int s_failures;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

enum { JCL = 1, JCR = 2 };
enum {
    SUB_DEV_INFO = 0x02,
    SUB_SET_HCI = 0x06,
    SUB_SPI_READ = 0x10,
    SUB_REPORT_MODE = 0x03,
    SUB_IMU = 0x40,
    SUB_VIBRATION = 0x48,
    SUB_LEDS = 0x30
};

static void start_joycon(int idx, int type) {
    uni_hid_device_t* d = fake_device(idx);
    d->controller_type = (type == JCL) ? CONTROLLER_TYPE_SwitchJoyConLeft : CONTROLLER_TYPE_SwitchJoyConRight;
    uni_hid_parser_switch_setup(d);
}

static int last_subcmd_of(int dev_idx) {
    for (int i = fake_sent_count() - 1; i >= 0; i--)
        if (fake_sent(i)->dev_idx == dev_idx && fake_sent_subcmd(i) >= 0)
            return fake_sent_subcmd(i);
    return -1;
}

static void test_setup_enables_vibration(void) {
    fake_reset();
    start_joycon(0, JCL);
    fake_joycon_run_setup(0, JCL, 50);
    CHECK(fake_device_ready(0));
    CHECK(fake_count_subcmd(0, SUB_VIBRATION) == 1);
    for (int i = 0; i < fake_sent_count(); i++)
        if (fake_sent_subcmd(i) == SUB_VIBRATION)
            CHECK(fake_sent(i)->bytes[12] == 0x01);  // enable
    // Order: vibration is enabled after the IMU, before the player LEDs.
    int imu = -1, vib = -1, leds = -1;
    for (int i = 0; i < fake_sent_count(); i++) {
        if (fake_sent_subcmd(i) == SUB_IMU && imu < 0) imu = i;
        if (fake_sent_subcmd(i) == SUB_VIBRATION && vib < 0) vib = i;
        if (fake_sent_subcmd(i) == SUB_LEDS && leds < 0) leds = i;
    }
    CHECK(imu >= 0 && vib > imu && leds > vib);
    // Setup finished: no setup timer left behind.
    CHECK(!fake_timer_pending_for_context(fake_device(0)));
}

static void test_lost_reply_is_retried_then_skipped(void) {
    fake_reset();
    start_joycon(0, JCL);
    CHECK(last_subcmd_of(0) == SUB_DEV_INFO);
    CHECK(fake_count_subcmd(0, SUB_DEV_INFO) == 1);
    fake_advance_ms(1000);  // reply lost: first retry
    CHECK(fake_count_subcmd(0, SUB_DEV_INFO) == 2);
    fake_advance_ms(1000);  // second retry
    CHECK(fake_count_subcmd(0, SUB_DEV_INFO) == 3);
    fake_advance_ms(1000);  // give up on this step, move on (third-party pads)
    CHECK(last_subcmd_of(0) == SUB_SPI_READ);
    // It keeps progressing instead of stalling: answer everything from here on.
    fake_joycon_run_setup(0, JCL, 50);
    CHECK(fake_device_ready(0));
}

static void test_stale_reply_does_not_advance(void) {
    fake_reset();
    start_joycon(0, JCL);
    const int sent_before = fake_sent_count();
    // Late reply to some other subcommand while DEV_INFO is pending.
    fake_joycon_reply(0, SUB_REPORT_MODE, 0, 0);
    CHECK(fake_sent_count() == sent_before);
    CHECK(last_subcmd_of(0) == SUB_DEV_INFO);
    // SPI reply for an address that was never requested is ignored too.
    fake_joycon_reply_last(0, JCL);  // real DEV_INFO reply -> factory stick SPI read
    CHECK(last_subcmd_of(0) == SUB_SPI_READ);
    const int sent_mid = fake_sent_count();
    fake_joycon_reply(0, SUB_SPI_READ, 0x8010, 9);  // user-calibration address: not pending yet
    CHECK(fake_sent_count() == sent_mid);
    fake_joycon_run_setup(0, JCL, 50);
    CHECK(fake_device_ready(0));
}

static void test_one_pad_in_setup_at_a_time(void) {
    fake_reset();
    start_joycon(0, JCL);
    start_joycon(1, JCR);  // reconnects while the left one is still in setup
    CHECK(fake_count_subcmd(1, SUB_DEV_INFO) == 0);
    fake_advance_ms(500);  // still waiting (left not ready, not stale)
    CHECK(fake_count_subcmd(1, SUB_DEV_INFO) == 0);
    fake_joycon_run_setup(0, JCL, 50);
    CHECK(fake_device_ready(0));
    fake_advance_ms(200);  // deferred setup starts
    CHECK(fake_count_subcmd(1, SUB_DEV_INFO) == 1);
    fake_joycon_run_setup(1, JCR, 50);
    CHECK(fake_device_ready(1));
}

static void test_stale_owner_does_not_block_forever(void) {
    fake_reset();
    start_joycon(0, JCL);  // never answers anything
    start_joycon(1, JCR);
    fake_advance_ms(6000);  // > owner stale guard
    CHECK(fake_count_subcmd(1, SUB_DEV_INFO) >= 1);
}

static void test_cleanup_unlinks_timers(void) {
    fake_reset();
    start_joycon(0, JCL);  // setup step timer armed
    start_joycon(1, JCR);  // deferral timer armed
    CHECK(fake_timer_count() > 0);
    uni_hid_parser_switch_cleanup(fake_device(0));
    uni_hid_parser_switch_cleanup(fake_device(1));
    CHECK(fake_timer_count() == 0);
}

static void test_request_sleep(void) {
    fake_reset();
    start_joycon(0, JCL);
    fake_joycon_run_setup(0, JCL, 50);
    uni_hid_parser_switch_request_sleep(fake_device(0));
    CHECK(last_subcmd_of(0) == SUB_SET_HCI);
    const fake_sent_t* p = fake_sent(fake_sent_count() - 1);
    CHECK(p->bytes[1] == 0x01);   // subcommand report
    CHECK(p->bytes[12] == 0x00);  // disconnect + sleep
}

int main(void) {
    struct {
        const char* name;
        void (*fn)(void);
    } tests[] = {
        {"setup_enables_vibration", test_setup_enables_vibration},
        {"lost_reply_is_retried_then_skipped", test_lost_reply_is_retried_then_skipped},
        {"stale_reply_does_not_advance", test_stale_reply_does_not_advance},
        {"one_pad_in_setup_at_a_time", test_one_pad_in_setup_at_a_time},
        {"stale_owner_does_not_block_forever", test_stale_owner_does_not_block_forever},
        {"cleanup_unlinks_timers", test_cleanup_unlinks_timers},
        {"request_sleep", test_request_sleep},
    };
    int failed = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        const int before = s_failures;
        tests[i].fn();
        const int ok = s_failures == before;
        printf("[%s] %s\n", ok ? " OK " : "FAIL", tests[i].name);
        failed += ok ? 0 : 1;
    }
    printf("%zu tests, %d failed\n", sizeof(tests) / sizeof(tests[0]), failed);
    return failed ? 1 : 0;
}
