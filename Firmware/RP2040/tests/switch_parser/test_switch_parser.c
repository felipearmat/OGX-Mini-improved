// Bluepad32 Switch parser (with the OGX-Mini-improved patch series) against a fake Joy-Con.
// Regressions covered:
//  - Joy-Cons never vibrated: setup must enable vibration (subcommand 0x48, arg 0x01);
//  - a lost reply stalled setup forever (single one-shot timeout) -> per-step retries;
//  - late replies advanced the setup twice -> stale replies are ignored;
//  - two Joy-Cons reconnecting together collided -> one pad in setup at a time;
//  - wiped parser timers left linked in the run loop -> cleanup unlinks them;
//  - request_sleep sends subcommand 0x06 with arg 0x00;
//  - rumble stuck on after a lost "stop": idle refresh re-sends neutral rumble;
//  - a merged Joy-Con pair keeps only the selected half's IMU on (right by default);
//  - rumble magnitude was encoded as frequency (fixed amplitude) instead of amplitude.
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

static int last_imu_arg(int dev_idx) {
    for (int i = fake_sent_count() - 1; i >= 0; i--)
        if (fake_sent(i)->dev_idx == dev_idx && fake_sent_subcmd(i) == SUB_IMU)
            return fake_sent(i)->bytes[12];
    return -1;
}

static void pair_both(void) {
    start_joycon(0, JCL);
    fake_joycon_run_setup(0, JCL, 50);
    start_joycon(1, JCR);
    fake_joycon_run_setup(1, JCR, 50);
}

static void test_pair_imu_defaults_to_right(void) {
    fake_reset();
    uni_hid_parser_switch_set_pair_imu_side(true);
    pair_both();
    CHECK(fake_device_ready(0) && fake_device_ready(1));
    CHECK(uni_hid_parser_switch_get_pair_partner_idx(fake_device(0)) == 1);  // paired
    CHECK(last_imu_arg(1) == 1);  // right keeps motion for the merged pad
    CHECK(last_imu_arg(0) == 0);  // left is switched off
}

