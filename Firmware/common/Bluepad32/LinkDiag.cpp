#include "Bluepad32/LinkDiag.h"

#include "uni.h"
#include "bt/uni_bt.h"
#include "uni_diag_hooks.h"
#include "parser/uni_hid_parser_switch.h"
#include "Diagnostics/Diagnostics.h"
#include "UserSettings/DongleSettings.h"

namespace link_diag {

static_assert(CONFIG_BLUEPAD32_MAX_DEVICES <= diag::kSlots, "diagnostics slots");
static btstack_packet_callback_registration_t s_hci_cb;
#if defined(ENABLE_BLE)
static btstack_packet_callback_registration_t s_sm_cb;
#endif
/* Links whose remote version is still to be asked: every connection, also one whose pairing then
 * fails (the controller's Bluetooth chip and firmware sub-version in the report). */
static hci_con_handle_t s_version_handles[4] = {HCI_CON_HANDLE_INVALID, HCI_CON_HANDLE_INVALID,
                                                HCI_CON_HANDLE_INVALID, HCI_CON_HANDLE_INVALID};
static bool s_tx_max_pending[CONFIG_BLUEPAD32_MAX_DEVICES]{};
static bool s_qos_pending[CONFIG_BLUEPAD32_MAX_DEVICES]{};
static uint32_t s_poll_latency_us = 0;  // set_poll_latency_us(); 0 = the controller's own polling
static bool s_tx_asked_max = false;  // which level the last Read Transmit Power Level asked for


/* Read AFH Channel Map (Status Parameters, OCF 0x0006): not in BTstack's command table. */
static const hci_cmd_t s_hci_read_afh_channel_map = {HCI_OPCODE(0x05, 0x0006), "H"};
/* Read Scan Enable (Controller & Baseband, OCF 0x0019): whether the controller takes connections
 * (page scan) and answers searches (inquiry scan); not in BTstack's command table either. */
static const hci_cmd_t s_hci_read_scan_enable = {HCI_OPCODE(0x03, 0x0019), ""};
static int s_scan_enable = -1;  // last value logged, -1 = none since the stack started
static bool s_scan_read_pending = false;

static void ask_scan_enable()
{
    s_scan_read_pending = !hci_can_send_command_packet_now();
    if (!s_scan_read_pending)
        hci_send_cmd(&s_hci_read_scan_enable);
}
static uint8_t count_bits(const uint8_t* map, unsigned bits)
{
    uint8_t n = 0;
    for (unsigned i = 0; i < bits; ++i)
        if (map[i / 8] & (1u << (i % 8)))
            ++n;
    return n;
}

/* One HCI query per pass, rotating (remote version and maximum transmit power once per pad;
 * then RSSI, channel map, failed contacts and current transmit power), so diagnostics never
 * crowd the command queue. Those queries only with the "detailed link diagnostics" option
 * (dongle_settings, applied live; what is pending waits for it); the QoS request, the scan state
 * and the Switch firmware version do not depend on it. */
void query(uint32_t now_ms, bool (*slot_connected)(int slot))
{
    if (s_scan_read_pending)
        ask_scan_enable();
    static uint32_t s_last_ms = 0;
    static uint8_t s_step = 0;
    if (now_ms - s_last_ms < 700)
        return;
    s_last_ms = now_ms;
    diag::searching(uni_bt_is_scanning());
    const bool details = dongle_settings::get().link_diagnostics != 0;
    for (auto& vh : s_version_handles) {
        if (!details)
            break;
        if (vh == HCI_CON_HANDLE_INVALID)
            continue;
        if (!hci_can_send_command_packet_now())
            return;
        const hci_con_handle_t h = vh;
        vh = HCI_CON_HANDLE_INVALID;
        if (gap_get_connection_type(h) != GAP_CONNECTION_INVALID) {
            hci_send_cmd(&hci_read_remote_version_information, h);
            return;
        }
    }
    for (uint8_t n = 0; n < CONFIG_BLUEPAD32_MAX_DEVICES; ++n) {
        const uint8_t i = static_cast<uint8_t>((s_step / 4 + n) % CONFIG_BLUEPAD32_MAX_DEVICES);
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (!slot_connected(i) || !d || d->conn.handle == UNI_BT_CONN_HANDLE_INVALID)
            continue;
        if (!hci_can_send_command_packet_now())
            return;
        const hci_con_handle_t h = d->conn.handle;
        const bool le = gap_get_connection_type(h) == GAP_CONNECTION_LE;
        uint8_t major = 0, minor = 0;
        if (uni_hid_parser_switch_get_firmware_version(d, &major, &minor))
            diag::slot_switch_firmware(i, major, minor);
        if (!le && s_qos_pending[i] && s_poll_latency_us) {  // QoS Setup: guaranteed service
            s_qos_pending[i] = false;
            hci_send_cmd(&hci_qos_setup, h, 0, 2, 12000, 0, s_poll_latency_us, 0xFFFFFFFF);
            return;
        }
        if (!details)
            continue;
        if (s_tx_max_pending[i]) {
            s_tx_max_pending[i] = false;
            s_tx_asked_max = true;
            hci_send_cmd(&hci_read_transmit_power_level, h, 1);  // 1 = maximum
            return;
        }
        switch (s_step++ % 4) {
            case 0: gap_read_rssi(h); break;
            case 1:
                if (le) hci_send_cmd(&hci_le_read_channel_map, h);
                else hci_send_cmd(&s_hci_read_afh_channel_map, h);
                break;
            case 2:
                if (!le) hci_send_cmd(&hci_read_failed_contact_counter, h);
                break;
            default:
                s_tx_asked_max = false;
                hci_send_cmd(&hci_read_transmit_power_level, h, 0);  // 0 = current
                break;
        }
        return;
    }
}

static void ask_version(hci_con_handle_t h)
{
    for (auto& vh : s_version_handles)
        if (vh == h)
            return;
    for (auto& vh : s_version_handles)
        if (vh == HCI_CON_HANDLE_INVALID) {
            vh = h;
            return;
        }
}

#if defined(ENABLE_BLE)
/* Pairing (Security Manager, BLE): where a controller's pairing stops, for the report. */
static void sm_handler(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size)
{
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET)
        return;
    const uint32_t now = btstack_run_loop_get_time_ms();
    switch (hci_event_packet_get_type(packet)) {
        case SM_EVENT_PAIRING_STARTED:
            diag::event(now, "LE 0x%04x pairing", sm_event_pairing_started_get_handle(packet));
            break;
        case SM_EVENT_PAIRING_COMPLETE:
            if (sm_event_pairing_complete_get_status(packet) == ERROR_CODE_SUCCESS)
                diag::event(now, "LE 0x%04x paired", sm_event_pairing_complete_get_handle(packet));
            else
                diag::event(now, "LE 0x%04x pair fail 0x%02x r%u", sm_event_pairing_complete_get_handle(packet),
                            sm_event_pairing_complete_get_status(packet), sm_event_pairing_complete_get_reason(packet));
            break;
        case SM_EVENT_REENCRYPTION_STARTED:
            diag::event(now, "LE 0x%04x old key", sm_event_reencryption_started_get_handle(packet));
            break;
        case SM_EVENT_REENCRYPTION_COMPLETE:
            diag::event(now, "LE 0x%04x old key 0x%02x", sm_event_reencryption_complete_get_handle(packet),
                        sm_event_reencryption_complete_get_status(packet));
            break;
        default:
            break;
    }
}

