#include "btstack_port_esp32.h"
#include "btstack_run_loop.h"
#include "btstack_stdio_esp32.h"
#include "uni.h"
#include "bt/uni_bt.h"

#include "Board/ogxm_log.h"
#include "BTManager/BTManager.h"
#include "Bluepad32/BtPad.h"
#include "UserSettings/DongleSettings.h"
#include "Bluepad32/LinkDiag.h"
#include "Diagnostics/Diagnostics.h"
#include "Diagnostics/DiagForward.h"

/* Pad behaviour shared with the Pico W path lives in Firmware/common/Bluepad32 (BtPad.h,
 * ScanControl, IdleOff.h, ComboHold.h); this file connects it to the I2C link. */

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

/* The output a pad feeds: its Joy-Con pair's output, else its own slot's, else a free one. */
int BTManager::resolve_output(uni_hid_device_t* device, int slot)
{
    const int preferred = bt_pad::output_idx(device);
    if (preferred >= 0 && preferred < MAX_GAMEPADS)
    {
        return preferred;
    }
    for (int out = 0; out < MAX_GAMEPADS; ++out)
    {
        bool used = false;
        for (uint8_t i = 0; i < slots_.size(); ++i)
        {
            used |= (i != slot && slots_[i].connected && slots_[i].output == out);
        }
        if (!used)
        {
            return out;
        }
    }
    return -1;
}

void BTManager::device_disconnected_cb(uni_hid_device_t* device)
{
    const int idx = uni_hid_device_get_idx_for_instance(device);
    if (idx < 0 || idx >= static_cast<int>(slots_.size()))
    {
        return;
    }
    Slot& slot = slots_[idx];
    const bool was_ready = slot.was_ready;
    const int output = slot.output;
    btstack_run_loop_remove_timer(&slot.mode_lightbar_timer);
    slot.connected = false;
    slot.was_ready = false;
    slot.output = -1;
    slot.combo.reset();
    slot.keepalive = bt_pad::KeepaliveState{};
    slot.lightbar = Lightbar{};
    slot.host_rumble_ok_ms = 0;
    slot.battery_sent = 0;
    diag::slot_disconnected(static_cast<size_t>(idx), btstack_run_loop_get_time_ms());
    diag_event("slot %d disconnected", idx);

    if (output >= 0 && output < MAX_GAMEPADS)
    {
        bool still_fed = false;
        for (auto& other : slots_)
        {
            still_fed |= (other.connected && other.output == output);
        }
        if (!still_fed)
        {
            outputs_[output].connected.store(false);
            outputs_[output].idle.reset();
            send_neutral(static_cast<uint8_t>(output));
        }
    }
    apply_scan(true, true);  // a slot is free again

    bool any_slot = false;
    for (auto& other : slots_)
    {
        any_slot |= other.connected;
    }
    if (was_ready && !any_slot)
    {
        diag_event("last controller disconnected, restarting for a clean reconnect");
        restart_after_ms(bt_pad::kLastPadRebootDelayMs);
    }
}

uni_error_t BTManager::device_ready_cb(uni_hid_device_t* device)
{
    if (bt_pad::reject_on_ready(device))
    {
        return UNI_ERROR_INVALID_CONTROLLER;
    }
    const int idx = uni_hid_device_get_idx_for_instance(device);
    if (idx < 0 || idx >= static_cast<int>(slots_.size()))
    {
        return UNI_ERROR_IGNORE_DEVICE;
    }
    const int output = resolve_output(device, idx);
    if (output < 0)
    {
        OGXM_LOG("BP32: slot %d ready but every output is in use\n", idx);
        return UNI_ERROR_NO_SLOTS;
    }

    const uint32_t now_ms = btstack_run_loop_get_time_ms();
    Slot& slot = slots_[idx];
    slot.connected = true;
    slot.was_ready = true;
    slot.output = output;
    slot.combo.reset(now_ms + bt_pad::kDisconnectComboGraceMs);
    slot.keepalive = bt_pad::KeepaliveState{};  // first keepalive on the next feedback tick
    slot.lightbar = Lightbar{};
    const uint32_t grace_ms = bt_pad::host_rumble_grace_ms(device);
    slot.host_rumble_ok_ms = grace_ms ? now_ms + grace_ms : 0;
    {
        /* Diagnostics: the controller, as the Pico W path records it (Diagnostics.h). */
        const bool le = gap_get_connection_type(device->conn.handle) == GAP_CONNECTION_LE;
        diag::slot_connected(static_cast<size_t>(idx), now_ms, device->name, device->vendor_id, device->product_id,
                             static_cast<uint8_t>(device->controller_type), le, device->conn.handle,
                             device->conn.btaddr);
        link_diag::on_slot_ready(idx);
        diag_event("slot %d ready: %s (%04x:%04x, %s)", idx, device->name, device->vendor_id,
                   device->product_id, le ? "LE" : "Classic");
    }
    if (!outputs_[output].connected.exchange(true))
    {
        outputs_[output].idle.reset();
    }
    apply_scan(false, true);  // a pad is ready

    bt_pad::setup_on_ready(device, static_cast<uint8_t>(output), motion_output_);
    schedule_mode_lightbar(device, idx);
    bt_pad::play_connection_rumble(device);
    return UNI_ERROR_SUCCESS;
}