static void test_pair_keeps_left_imu(void) {
    fake_reset();
    uni_hid_parser_switch_set_pair_imu_side(false);
    start_joycon(0, JCL);
    fake_joycon_run_setup(0, JCL, 50);
    start_joycon(1, JCR);
    fake_joycon_run_setup(1, JCR, 50);
    CHECK(fake_device_ready(0) && fake_device_ready(1));
    CHECK(uni_hid_parser_switch_get_pair_partner_idx(fake_device(0)) == 1);  // paired
    CHECK(last_imu_arg(0) == 1);  // left keeps motion for the merged pad
    CHECK(last_imu_arg(1) == 0);  // right is switched off
    uni_hid_parser_switch_set_pair_imu_side(true);  // restore the default
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

static int count_neutral_rumble(int dev_idx) {
    static const uint8_t neutral[8] = {0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};
    int n = 0;
    for (int i = 0; i < fake_sent_count(); i++) {
        const fake_sent_t* p = fake_sent(i);
        if (p->dev_idx == dev_idx && p->len >= 11 && p->bytes[1] == 0x10 && memcmp(&p->bytes[3], neutral, 8) == 0)
            n++;
    }
    return n;
}

static void test_idle_rumble_refresh(void) {
    fake_reset();
    start_joycon(0, JCL);
    uni_hid_parser_switch_refresh_idle_rumble(fake_device(0));
    CHECK(count_neutral_rumble(0) == 0);  // not ready yet: nothing sent
    fake_joycon_run_setup(0, JCL, 50);

    uni_hid_parser_switch_play_dual_rumble(fake_device(0), 0, 250, 200, 200);
    const int before = count_neutral_rumble(0);
    uni_hid_parser_switch_refresh_idle_rumble(fake_device(0));
    CHECK(count_neutral_rumble(0) == before);  // rumble playing: refresh must not cut it

    fake_advance_ms(300);  // duration over: the parser's own single "stop"
    CHECK(count_neutral_rumble(0) == before + 1);
    uni_hid_parser_switch_refresh_idle_rumble(fake_device(0));
    CHECK(count_neutral_rumble(0) == before + 2);  // idle refresh re-sends neutral
}

static int count_rumble_on(int dev_idx) {
    int n = 0;
    for (int i = 0; i < fake_sent_count(); i++) {
        const fake_sent_t* p = fake_sent(i);
        if (p->dev_idx == dev_idx && p->len >= 11 && p->bytes[1] == 0x10 && p->bytes[4] != 0x01)
            n++;  // rumble-only packet that is not the neutral {00 01 40 40 ..}
    }
    return n;
}

// A 3 s rumble (cutscene) refreshed every 250 ms, as the OGX feedback loop does with a
// 350 ms duration, and the idle refresh called on every tick (worst case): no stop and no
// neutral packet may go out until the rumble really ends, then exactly one stop.
static void test_long_rumble_not_interrupted(void) {
    fake_reset();
    start_joycon(0, JCL);
    fake_joycon_run_setup(0, JCL, 50);
    const int neutral_before = count_neutral_rumble(0);
    for (int t = 0; t < 3000; t += 250) {
        uni_hid_parser_switch_play_dual_rumble(fake_device(0), 0, 350, 200, 200);
        uni_hid_parser_switch_refresh_idle_rumble(fake_device(0));
        fake_advance_ms(250);
    }
    CHECK(count_neutral_rumble(0) == neutral_before);  // never stopped mid-rumble
    CHECK(count_rumble_on(0) == 12);                    // one "on" per cycle
    fake_advance_ms(200);                               // rumble requests stopped
    CHECK(count_neutral_rumble(0) == neutral_before + 1);
}

// Last rumble-only packet's left-motor bytes (offset 3..6 in the sent report, see
// count_neutral_rumble/count_rumble_on above), or NULL if none was sent.
static const uint8_t* last_left_rumble_bytes(int dev_idx) {
    for (int i = fake_sent_count() - 1; i >= 0; i--) {
        const fake_sent_t* p = fake_sent(i);
        if (p->dev_idx == dev_idx && p->len >= 11 && p->bytes[1] == 0x10)
            return &p->bytes[3];
    }
    return NULL;
}

// Rumble intensity: both actuators get the same data, like SDL (HIDAPI_DriverSwitch_ActuallyRumbleJoystick):
// weak sets the high-band amplitude, strong the low-band amplitude, both bands at ~150 Hz.
// Before, the magnitude was encoded as a frequency with a fixed amplitude (every rumble about
// the same strength); then weak went to the left actuator and strong to the right one only, so
// a single Joy-Con ignored one of them.
static const uint8_t* last_right_rumble_bytes(int dev_idx) {
    const uint8_t* left = last_left_rumble_bytes(dev_idx);
    return left ? left + 4 : NULL;
}

static void test_rumble_intensity_tracks_magnitude(void) {
    fake_reset();
    start_joycon(0, JCL);
    fake_joycon_run_setup(0, JCL, 50);

    uni_hid_parser_switch_play_dual_rumble(fake_device(0), 0, 250, 40, 40);
    const uint8_t* low = last_left_rumble_bytes(0);
    CHECK(low != NULL);
    uint8_t low_bytes[4];
    memcpy(low_bytes, low, sizeof(low_bytes));

    uni_hid_parser_switch_play_dual_rumble(fake_device(0), 0, 250, 220, 220);
    const uint8_t* high = last_left_rumble_bytes(0);
    CHECK(high != NULL);

    CHECK(low_bytes[0] == high[0]);    // high-band frequency fixed regardless of magnitude
    CHECK(high[1] > low_bytes[1]);     // high-band amplitude grows with weak
    // Low-band amplitude: byte 3, plus a half step in bit 7 of byte 2.
    const int low_band_low = low_bytes[3] * 2 + (low_bytes[2] >> 7);
    const int low_band_high = high[3] * 2 + (high[2] >> 7);
    CHECK(low_band_high > low_band_low);   // low-band amplitude grows with strong
}

// SDL's bytes for a full single-motor request (checked on Joy-Con L and R hardware).
static void test_rumble_single_magnitude_drives_both_actuators(void) {
    fake_reset();
    start_joycon(0, JCR);
    fake_joycon_run_setup(0, JCR, 50);

    static const uint8_t weak_only[4] = {0x74, 0xc8, 0x3d, 0x40};
    static const uint8_t strong_only[4] = {0x74, 0x00, 0x3d, 0x72};

    uni_hid_parser_switch_play_dual_rumble(fake_device(0), 0, 250, 255, 0);
    CHECK(last_left_rumble_bytes(0) != NULL);
    CHECK(memcmp(last_left_rumble_bytes(0), weak_only, 4) == 0);
    CHECK(memcmp(last_right_rumble_bytes(0), weak_only, 4) == 0);

    uni_hid_parser_switch_play_dual_rumble(fake_device(0), 0, 250, 0, 255);
    CHECK(memcmp(last_left_rumble_bytes(0), strong_only, 4) == 0);
    CHECK(memcmp(last_right_rumble_bytes(0), strong_only, 4) == 0);
}

static void test_rumble_same_data_on_both_actuators(void) {
    fake_reset();
    start_joycon(0, JCL);
    fake_joycon_run_setup(0, JCL, 50);

    uni_hid_parser_switch_play_dual_rumble(fake_device(0), 0, 250, 90, 170);
    const uint8_t* left = last_left_rumble_bytes(0);
    CHECK(left != NULL);
    CHECK(memcmp(left, left + 4, 4) == 0);
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
        {"pair_imu_defaults_to_right", test_pair_imu_defaults_to_right},
        {"pair_keeps_left_imu", test_pair_keeps_left_imu},
        {"cleanup_unlinks_timers", test_cleanup_unlinks_timers},
        {"idle_rumble_refresh", test_idle_rumble_refresh},
        {"long_rumble_not_interrupted", test_long_rumble_not_interrupted},
        {"rumble_intensity_tracks_magnitude", test_rumble_intensity_tracks_magnitude},
        {"rumble_single_magnitude_drives_both_actuators", test_rumble_single_magnitude_drives_both_actuators},
        {"rumble_same_data_on_both_actuators", test_rumble_same_data_on_both_actuators},
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
