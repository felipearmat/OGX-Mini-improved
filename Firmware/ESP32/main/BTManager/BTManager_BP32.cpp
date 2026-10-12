#include "btstack_port_esp32.h"
#include "btstack_run_loop.h"
#include "btstack_stdio_esp32.h"
#include "uni.h"
#include "bt/uni_bt.h"
#include "bt/uni_bt_bredr.h"
#include "bt/uni_bt_le.h"

#include "Board/ogxm_log.h"
#include "BTManager/BTManager.h"
#include "BLEServer/BLEServer.h"
#include "Bluepad32/BtPad.h"
#include "Bluepad32/ScanPolicy.h"

/* Pad behaviour shared with the Pico W path lives in Firmware/common/Bluepad32 (BtPad.h,
 * ScanPolicy.h, ComboHold.h); this file only connects it to the I2C link. */

void BTManager::init(int argc, const char** arg_V)
{
    OGXM_LOG("BP32: Initializing Bluepad32\n");
}

void BTManager::init_complete_cb(void)
{
    uni_bt_enable_new_connections_unsafe(true);
    // uni_bt_del_keys_unsafe();
    uni_property_dump_all();
}

uni_error_t BTManager::device_discovered_cb(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi)
{
    if (!((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_GAMEPAD))
    {
        return UNI_ERROR_IGNORE_DEVICE;
    }
    return UNI_ERROR_SUCCESS;
}

void BTManager::device_connected_cb(uni_hid_device_t* device)
{
    OGXM_LOG("BP32: Device connected, addr:  %p, index: %i\n", device, uni_hid_device_get_idx_for_instance(device));
}

void BTManager::device_disconnected_cb(uni_hid_device_t* device)
{
    int idx = uni_hid_device_get_idx_for_instance(device);
    if (idx >= MAX_GAMEPADS || idx < 0)
    {
        return;
    }
    Device& pad = devices_[idx];
    const bool was_ready = pad.was_ready;
    pad.was_ready = false;
    pad.combo.reset();
    pad.keepalive = bt_pad::KeepaliveState{};
    pad.host_rumble_ok_ms = 0;

    manage_connection(static_cast<uint8_t>(idx), false);
    update_search();

    if (was_ready && !any_connected())
    {
        OGXM_LOG("BP32: Last controller disconnected, restarting for a clean reconnect\n");
        restart_after_ms(bt_pad::kLastPadRebootDelayMs);
    }
}

/* Search for new controllers only while a slot is free (Bluepad32/ScanPolicy.h): a full-duty BLE
 * scan next to a connected pad shares the radio with it. Page scan stays on, so a pad that knows
 * the adapter still reconnects. */
void BTManager::update_search()
{
    int connected = 0;
    for (auto& device : devices_)
    {
        connected += device.connected.load() ? 1 : 0;
    }

    /* No search time limits on the ESP32: full search while a slot is free. */
    const scan_policy::Times times{scan_policy::kNoLimit, scan_policy::kNoLimit};
    if (scan_policy::decide(connected, MAX_GAMEPADS, false, 0, times) == scan_policy::Scan::Off)
    {
        uni_bt_enable_new_connections_unsafe(false);  // stops BR/EDR inquiry and the BLE scan
        uni_bt_bredr_scan_stop();                     // also when started outside that flag
        uni_bt_le_scan_stop();
    }
    else if (!uni_bt_enable_new_connections_is_enabled())
    {
        uni_bt_enable_new_connections_unsafe(true);
    }
}

uni_error_t BTManager::device_ready_cb(uni_hid_device_t* device)
{
    if (bt_pad::reject_on_ready(device))
    {
        return UNI_ERROR_INVALID_CONTROLLER;
    }
    int idx = uni_hid_device_get_idx_for_instance(device);
    if (idx >= MAX_GAMEPADS || idx < 0)
    {
        return UNI_ERROR_IGNORE_DEVICE;
    }

    const uint32_t now_ms = btstack_run_loop_get_time_ms();
    Device& pad = devices_[idx];
    pad.was_ready = true;
    pad.combo.reset(now_ms + bt_pad::kDisconnectComboGraceMs);
    pad.keepalive = bt_pad::KeepaliveState{};  // first keepalive on the next feedback tick
    const uint32_t grace_ms = bt_pad::host_rumble_grace_ms(device);
    pad.host_rumble_ok_ms = grace_ms ? now_ms + grace_ms : 0;

    manage_connection(static_cast<uint8_t>(idx), true);
    update_search();

    bt_pad::setup_on_ready(device, static_cast<uint8_t>(idx), false);  // no motion over the I2C link
    bt_pad::play_connection_rumble(device);
    return UNI_ERROR_SUCCESS;
}

void BTManager::controller_data_cb(uni_hid_device_t* bp_device, uni_controller_t* controller)
{
    if (controller->klass != UNI_CONTROLLER_CLASS_GAMEPAD)
    {
        return;
    }

    int idx = uni_hid_device_get_idx_for_instance(bp_device);
    if (idx < 0 || idx >= MAX_GAMEPADS)
    {
        return;
    }
    Device& pad = devices_[idx];
    const uni_gamepad_t* uni_gp = &controller->gamepad;

    if (pad.combo.update(bt_pad::disconnect_combo_pressed(bp_device, uni_gp),
                         btstack_run_loop_get_time_ms(), bt_pad::kDisconnectComboHoldMs))
    {
        OGXM_LOG("BP32: Disconnect combo detected, disconnecting controller %d\n", idx);
        schedule_disconnect_combo(static_cast<uint8_t>(idx));
        return;  // no further input from a pad that is going away
    }

    const bt_pad::Input mapped = bt_pad::map_input(bp_device, uni_gp, pad.mapper);

    I2CDriver::PacketIn packet_in = I2CDriver::PacketIn();
    packet_in.packet_id = I2CDriver::PacketID::SET_PAD;
    packet_in.index = static_cast<uint8_t>(idx);
    packet_in.dpad = mapped.dpad;
    packet_in.buttons = mapped.buttons;
    packet_in.trigger_l = mapped.trigger_l;
    packet_in.trigger_r = mapped.trigger_r;
    packet_in.joystick_lx = mapped.joystick_lx;
    packet_in.joystick_ly = mapped.joystick_ly;
    packet_in.joystick_rx = mapped.joystick_rx;
    packet_in.joystick_ry = mapped.joystick_ry;

    if (std::memcmp(&packet_in, &pad.packet_in, sizeof(packet_in)) == 0)
    {
        return;  // nothing new for the RP2040
    }
    pad.packet_in = packet_in;
    i2c_driver_.write_packet(I2CDriver::MULTI_SLAVE ? packet_in.index + Esp32Bridge::SLAVE_ADDR : Esp32Bridge::SLAVE_ADDR, packet_in);
}

const uni_property_t* BTManager::get_property_cb(uni_property_idx_t idx)
{
    return nullptr;
}

void BTManager::oob_event_cb(uni_platform_oob_event_t event, void* data)
{
    return;
}

uni_platform* BTManager::get_bp32_driver()
{
    static uni_platform driver = {
        .name = "OGXMiniW",
        .init =
            [](int argc, const char** argv)
            { get_instance().init(argc, argv); },

        .on_init_complete =
            [](void)
            { get_instance().init_complete_cb(); },

        .on_device_discovered =
            [](bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi)
            { return get_instance().device_discovered_cb(addr, name, cod, rssi); },

        .on_device_connected =
            [](uni_hid_device_t* device)
            { get_instance().device_connected_cb(device); },

        .on_device_disconnected =
            [](uni_hid_device_t* device)
            { get_instance().device_disconnected_cb(device); },

        .on_device_ready =
            [](uni_hid_device_t* device)
            { return get_instance().device_ready_cb(device); },

        .on_controller_data =
            [](uni_hid_device_t* device, uni_controller_t* controller)
            { get_instance().controller_data_cb(device, controller); },

        .get_property =
            [](uni_property_idx_t idx)
            {  return get_instance().get_property_cb(idx); },

        .on_oob_event =
            [](uni_platform_oob_event_t event, void* data)
            { get_instance().oob_event_cb(event, data); },
    };
    return &driver;
}