void BTManager::controller_data_cb(uni_hid_device_t* bp_device, uni_controller_t* controller)
{
    if (controller->klass != UNI_CONTROLLER_CLASS_GAMEPAD)
    {
        return;
    }

    const int idx = uni_hid_device_get_idx_for_instance(bp_device);
    if (idx < 0 || idx >= static_cast<int>(slots_.size()) || !slots_[idx].connected)
    {
        return;
    }
    /* A Joy-Con merging after it was ready: follow the pair's output. */
    const int pair_output = bt_pad::output_idx(bp_device);
    if (pair_output >= 0 && pair_output < MAX_GAMEPADS)
    {
        slots_[idx].output = pair_output;
    }
    const int out = slots_[idx].output;
    if (out < 0 || out >= MAX_GAMEPADS)
    {
        return;
    }
    Output& output = outputs_[out];
    const uni_gamepad_t* uni_gp = &controller->gamepad;
    const uint32_t now_ms = btstack_run_loop_get_time_ms();

    if (slots_[idx].combo.update(bt_pad::disconnect_combo_pressed(bp_device, uni_gp), now_ms,
                                 bt_pad::kDisconnectComboHoldMs))
    {
        schedule_disconnect_combo(static_cast<uint8_t>(idx));
        return;  // no further input from a pad that is going away
    }
    output.idle.note_input(uni_gp, now_ms, FEEDBACK_TIME_MS + 100);
    ++slots_[idx].reports;

    const bt_pad::Input mapped = bt_pad::map_input(bp_device, uni_gp, output.mapper);

    /* Motion, touchpad and battery (I2C/Esp32Bridge.h SET_PAD_EXT); the RP2040 turns the motion
     * into the output's frame as the Pico W path does (MotionImu::apply_bt_motion). */
    I2CDriver::PacketInExt ext;
    ext.index = static_cast<uint8_t>(out);
    ext.motion_source = bt_pad::motion_source_of(bp_device, uni_gp);
    for (int i = 0; i < 3; ++i)
    {
        ext.accel[i] = Esp32Bridge::saturate_i16(uni_gp->accel[i]);
        ext.gyro[i] = Esp32Bridge::saturate_i16(uni_gp->gyro[i]);
    }
    std::memcpy(ext.touch_raw, mapped.touch_raw, sizeof(ext.touch_raw));
    ext.flags = (mapped.touchpad_valid ? I2CDriver::PacketInExt::TOUCHPAD_VALID : 0) |
                (mapped.touchpad_click ? I2CDriver::PacketInExt::TOUCHPAD_CLICK : 0);
    if (bt_pad::is_switch_joycon(bp_device))
    {
        ext.flags |= bp_device->controller_type == CONTROLLER_TYPE_SwitchJoyConLeft
                         ? I2CDriver::PacketInExt::JOYCON_LEFT : I2CDriver::PacketInExt::JOYCON_RIGHT;
        if (bt_pad::pair_partner_idx(bp_device) >= 0)
        {
            ext.flags |= I2CDriver::PacketInExt::JOYCON_PAIRED;
        }
    }
    ext.battery = bp_device->controller.battery;
    if (ext.battery != slots_[idx].battery_sent && ext.battery != UNI_CONTROLLER_BATTERY_NOT_AVAILABLE)
    {
        slots_[idx].battery_sent = ext.battery;
        diag::slot_battery(static_cast<size_t>(idx), ext.battery);
    }

    I2CDriver::PacketIn packet_in = I2CDriver::PacketIn();
    packet_in.packet_id = I2CDriver::PacketID::SET_PAD;
    packet_in.index = static_cast<uint8_t>(out);
    packet_in.dpad = mapped.dpad;
    packet_in.buttons = mapped.buttons;
    packet_in.trigger_l = mapped.trigger_l;
    packet_in.trigger_r = mapped.trigger_r;
    packet_in.joystick_lx = mapped.joystick_lx;
    packet_in.joystick_ly = mapped.joystick_ly;
    packet_in.joystick_rx = mapped.joystick_rx;
    packet_in.joystick_ry = mapped.joystick_ry;
    packet_in.bt_slot = static_cast<uint8_t>(idx + 1);
    uint8_t counter_value = 0, counter_bits = 0, counter_unit = 0;
    if (diag_forward::take_counter(idx, counter_value, counter_bits, counter_unit))
    {
        packet_in.counter_value = counter_value;
        packet_in.counter_bits = counter_bits;
        packet_in.counter_unit = counter_unit;
    }

    /* SET_PAD goes out on every report (the RP2040 measures the input timing for the diagnostics
     * from it); the extension only when it changed, before the SET_PAD that applies it. */
    if (std::memcmp(&ext, &output.ext, sizeof(ext)) != 0)
    {
        output.ext = ext;
        i2c_driver_.write_packet(slave_addr(static_cast<uint8_t>(out)), ext);
    }
    /* STEAM mode: the pad's own input for the passthrough report, before the SET_PAD it goes with
     * (as the Pico W path builds it: Bluepad32/BtPad.h ps5_core_from). */
    if (UserSettings::get_instance().get_current_driver() == DeviceDriverType::STEAM)
    {
        I2CDriver::PacketPadRaw raw;
        raw.index = static_cast<uint8_t>(out);
        raw.flags = bp_device->controller_type == CONTROLLER_TYPE_PS5Controller ? I2CDriver::PacketPadRaw::DUALSENSE : 0;
        raw.core = bt_pad::ps5_core_from(uni_gp);
        i2c_driver_.write_packet(slave_addr(static_cast<uint8_t>(out)), raw);
    }
    output.packet_in = packet_in;
    i2c_driver_.write_packet(slave_addr(static_cast<uint8_t>(out)), packet_in);
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
