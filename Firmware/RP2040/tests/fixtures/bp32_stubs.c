// What the fixture replay needs of Bluepad32 beyond fake_bt.c: the device calls the DS4 parser
// makes while it sets a pad up (feature report requests, the virtual mouse) do nothing here, and
// the queries for pads with no parser in this test (DualSense, Switch 2) answer "none".
#include "parser/uni_hid_parser_ds5.h"
#include "parser/uni_hid_parser_switch2.h"
#include "uni_hid_device.h"

void uni_hid_device_send_ctrl_report(uni_hid_device_t* d, const uint8_t* report, uint16_t len) {
    (void)d;
    (void)report;
    (void)len;
}

uni_hid_device_t* uni_hid_device_create_virtual(uni_hid_device_t* parent) {
    (void)parent;
    return NULL;
}

void uni_hid_device_connect(uni_hid_device_t* d) {
    (void)d;
}

void uni_hid_device_set_cod(uni_hid_device_t* d, uint32_t cod) {
    (void)d;
    (void)cod;
}

void uni_hid_parser_ds5_get_touchpad(struct uni_hid_device_s* d, uint8_t touch_points[8], bool* touchpad_click) {
    (void)d;
    for (int i = 0; i < 8; ++i)
        touch_points[i] = 0;
    *touchpad_click = false;
}

bool uni_hid_parser_switch2_is_ble_device(const struct uni_hid_device_s* d) {
    (void)d;
    return false;
}

int uni_hid_parser_switch2_get_pair_partner_idx(const struct uni_hid_device_s* d) {
    (void)d;
    return -1;
}