#endif  // ENABLE_BLE

static void hci_handler(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size)
{
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET)
        return;
    const uint32_t now = btstack_run_loop_get_time_ms();
    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_LE_META:
            switch (hci_event_le_meta_get_subevent_code(packet)) {
                case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
                    if (hci_subevent_le_connection_complete_get_status(packet) == 0) {
                        const uint16_t h = hci_subevent_le_connection_complete_get_connection_handle(packet);
                        const uint16_t iv = hci_subevent_le_connection_complete_get_conn_interval(packet);
                        const uint16_t lat = hci_subevent_le_connection_complete_get_conn_latency(packet);
                        const uint16_t to = hci_subevent_le_connection_complete_get_supervision_timeout(packet);
                        diag::le_parameters(h, iv, lat, to);
                        diag::event(now, "LE 0x%04x %u.%02u ms lat %u to %u", h,
                                    iv * 125 / 100, iv * 125 % 100, lat, to * 10);
                        bd_addr_t a;
                        hci_subevent_le_connection_complete_get_peer_address(packet, a);
                        diag::event(now, "LE 0x%04x from %02X:%02X:%02X", h, a[0], a[1], a[2]);
                        ask_version(h);
                    }
                    break;
                case HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE_V1:
                    if (hci_subevent_le_enhanced_connection_complete_v1_get_status(packet) == 0) {
                        const uint16_t h = hci_subevent_le_enhanced_connection_complete_v1_get_connection_handle(packet);
                        const uint16_t iv = hci_subevent_le_enhanced_connection_complete_v1_get_conn_interval(packet);
                        const uint16_t lat = hci_subevent_le_enhanced_connection_complete_v1_get_conn_latency(packet);
                        const uint16_t to = hci_subevent_le_enhanced_connection_complete_v1_get_supervision_timeout(packet);
                        diag::le_parameters(h, iv, lat, to);
                        diag::event(now, "LE 0x%04x %u.%02u ms lat %u to %u", h,
                                    iv * 125 / 100, iv * 125 % 100, lat, to * 10);
                        bd_addr_t a;
                        hci_subevent_le_enhanced_connection_complete_v1_get_peer_addresss(packet, a);  // BTstack spells it so
                        diag::event(now, "LE 0x%04x from %02X:%02X:%02X", h, a[0], a[1], a[2]);
                        ask_version(h);
                    }
                    break;
                case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
                    if (hci_subevent_le_connection_update_complete_get_status(packet) == 0) {
                        const uint16_t h = hci_subevent_le_connection_update_complete_get_connection_handle(packet);
                        const uint16_t iv = hci_subevent_le_connection_update_complete_get_conn_interval(packet);
                        const uint16_t lat = hci_subevent_le_connection_update_complete_get_conn_latency(packet);
                        const uint16_t to = hci_subevent_le_connection_update_complete_get_supervision_timeout(packet);
                        diag::le_parameters(h, iv, lat, to);
                        diag::event(now, "LE 0x%04x now %u.%02u ms lat %u", h,
                                    iv * 125 / 100, iv * 125 % 100, lat, to * 10);
                    }
                    break;
                default:
                    break;
            }
            break;
        case HCI_EVENT_READ_REMOTE_VERSION_INFORMATION_COMPLETE:
            if (hci_event_read_remote_version_information_complete_get_status(packet) == 0) {
                const uint16_t h = hci_event_read_remote_version_information_complete_get_connection_handle(packet);
                const uint16_t company = hci_event_read_remote_version_information_complete_get_manufacturer_name(packet);
                diag::remote_version(h, hci_event_read_remote_version_information_complete_get_version(packet), company,
                                     hci_event_read_remote_version_information_complete_get_subversion(packet));
                const char* vendor = diag::company_name(company);
                diag::event(now, "link 0x%04x chip %s v%u s%04x", h, vendor ? vendor : "?",
                            hci_event_read_remote_version_information_complete_get_version(packet),
                            hci_event_read_remote_version_information_complete_get_subversion(packet));
            }
            break;
        case HCI_EVENT_COMMAND_COMPLETE: {
            const uint16_t op = hci_event_command_complete_get_command_opcode(packet);
            const uint8_t* r = hci_event_command_complete_get_return_parameters(packet);
            if (r[0] != 0)  // status
                break;
            if (op == hci_write_scan_enable.opcode) {
                ask_scan_enable();
                break;
            }
            if (op == s_hci_read_scan_enable.opcode) {  // status, scan enable (bit 0 inquiry, bit 1 page)
                if (r[1] != s_scan_enable)
                    diag::event(now, "scan: connections %s, visible %s", (r[1] & 2) ? "on" : "off",
                                (r[1] & 1) ? "on" : "off");
                s_scan_enable = r[1];
                break;
            }
            const uint16_t h = little_endian_read_16(r, 1);
            if (op == s_hci_read_afh_channel_map.opcode)
                diag::channels(h, count_bits(&r[4], 79), 79);  // status, handle, mode, map[10]
            else if (op == hci_le_read_channel_map.opcode)
                diag::channels(h, count_bits(&r[3], 37), 37);  // status, handle, map[5]
            else if (op == hci_read_failed_contact_counter.opcode)
                diag::failed_contacts(h, little_endian_read_16(r, 3));
            else if (op == hci_read_transmit_power_level.opcode)
                diag::tx_power(h, s_tx_asked_max, static_cast<int8_t>(r[3]));  // status, handle, level
            break;
        }
        case GAP_EVENT_RSSI_MEASUREMENT:
            diag::rssi(gap_event_rssi_measurement_get_con_handle(packet),
                       static_cast<int8_t>(gap_event_rssi_measurement_get_rssi(packet)));
            break;
        case HCI_EVENT_MODE_CHANGE:
            if (hci_event_mode_change_get_status(packet) == 0) {
                const uint16_t h = hci_event_mode_change_get_handle(packet);
                const uint8_t mode = hci_event_mode_change_get_mode(packet);
                const uint16_t iv = hci_event_mode_change_get_interval(packet);
                diag::link_mode(h, mode, iv);
                diag::event(now, "link 0x%04x %s %u.%03u ms", h,
                            mode == 2 ? "sniff" : mode == 0 ? "active" : mode == 1 ? "hold" : "park",
                            iv * 625 / 1000, iv * 625 % 1000);
            }
            break;
        case HCI_EVENT_QOS_SETUP_COMPLETE:  // status, handle, flags, service type, rate, peak, latency
            if (packet[2] == 0)
                diag::event(now, "link 0x%04x poll %lu us", little_endian_read_16(packet, 3),
                            (unsigned long)little_endian_read_32(packet, 15));
            break;
        case HCI_EVENT_INQUIRY_COMPLETE:
            diag::inquiry_complete(now);
            break;
        case GAP_EVENT_ADVERTISING_REPORT:
        case GAP_EVENT_EXTENDED_ADVERTISING_REPORT:
            diag::le_adv_report();
            break;
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                s_scan_enable = -1;
                ask_scan_enable();
            }
            break;
        case HCI_EVENT_CONNECTION_REQUEST: {  // a controller asks for a Classic link
            bd_addr_t a;
            hci_event_connection_request_get_bd_addr(packet, a);
            diag::event(now, "link request %02X:%02X:%02X class %06lx", a[0], a[1], a[2],
                        (unsigned long)hci_event_connection_request_get_class_of_device(packet));
            break;
        }
        case HCI_EVENT_CONNECTION_COMPLETE:
            if (hci_event_connection_complete_get_status(packet) == 0) {
                ask_version(hci_event_connection_complete_get_connection_handle(packet));
            } else {
                bd_addr_t a;
                hci_event_connection_complete_get_bd_addr(packet, a);
                diag::event(now, "link %02X:%02X:%02X fail 0x%02x", a[0], a[1], a[2],
                            hci_event_connection_complete_get_status(packet));
            }
            break;
        case HCI_EVENT_LINK_KEY_REQUEST: {  // the controller asks the host for the stored key
            bd_addr_t a;
            hci_event_link_key_request_get_bd_addr(packet, a);
            diag::event(now, "key request %02X:%02X:%02X", a[0], a[1], a[2]);
            break;
        }
        case HCI_EVENT_LINK_KEY_NOTIFICATION: {  // a new key (pairing) to store
            bd_addr_t a;
            reverse_bd_addr(&packet[2], a);  // event code, length, address (no getter in BTstack)
            diag::event(now, "new key %02X:%02X:%02X", a[0], a[1], a[2]);
            break;
        }
        case HCI_EVENT_AUTHENTICATION_COMPLETE:
            if (hci_event_authentication_complete_get_status(packet) != 0)
                diag::event(now, "link 0x%04x auth fail 0x%02x",
                            hci_event_authentication_complete_get_connection_handle(packet),
                            hci_event_authentication_complete_get_status(packet));
            break;
        case HCI_EVENT_SIMPLE_PAIRING_COMPLETE:
            if (hci_event_simple_pairing_complete_get_status(packet) != 0) {
                bd_addr_t a;
                hci_event_simple_pairing_complete_get_bd_addr(packet, a);
                diag::event(now, "SSP %02X:%02X:%02X fail 0x%02x", a[0], a[1], a[2],
                            hci_event_simple_pairing_complete_get_status(packet));
            }
            break;
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            for (auto& vh : s_version_handles)
                if (vh == hci_event_disconnection_complete_get_connection_handle(packet))
                    vh = HCI_CON_HANDLE_INVALID;
            diag::event(now, "link 0x%04x closed, reason 0x%02x",
                        hci_event_disconnection_complete_get_connection_handle(packet),
                        hci_event_disconnection_complete_get_reason(packet));
            break;
        default:
            break;
    }
}

void init()
{
    s_hci_cb.callback = &hci_handler;
    hci_add_event_handler(&s_hci_cb);
#if defined(ENABLE_BLE)
    s_sm_cb.callback = &sm_handler;
    sm_add_event_handler(&s_sm_cb);
#endif
}

void on_slot_ready(int slot)
{
    if (slot < 0 || slot >= CONFIG_BLUEPAD32_MAX_DEVICES)
        return;
    s_tx_max_pending[slot] = true;
    s_qos_pending[slot] = true;
}

void set_poll_latency_us(uint32_t us)
{
    s_poll_latency_us = us;
}

} // namespace link_diag

/* Bluepad32 diagnostics hooks (bluepad32_diagnostics_hooks.diff). */
extern "C" void uni_diag_on_input_report(struct uni_hid_device_s* d, const uint8_t* report, uint16_t len)
{
    const int slot = uni_hid_device_get_idx_for_instance(d);
    if (slot < 0 || !report || len == 0)
        return;
    /* Input timing per physical device (each half of a merged Joy-Con pair on its own); Switch
     * subcommand replies (0x21) are not input. */
    if (!(d->controller_type == CONTROLLER_TYPE_SwitchJoyConLeft || d->controller_type == CONTROLLER_TYPE_SwitchJoyConRight ||
          d->controller_type == CONTROLLER_TYPE_SwitchProController) || report[0] != 0x21)
        diag::slot_report(static_cast<size_t>(slot), btstack_run_loop_get_time_ms());
    /* The pads' own report counters: DS4 report 0x11 (6 bits, byte 9), DualSense 0x31 (byte 8). */
    if (d->controller_type == CONTROLLER_TYPE_PS4Controller && report[0] == 0x11 && len >= 10)
        diag::slot_counter(static_cast<size_t>(slot), report[9] >> 2, 6);
    else if (d->controller_type == CONTROLLER_TYPE_PS5Controller && report[0] == 0x31 && len >= 9)
        diag::slot_counter(static_cast<size_t>(slot), report[8], 8);
    /* Switch pads: the timer in byte 1 of the standard report 0x30, which moves by a few counts
     * per report (learnt). */
    else if ((d->controller_type == CONTROLLER_TYPE_SwitchJoyConLeft || d->controller_type == CONTROLLER_TYPE_SwitchJoyConRight ||
              d->controller_type == CONTROLLER_TYPE_SwitchProController) && report[0] == 0x30 && len >= 2)
        diag::slot_counter(static_cast<size_t>(slot), report[1], 8, 0);
}

extern "C" void uni_diag_on_device_information(const uint8_t* packet, uint16_t size)
{
    (void)size;
    switch (hci_event_gattservice_meta_get_subevent_code(packet)) {
        case GATTSERVICE_SUBEVENT_DEVICE_INFORMATION_MANUFACTURER_NAME:
            if (gattservice_subevent_device_information_manufacturer_name_get_att_status(packet) == 0)
                diag::device_information(gattservice_subevent_device_information_manufacturer_name_get_con_handle(packet),
                                         "manufacturer", gattservice_subevent_device_information_manufacturer_name_get_value(packet));
            break;
        case GATTSERVICE_SUBEVENT_DEVICE_INFORMATION_MODEL_NUMBER:
            if (gattservice_subevent_device_information_model_number_get_att_status(packet) == 0)
                diag::device_information(gattservice_subevent_device_information_model_number_get_con_handle(packet),
                                         "model", gattservice_subevent_device_information_model_number_get_value(packet));
            break;
        case GATTSERVICE_SUBEVENT_DEVICE_INFORMATION_FIRMWARE_REVISION:
            if (gattservice_subevent_device_information_firmware_revision_get_att_status(packet) == 0)
                diag::device_information(gattservice_subevent_device_information_firmware_revision_get_con_handle(packet),
                                         "firmware", gattservice_subevent_device_information_firmware_revision_get_value(packet));
            break;
        case GATTSERVICE_SUBEVENT_DEVICE_INFORMATION_HARDWARE_REVISION:
            if (gattservice_subevent_device_information_hardware_revision_get_att_status(packet) == 0)
                diag::device_information(gattservice_subevent_device_information_hardware_revision_get_con_handle(packet),
                                         "hardware", gattservice_subevent_device_information_hardware_revision_get_value(packet));
            break;
        case GATTSERVICE_SUBEVENT_DEVICE_INFORMATION_SOFTWARE_REVISION:
            if (gattservice_subevent_device_information_software_revision_get_att_status(packet) == 0)
                diag::device_information(gattservice_subevent_device_information_software_revision_get_con_handle(packet),
                                         "software", gattservice_subevent_device_information_software_revision_get_value(packet));
            break;
        case GATTSERVICE_SUBEVENT_DEVICE_INFORMATION_PNP_ID:
            if (gattservice_subevent_device_information_pnp_id_get_att_status(packet) == 0)
                diag::pnp_id(gattservice_subevent_device_information_pnp_id_get_con_handle(packet),
                             gattservice_subevent_device_information_pnp_id_get_vendor_source_id(packet),
                             gattservice_subevent_device_information_pnp_id_get_vendor_id(packet),
                             gattservice_subevent_device_information_pnp_id_get_product_id(packet),
                             gattservice_subevent_device_information_pnp_id_get_product_version(packet));
            break;
        default:
            break;
    }
}
