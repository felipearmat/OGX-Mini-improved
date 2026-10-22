#include "Board/Config.h"
#include "OGXMini/Board/ESP32_Bluepad32_I2C.h"
#if (OGXM_BOARD == ESP32_BLUEPAD32_I2C)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <pico/multicore.h>
#include <pico/i2c_slave.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <hardware/uart.h>
#include <hardware/watchdog.h>

#include "tusb.h"
#include "bsp/board_api.h"

#include "USBDevice/DeviceManager.h"
#include "UserSettings/UserSettings.h"
#include "Board/board_api.h"
#include "Board/esp32_api.h"
#include "Board/BootMode.h"
#include "Board/BootStage.h"
#include "Diagnostics/DiagnosticsBoard.h"
#include "Board/FlashLog.h"
#include "Board/BtAdapterAddress.h"
#include "Board/ReportedMac.h"
#include "Board/LogRing.h"
#include "Board/RxRing.h"
#include "Board/ModeIndicator.h"
#include "Board/StatusLed.h"
#include "USBDevice/DeviceDriver/UARTBridge/UARTBridge.h"
#include "USBDevice/DeviceDriver/WebApp/WebApp.h"
#include "Gamepad/Gamepad.h"
#include "Gamepad/MotionImu.h"
#include "I2C/Esp32Bridge.h"
#include "I2C/ModeSync.h"
#include "UserSettings/DongleSettings.h"
#include "USBDevice/DeviceDriver/KBM/KbmSettings.h"
#include "USBDevice/DeviceDriver/Steam/SteamActive.h"
#include "USBDevice/DeviceDriver/Steam/SteamBtReport.h"
#include "USBDevice/DeviceDriver/Steam/SteamPassthrough.h"
#include "USBDevice/DeviceDriver/Steam/SteamTouchpad.h"
#include "Diagnostics/Diagnostics.h"
#include "USBDevice/DeviceDriver/MotionOutputActive.h"
#include "TaskQueue/TaskQueue.h"
#if defined(CONFIG_EN_USB_HOST)
#include "pio_usb.h"
#include "USBHost/PioHostPort.h"
#include "USBHost/HostManager.h"
#include "Input/InputSlot.h"
#endif

// Wire format shared with the ESP32 companion firmware: I2C/Esp32Bridge.h
using Esp32Bridge::PacketID;
using Esp32Bridge::PacketIn;
using Esp32Bridge::PacketOut;
using Esp32Bridge::PacketInExt;
using Esp32Bridge::PacketOutExt;
using Esp32Bridge::PacketDiag;
using Esp32Bridge::PacketProfile;
using Esp32Bridge::PacketBlob;
using Esp32Bridge::PacketPadRaw;

/* Settings blobs for the ESP32's BLE web app (GET_BLOB / SET_BLOB): the current bytes of a kind. */
static size_t blob_bytes(uint8_t kind, const uint8_t*& data) {
    switch (kind) {
        case PacketBlob::DONGLE_SETTINGS:
            data = reinterpret_cast<const uint8_t*>(&dongle_settings::get());
            return sizeof(dongle_settings::Settings);
        case PacketBlob::KBM_SETTINGS:
            data = reinterpret_cast<const uint8_t*>(&kbm_settings::get());
            return sizeof(kbm_settings::Settings);
        default:
            data = nullptr;
            return 0;
    }
}

/* The settings live here (USB web app, flash); the ESP32 maps its pads with them and its BLE web
 * app edits them through the I2C link (GET_PROFILE / SET_PROFILE). Saving reboots the RP2040, so
 * the copy taken at boot stays current. */
/* Main-loop watchdog (run()): longest the loop may go without a pass before the adapter reboots. */
static constexpr uint32_t kHangWatchdogMs = 5000;

/* The ESP32's serial output, received by DMA (Board/RxRing.h): read only from the main loop, the
 * 32-byte UART FIFO overflowed whenever one pass took over about 2.8 ms (a flash log write, USB),
 * and the ESP32's boot lines came with bytes missing. 4 KB holds about 355 ms of output. */
static RxRing<12> s_esp_rx;
static int s_esp_rx_dma = -1;
static uint32_t s_esp_rx_base = 0;
static constexpr uint32_t kEspRxDmaCount = 0x0FFFFFFF;  // the RP2350's mode bits (31:28) stay 0

/* The highest free channel: PIO USB claims its fixed one (0 by default) only when the host port
 * starts, and panics if it is taken; the lowest free channel used to be that one. */
static void esp_rx_start() {
#if defined(CONFIG_EN_USB_HOST)
    constexpr int kLowest = PIO_USB_DMA_TX_DEFAULT + 1;
#else
    constexpr int kLowest = 0;
#endif
    for (int ch = NUM_DMA_CHANNELS - 1; ch >= kLowest && s_esp_rx_dma < 0; --ch) {
        if (!dma_channel_is_claimed(static_cast<uint>(ch))) {
            dma_channel_claim(static_cast<uint>(ch));
            s_esp_rx_dma = ch;
        }
    }
    if (s_esp_rx_dma < 0)
        return;  // no channel: the ESP32's output is not kept
    dma_channel_config c = dma_channel_get_default_config(s_esp_rx_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, decltype(s_esp_rx)::kSizeBits);
    channel_config_set_dreq(&c, uart_get_dreq_num(uart0, false));
    dma_channel_configure(s_esp_rx_dma, &c, s_esp_rx.buffer(), &uart_get_hw(uart0)->dr, kEspRxDmaCount, true);
}

// Bytes the DMA has received so far; restarts the channel when its count runs out (hours of output).
static uint32_t esp_rx_produced() {
    if (s_esp_rx_dma < 0)
        return 0;
    uint32_t left = dma_channel_hw_addr(s_esp_rx_dma)->transfer_count & kEspRxDmaCount;
    if (!dma_channel_is_busy(s_esp_rx_dma)) {
        s_esp_rx_base += kEspRxDmaCount - left;
        dma_channel_set_trans_count(s_esp_rx_dma, kEspRxDmaCount, true);  // the write address goes on
        left = kEspRxDmaCount;
    }
    return s_esp_rx_base + (kEspRxDmaCount - left);
}

static UserProfile _profiles[UserSettings::MAX_PROFILES];
static uint16_t _profiles_crc = 0;
static uint8_t _active_profile[MAX_GAMEPADS];
static DeviceDriverType _current_driver = DeviceDriverType::NONE;

/* Motion, touchpad and battery of the ESP32's pads (SET_PAD_EXT), applied with the next SET_PAD;
 * the Wiimote's pseudo gyro keeps state per output. */
static PacketInExt _pad_ext[MAX_GAMEPADS];
static MotionImu::WiiPseudoGyroState _wii_gyro[MAX_GAMEPADS];

static Gamepad::Lightbar _gamepads_lightbar(uint8_t index);

/* Bluetooth diagnostics from the ESP32 (SET_DIAG) into Diagnostics/Diagnostics.h, as the Pico W path
 * records them, so the web app's report covers the hybrid's controllers too. */
static void record_diag(const PacketDiag& diag) {
    static char names[diag::kSlots][PacketDiag::kData + 1]{};
    static char event[diag::kEventText]{};
    static size_t event_len = 0;
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (diag.slot >= diag::kSlots)
        return;
    auto d8 = [&diag](size_t i) { return diag.data[i]; };
    auto d16 = [&diag](size_t i) { return static_cast<uint16_t>(diag.data[i] | (diag.data[i + 1] << 8)); };
    switch (diag.kind) {
        case PacketDiag::SLOT_NAME:
            std::memcpy(names[diag.slot], diag.data, PacketDiag::kData);
            names[diag.slot][PacketDiag::kData] = '\0';
            break;
        case PacketDiag::SLOT_READY: {
            const uint8_t* d = diag.data;
            diag::slot_connected(diag.slot, now, names[diag.slot],
                                 static_cast<uint16_t>(d[0] | (d[1] << 8)), static_cast<uint16_t>(d[2] | (d[3] << 8)),
                                 d[4], d[5] != 0, static_cast<uint16_t>(d[6] | (d[7] << 8)), &d[8]);
            reported_mac::on_pad_ready(&d[8]);  // "MAC address per controller" option, as on the Pico W
            break;
        }
        case PacketDiag::SLOT_GONE:
            diag::slot_disconnected(diag.slot, now);
            break;
        case PacketDiag::BATTERY:
            diag::slot_battery(diag.slot, diag.data[0]);
            break;
        case PacketDiag::EVENT: {
            const size_t room = sizeof(event) - 1 - event_len;
            size_t part = strnlen(reinterpret_cast<const char*>(&diag.data[1]), PacketDiag::kEventPart);
            part = part < room ? part : room;
            std::memcpy(event + event_len, &diag.data[1], part);
            event_len += part;
            if (diag.data[0]) {
                event[event_len] = '\0';
                diag::event(now, "%s", event);
                event_len = 0;
            }
            break;
        }
        case PacketDiag::SEARCHING:
            diag::searching(diag.data[0] != 0, diag.data[1] != 0);
            break;
        case PacketDiag::ADAPTER_ADDRESS:
            bt_adapter_address::set(diag.data);  // DualShock 3 USB pairing (USB host port)
            break;
        /* Link details (Bluepad32/LinkDiag on the ESP32): the same Diagnostics.h calls. */
        case PacketDiag::SWITCH_FIRMWARE:
            diag::slot_switch_firmware(diag.slot, d8(0), d8(1));
            break;
        case PacketDiag::LE_PARAMETERS:
            diag::le_parameters(d16(0), d16(2), d16(4), d16(6));
            break;
        case PacketDiag::REMOTE_VERSION:
            diag::remote_version(d16(0), d8(2), d16(3), d16(5));
            break;
        case PacketDiag::DEVICE_INFO: {
            static const char* const kFields[] = {"manufacturer", "model", "firmware", "hardware", "software"};
            char value[PacketDiag::kData - 2]{};
            std::memcpy(value, &diag.data[3], sizeof(value) - 1);
            if (d8(2) < 5)
                diag::device_information(d16(0), kFields[d8(2)], value);
            break;
        }
        case PacketDiag::PNP_ID:
            diag::pnp_id(d16(0), d8(2), d16(3), d16(5), d16(7));
            break;
        case PacketDiag::RSSI:
            diag::rssi(d16(0), static_cast<int8_t>(d8(2)));
            break;
        case PacketDiag::CHANNELS:
            diag::channels(d16(0), d8(2), d8(3));
            break;
        case PacketDiag::FAILED_CONTACTS:
            diag::failed_contacts(d16(0), d16(2));
            break;
        case PacketDiag::TX_POWER:
            diag::tx_power(d16(0), d8(2) != 0, static_cast<int8_t>(d8(3)));
            break;
        case PacketDiag::LINK_MODE:
            diag::link_mode(d16(0), d8(2), d16(3));
            break;
        case PacketDiag::INQUIRY_COMPLETE:
            diag::inquiry_complete(now);
            break;
        case PacketDiag::LE_ADV_REPORTS:
            for (uint16_t i = d16(0); i > 0; --i)
                diag::le_adv_report();
            break;
        case PacketDiag::COMPANION_SLOT:
            diag::companion_slot(diag.slot, d16(0), d8(2) != 0, d16(3));
            break;
        case PacketDiag::COMPANION_VERSION: {
            char version[PacketDiag::kData + 1]{};
            std::memcpy(version, diag.data, PacketDiag::kData);
            diag::companion_version(version);
            break;
        }
        case PacketDiag::COMPANION_INFO: {
            char firmware[PacketDiag::kData - 11 + 1]{};
            std::memcpy(firmware, &diag.data[11], sizeof(firmware) - 1);
            const uint32_t uptime = d16(0) | (static_cast<uint32_t>(d16(2)) << 16);
            diag::companion_info(now, firmware, uptime, d8(4) != 0, d16(5), d16(7), d16(9));
            break;
        }
        default:
            break;
    }
}

static Gamepad::PadIn pad_in_from_packet(const PacketIn& packet) {
    Gamepad::PadIn pad_in;
    pad_in.dpad = packet.dpad;
    pad_in.buttons = packet.buttons;
    pad_in.trigger_l = packet.trigger_l;
    pad_in.trigger_r = packet.trigger_r;
    pad_in.joystick_lx = packet.joystick_lx;
    pad_in.joystick_ly = packet.joystick_ly;
    pad_in.joystick_rx = packet.joystick_rx;
    pad_in.joystick_ry = packet.joystick_ry;
    std::memcpy(pad_in.analog, packet.analog, sizeof(packet.analog));
    if (packet.index < MAX_GAMEPADS) {
        /* As on the Pico W path (Bluepad32.cpp): motion through MotionImu::apply_bt_motion, then
         * touchpad and battery. */
        const PacketInExt& ext = _pad_ext[packet.index];
        const int32_t accel[3] = {ext.accel[0], ext.accel[1], ext.accel[2]};
        const bool scaled = (ext.flags & PacketInExt::GYRO_SCALED) != 0;
        const int32_t gyro[3] = {Esp32Bridge::gyro_from_wire(ext.gyro[0], ext.motion_source, scaled),
                                 Esp32Bridge::gyro_from_wire(ext.gyro[1], ext.motion_source, scaled),
                                 Esp32Bridge::gyro_from_wire(ext.gyro[2], ext.motion_source, scaled)};
        MotionImu::apply_bt_motion(pad_in, ext.motion_source, accel, gyro,
                                   (ext.flags & PacketInExt::JOYCON_LEFT) != 0,
                                   (ext.flags & PacketInExt::JOYCON_RIGHT) != 0,
                                   (ext.flags & PacketInExt::JOYCON_PAIRED) != 0,
                                   _wii_gyro[packet.index], to_ms_since_boot(get_absolute_time()));
        MotionImu::report_motion(pad_in, to_ms_since_boot(get_absolute_time()));
        std::memcpy(pad_in.touch_raw, ext.touch_raw, sizeof(pad_in.touch_raw));
        pad_in.touchpad_valid = (ext.flags & PacketInExt::TOUCHPAD_VALID) ? 1 : 0;
        pad_in.touchpad_click = (ext.flags & PacketInExt::TOUCHPAD_CLICK) ? 1 : 0;
        pad_in.battery = ext.battery;
    }
    return pad_in;
}

/* The reply to the ESP32's GET_EXT: the host's lightbar, whether the mode uses motion, and the
 * dongle settings (the web app keeps them here, on the USB side). */
static PacketOutExt ext_reply(uint8_t index) {
    PacketOutExt reply;
    reply.index = index;
    if (index < MAX_GAMEPADS) {
        const Gamepad::Lightbar lightbar = _gamepads_lightbar(index);
        if (lightbar.valid) {
            reply.flags |= PacketOutExt::LIGHTBAR_VALID;
            reply.lightbar_r = lightbar.r;
            reply.lightbar_g = lightbar.g;
            reply.lightbar_b = lightbar.b;
        }
    }
    if (MotionOutputActive::is_enabled()) {
        reply.flags |= PacketOutExt::MOTION_OUTPUT;
    }
    if (WebAppDevice::connected()) {
        reply.flags |= PacketOutExt::WEB_APP_OPEN;  // the ESP32 sends unmapped buttons meanwhile
    }
    dongle_settings::encode_v6(dongle_settings::get(), reply.settings);
    reply.driver = _current_driver;
    reply.active_profile = index < MAX_GAMEPADS ? _active_profile[index] : 1;
    reply.profiles_crc = _profiles_crc;
    mode_indicator::shown_color(_current_driver, reply.mode_rgb[0], reply.mode_rgb[1], reply.mode_rgb[2]);
    return reply;
}

constexpr size_t  MAX_BUFFER_SIZE = std::max(sizeof(PacketOut), sizeof(PacketIn));

static Gamepad _gamepads[MAX_GAMEPADS];

static Gamepad::Lightbar _gamepads_lightbar(uint8_t index) {
    return _gamepads[index].get_host_lightbar();
}
static bool _uart_bridge_mode = false;

static inline void slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
    static size_t count = 0;
    static PacketIn packet_in;
    static PacketOut packet_out;
    static bool ext_requested = false;  // answer the next read with a PacketOutExt
    /* Mode changes from the ESP32 count once it has read our PacketOutExt (I2C/ModeSync.h). */
    static uint8_t ext_replies = 0;
    static PacketProfile profile_request;  // answer the next read with this chunk
    static bool profile_requested = false;
    static uint8_t incoming[sizeof(UserProfile)];  // a profile saved over BLE, chunk by chunk
    static PacketBlob blob_request;  // answer the next read with this chunk
    static bool blob_requested = false;
    static uint8_t incoming_blob[PacketBlob::kMaxLen];  // settings saved over BLE
    static bool raw_seen = false;      // a SET_PAD_RAW came with this report
    static bool raw_dualsense = false;
    static_assert(sizeof(PacketInExt) <= sizeof(PacketIn), "the receive buffer holds every ESP32 packet");
    static DeviceDriverType current_device_type = 
        UserSettings::get_instance().get_current_driver();
    /* Custom fix: the reply being read. Replies (up to 37 bytes) are longer than the I2C TX FIFO
     * (16): written whole with i2c_write_raw_blocking they kept this interrupt waiting until the
     * ESP32 read the rest. When the ESP32 gave up on a read (its 2 ms limit, e.g. while this core
     * was parked for a flash write), the handler kept waiting, handed the rest of the old reply
     * to the next read, and left the ESP32's writes unserved meanwhile: errors that fed each other
     * until the link went silent. Now each read request tops the FIFO up from this buffer
     * without waiting; the end of the transfer drops what was not read. */
    static uint8_t reply_buf[std::max({sizeof(PacketOut), sizeof(PacketOutExt), sizeof(PacketProfile), sizeof(PacketBlob)})];
    static uint8_t reply_len = 0;
    static uint8_t reply_pos = 0;
    static bool reply_active = false;

    switch (event) {
        case I2C_SLAVE_RECEIVE:
            if (count < sizeof(PacketIn)) {
                reinterpret_cast<uint8_t*>(&packet_in)[count] = i2c_read_byte_raw(i2c);
                ++count;
            }
            break;
        case I2C_SLAVE_FINISH:
            if (reply_active) {
                diag::i2c_reply(reply_pos < reply_len);  // a cut reply's rest is dropped here
                reply_active = false;
            }
            if (count == 0) {
                break;
            }
            /* Custom fix: every ESP32 packet is 32 bytes and says so in its first byte. A write the
             * bus corrupted (no checksum on the link; the ESP32's writes are acknowledged byte by
             * byte whatever arrives) was acted on all the same: a stray byte read as SET_DRIVER
             * switched the adapter to another mode now and then. */
            if (count != sizeof(PacketIn) || packet_in.packet_len != sizeof(PacketIn)) {
                diag::i2c_packet_rejected();
                count = 0;
                break;
            }
            switch (packet_in.packet_id) {
                case PacketID::SET_PAD_RAW: {
                    PacketPadRaw raw;
                    std::memcpy(static_cast<void*>(&raw), &packet_in, sizeof(raw));
                    if (SteamActive::is_enabled()) {
                        SteamBtReport::store_ps5_core(raw.core);  // as the Pico W path does
                        raw_seen = true;
                        raw_dualsense = (raw.flags & PacketPadRaw::DUALSENSE) != 0;
                    }
                    break;
                }
                case PacketID::SET_PAD:
                    /* STEAM mode: the passthrough report of this report (SET_PAD_RAW came first). */
                    if (SteamActive::is_enabled()) {
                        SteamPassthrough::input_has_touchpad = raw_seen && raw_dualsense;
                        if (raw_seen && raw_dualsense && packet_in.index < MAX_GAMEPADS) {
                            const PacketInExt& ext = _pad_ext[packet_in.index];
                            SteamTouchpad::apply_to_passthrough(ext.touch_raw, (ext.flags & PacketInExt::TOUCHPAD_CLICK) != 0);
                        }
                        raw_seen = false;
                    }
                    if (packet_in.index < MAX_GAMEPADS
#if defined(CONFIG_EN_USB_HOST)
                        /* A controller on the USB host port has the output: Bluetooth waits. */
                        && !InputSlot::usb_owns(packet_in.index)
#endif
                    ) {
                        _gamepads[packet_in.index].set_pad_in(pad_in_from_packet(packet_in));
                    }
                    if (packet_in.bt_slot > 0 && packet_in.bt_slot <= diag::kSlots) {
                        const size_t slot = packet_in.bt_slot - 1u;
                        diag::slot_report(slot, to_ms_since_boot(get_absolute_time()));
                        if (packet_in.counter_bits > 0) {
                            diag::slot_counter(slot, packet_in.counter_value, packet_in.counter_bits,
                                               packet_in.counter_unit);
                        }
                    }
                    break;
                case PacketID::SET_DIAG: {
                    PacketDiag diag_packet;
                    std::memcpy(static_cast<void*>(&diag_packet), &packet_in, sizeof(diag_packet));
                    record_diag(diag_packet);
                    break;
                }
                case PacketID::SET_PAD_EXT: {
                    PacketInExt ext;
                    std::memcpy(static_cast<void*>(&ext), &packet_in, sizeof(ext));
                    if (ext.index < MAX_GAMEPADS) {
                        _pad_ext[ext.index] = ext;
                    }
                    break;
                }
                case PacketID::GET_EXT:
                    ext_requested = true;
                    break;
                case PacketID::GET_BLOB:
                    std::memcpy(static_cast<void*>(&blob_request), &packet_in, sizeof(blob_request));
                    blob_requested = true;
                    break;
                case PacketID::SET_BLOB: {
                    PacketBlob chunk;
                    std::memcpy(static_cast<void*>(&chunk), &packet_in, sizeof(chunk));
                    const size_t offset = chunk.chunk * PacketBlob::kData;
                    if (chunk.total_len == 0 || chunk.total_len > PacketBlob::kMaxLen || offset >= chunk.total_len) {
                        break;
                    }
                    const size_t len = std::min(PacketBlob::kData, static_cast<size_t>(chunk.total_len) - offset);
                    std::memcpy(incoming_blob + offset, chunk.data, len);
                    if (offset + len != chunk.total_len) {
                        break;  // more chunks to come
                    }
                    if (chunk.kind == PacketBlob::DONGLE_SETTINGS) {
                        dongle_settings::Settings settings{};
                        if (dongle_settings::decode(incoming_blob, chunk.total_len, settings)) {
                            TaskQueue::Core0::queue_delayed_task(TaskQueue::Core0::get_new_task_id(), 100, false,
                                [settings] { UserSettings::get_instance().store_dongle_settings(settings); });
                        }
                    } else if (chunk.kind == PacketBlob::KBM_SETTINGS) {
                        kbm_settings::Settings settings{};
                        if (kbm_settings::decode(incoming_blob, chunk.total_len, settings)) {
                            TaskQueue::Core0::queue_delayed_task(TaskQueue::Core0::get_new_task_id(), 1, false,
                                [settings] { UserSettings::get_instance().store_kbm_settings(settings); });
                        }
                    }
                    break;
                }
                case PacketID::GET_PROFILE:
                    std::memcpy(static_cast<void*>(&profile_request), &packet_in, sizeof(profile_request));
                    profile_requested = true;
                    break;
                case PacketID::SET_PROFILE: {
                    PacketProfile chunk;
                    std::memcpy(static_cast<void*>(&chunk), &packet_in, sizeof(chunk));
                    if (chunk.chunk >= PacketProfile::kChunks) {
                        break;
                    }
                    const size_t offset = chunk.chunk * PacketProfile::kData;
                    const size_t len = std::min(PacketProfile::kData, sizeof(UserProfile) - offset);
                    std::memcpy(incoming + offset, chunk.data, len);
                    if (chunk.chunk + 1 != PacketProfile::kChunks) {
                        break;
                    }
                    UserProfile profile;
                    std::memcpy(&profile, incoming, sizeof(profile));
                    if (profile.id < 1 || profile.id > UserSettings::MAX_PROFILES || chunk.player_idx >= MAX_GAMEPADS) {
                        break;
                    }
                    OGXM_LOG("I2C: Profile saved over BLE.\n");
                    //Any writes to flash should be done on Core0 (they reboot)
                    TaskQueue::Core0::queue_delayed_task(
                        TaskQueue::Core0::get_new_task_id(), 100, false,
                        [profile, index = chunk.player_idx, driver = chunk.driver] {
                            if (driver != DeviceDriverType::NONE) {
                                UserSettings::get_instance().store_profile_and_driver_type(driver, index, profile);
                            } else {
                                UserSettings::get_instance().store_profile(index, profile);
                            }
                        }
                    );
                    break;
                }
                case PacketID::SET_DRIVER: {
                    /* The same request twice within 2 s (the ESP32 repeats a combo's request every
                     * tick, I2C/ModeSync.h): one corrupted packet no longer changes the mode. */
                    static DeviceDriverType last_request = DeviceDriverType::NONE;
                    static uint32_t last_request_ms = 0;
                    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
                    const bool confirmed = packet_in.device_driver == last_request && now_ms - last_request_ms < 2000;
                    last_request = packet_in.device_driver;
                    last_request_ms = now_ms;
                    if (confirmed && mode_sync::rp2040_accepts(ext_replies, packet_in.device_driver, current_device_type,
                                                              (packet_in.request_flags & PacketIn::FROM_COMBO) != 0)) {
                        OGXM_LOG("I2C: Driver change detected.\n");
                        //Any writes to flash should be done on Core0
                        TaskQueue::Core0::queue_delayed_task(
                            TaskQueue::Core0::get_new_task_id(), 1000, false, 
                            [new_device_type = packet_in.device_driver] { 
                                /* The session's input-to-use latency goes into the summary kept
                                 * across the reboot, as the Pico W path does on a mode change. */
                                const Gamepad::LatencyStats lat = _gamepads[0].latency_stats();
                                diag::pipeline_latency(lat.samples, lat.avg_us, lat.max_us);
                                UserSettings::get_instance().store_driver_type(new_device_type);
                            }
                        );
                    }
                    break;
                }
                default:
                    break;
            }
            count = 0;
            break;
        case I2C_SLAVE_REQUEST: {
            if (!reply_active) {  // the first byte of a read: build its reply
                reply_active = true;
                reply_pos = 0;
                if (blob_requested) {
                    blob_requested = false;
                    PacketBlob reply = blob_request;
                    const uint8_t* data = nullptr;
                    const size_t total = blob_bytes(reply.kind, data);
                    const size_t offset = reply.chunk * PacketBlob::kData;
                    reply.total_len = static_cast<uint8_t>(total);
                    if (data && offset < total) {
                        std::memcpy(reply.data, data + offset, std::min(PacketBlob::kData, total - offset));
                    }
                    reply_len = std::min<uint8_t>(reply.packet_len, sizeof(reply));
                    std::memcpy(reply_buf, &reply, reply_len);
                } else if (profile_requested) {
                    profile_requested = false;
                    PacketProfile reply = profile_request;
                    if (reply.profile_id >= 1 && reply.profile_id <= UserSettings::MAX_PROFILES &&
                        reply.chunk < PacketProfile::kChunks) {
                        const size_t offset = reply.chunk * PacketProfile::kData;
                        const size_t len = std::min(PacketProfile::kData, sizeof(UserProfile) - offset);
                        std::memcpy(reply.data, reinterpret_cast<const uint8_t*>(&_profiles[reply.profile_id - 1]) + offset, len);
                    } else {
                        reply.profile_id = 0;  // nothing there
                    }
                    reply_len = std::min<uint8_t>(reply.packet_len, sizeof(reply));
                    std::memcpy(reply_buf, &reply, reply_len);
                } else if (ext_requested) {
                    ext_requested = false;
                    if (ext_replies < mode_sync::kRepliesBeforeRequests) {
                        ++ext_replies;
                    }
                    const PacketOutExt reply = ext_reply(packet_in.index);
                    reply_len = std::min<uint8_t>(reply.packet_len, sizeof(reply));
                    std::memcpy(reply_buf, &reply, reply_len);
                } else {
                    if (packet_in.index < MAX_GAMEPADS) {
                        packet_out.index = packet_in.index;
                        /* Custom fix: the strongest rumble since the ESP32's last read, as the Pico W
                         * Bluetooth path does; the current value lost host pulses shorter than the
                         * ESP32's read period. */
                        const Gamepad::PadOut pad_out = _gamepads[packet_in.index].take_pad_out_peak();
                        packet_out.rumble_l = pad_out.rumble_l;
                        packet_out.rumble_r = pad_out.rumble_r;
                    }
                    reply_len = std::min<uint8_t>(packet_out.packet_len, sizeof(packet_out));
                    std::memcpy(reply_buf, &packet_out, reply_len);
                }
            }
            /* As many bytes as the FIFO takes now (a read request comes with it empty); the next
             * request gets more. A read past the reply's end gets a zero, so the bus never waits. */
            size_t room = i2c_get_write_available(i2c);
            if (reply_pos >= reply_len) {
                i2c_get_hw(i2c)->data_cmd = 0;
            }
            while (room > 0 && reply_pos < reply_len) {
                i2c_get_hw(i2c)->data_cmd = reply_buf[reply_pos++];
                --room;
            }
            break;
        }
        default:
            break;
    }
}

/* I2C slave to the ESP32. Its interrupt runs on Core1, the core that calls this: the handler
 * takes the gamepads' mutexes, which Core0's USB loop also holds, so on Core0 an interrupt could
 * wait forever on a mutex its own core holds (the adapter froze once a pad connected). */
static void init_i2c_slave() {
    i2c_init(I2C_PORT, I2C_BAUDRATE);

    gpio_init(I2C_SDA_PIN);
    gpio_init(I2C_SCL_PIN);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    i2c_slave_init(I2C_PORT, Esp32Bridge::SLAVE_ADDR, &slave_handler);

    OGXM_LOG("I2C Driver initialized\n");
}

#if defined(CONFIG_EN_USB_HOST)
/* Custom: USB host port (wired controllers, DualShock 3 pairing), on Core0: started once a device
 * is plugged in (Pico-PIO-USB does not reliably see a hot plug), then served from Core0's loop,
 * as on the Pico W (USBHost/PioHostPort.h). */
static void usb_host_tick() {
    static bool s_started = false;
    if (!s_started) {
        if (!board_api::usb::host_connected() || !pio_host_port::start()) {
            return;
        }
        HostManager& host_manager = HostManager::get_instance();
        TaskQueue::Core0::queue_delayed_task(TaskQueue::Core0::get_new_task_id(), 200, true,
            [&host_manager] { host_manager.send_feedback(); });
        s_started = true;
    }
    pio_host_port::service_frames();
    tuh_task();
}
#endif

/* Custom: the status LED (the Pico's own on GP25, an external RGB LED on GP6 in the output mode
 * colour, hardware/README.md): the shared status LED (Board/StatusLed.h) with the search state the
 * ESP32 reports; a wired controller on the USB host port counts as no search. */
static void status_led_tick() {
    static status_led::Blinker s_led;
    static uint32_t s_next_ms = 0;

    const uint32_t now = board_api::ms_since_boot();
    if (static_cast<int32_t>(now - s_next_ms) < 0) {
        return;
    }
    bool reduced = false;
    /* Nothing from the ESP32 yet: it boots searching. */
    const bool searching = !board_api::usb::host_any_pad_mounted() &&
                           diag::accepting_new_controllers(true, reduced);
    const status_led::Search search = !searching ? status_led::Search::NONE
                                    : reduced ? status_led::Search::REDUCED
                                    : status_led::Search::FULL;
    /* A colour saved or previewed in the web app, or other status LED options: show them now. */
    static uint32_t s_generation = UINT32_MAX;
    static uint8_t s_pico = 0xFF;
    static uint8_t s_external = 0xFF;
    static uint8_t s_brightness = 0;
    const dongle_settings::Settings& options = dongle_settings::get();
    if (s_generation != mode_indicator::color_generation()) {
        s_generation = mode_indicator::color_generation();
        uint8_t r, g, b;
        mode_indicator::shown_color(_current_driver, r, g, b);
        board_api::set_led_color(r, g, b);
        s_led.invalidate();
    }
    if (options.status_led_pico != s_pico || options.status_led_pico_ext != s_external ||
        options.status_led_brightness != s_brightness) {
        s_pico = options.status_led_pico;
        s_external = options.status_led_pico_ext;
        s_brightness = options.status_led_brightness;
        s_led.invalidate();
    }
    bool on = false;
    uint32_t next_ms = 0;
    if (s_led.step(search, on, next_ms)) {
        board_api::set_led(on);
    }
    s_next_ms = now + next_ms;
}

static void core1_task() {
    /* Custom fix: let Core0 park this core while it writes flash (UserSettings/NVSTool.h). Without
     * it, a settings save applied live (no restart) wrote flash while this core ran from it, which
     * could hang it; the main loop then stopped and the hang watchdog restarted the adapter. */
    multicore_lockout_victim_init();
    init_i2c_slave();
    while (true) {
        tight_loop_contents();
    }
}

void run_uart_bridge() {
    /* Programming: the ESP32 waits in its ROM bootloader (esptool then runs with --before
     * no_reset). Monitor: it keeps running, only its output is read. */
    if (!UARTBridgeDevice::monitor_mode) {
        esp32_api::enter_programming_mode();
    }

    OGXM_LOG("Entering UART Bridge mode\n");

    //Runs UART Bridge task, doesn't return unless programming is complete
    DeviceManager::get_instance().get_driver()->process(0, _gamepads[0]); 

    OGXM_LOG("Exiting UART Bridge mode\n");

    board_api::usb::disconnect_all(); 
    UserSettings::get_instance().write_datetime();
    board_api::reboot();
}

bool update_needed(UserSettings& user_settings) {
#if defined(OGXM_RETAIL)
    return !user_settings.verify_datetime();
#endif
    return false;
}

void esp32_bp32_i2c::initialize() {
    diag::set_latency_gamepad(&_gamepads[0]);  // Custom: input-to-use latency in the diagnostics
    boot_stage::set(boot_stage::Stage::BOARD_INIT);
    board_api::init_board();
    boot_stage::set(boot_stage::Stage::ESP32_PINS);
    esp32_api::init();

    boot_stage::set(boot_stage::Stage::FLASH);
    UserSettings& user_settings = UserSettings::get_instance();
    user_settings.initialize_flash();
    flash_log::init(board_api::ms_since_boot());  // Custom: the kept log in flash (Board/FlashLog.h)

    for (uint8_t i = 0; i < UserSettings::MAX_PROFILES; ++i) {
        _profiles[i] = user_settings.get_profile_by_id(i + 1);
    }
    _profiles_crc = Esp32Bridge::profiles_crc(_profiles, UserSettings::MAX_PROFILES);
    for (uint8_t i = 0; i < MAX_GAMEPADS; ++i) {
        _active_profile[i] = user_settings.get_active_profile_id(i);
    }
    _current_driver = user_settings.get_current_driver();

    /* Custom: the RGB status LED lights in the output mode colour (brightness applied when it is
     * written, from the dongle options). */
    uint8_t led_r, led_g, led_b;
    mode_indicator::shown_color(_current_driver, led_r, led_g, led_b);
    board_api::set_led_color(led_r, led_g, led_b);

    /* UART bridge: GP21 to GND at boot (jumper), a retail update, or asked over USB in Web App
     * mode (Board/BootMode.h: ESP32 update or log monitor), so the adapter's USB port does it all. */
    const boot_mode::Mode boot = boot_mode::take();
    UARTBridgeDevice::monitor_mode = (boot == boot_mode::Mode::ESP32_MONITOR);
    _uart_bridge_mode = 
        (esp32_api::uart_bridge_mode() || update_needed(user_settings) ||
         boot == boot_mode::Mode::ESP32_FLASH || boot == boot_mode::Mode::ESP32_MONITOR);

    DeviceDriverType driver_type = 
        _uart_bridge_mode ? 
            DeviceDriverType::UART_BRIDGE : user_settings.get_current_driver();

    boot_stage::set(boot_stage::Stage::DRIVER);
    DeviceManager::get_instance().initialize_driver(driver_type, _gamepads);
}

void esp32_bp32_i2c::run() {
    if (_uart_bridge_mode) {
        /* Waits for the computer as long as it takes: no boot watchdog (leaving the bridge arms
         * one again, usb::disconnect_all). */
        boot_stage::set(boot_stage::Stage::UART_BRIDGE);
        watchdog_disable();
        run_uart_bridge();
        return;
    }

    boot_stage::set(boot_stage::Stage::CORE1);
    multicore_reset_core1();
    multicore_launch_core1(core1_task);
#if defined(CONFIG_EN_USB_HOST)
    boot_stage::set(boot_stage::Stage::USB_HOST);
    HostManager::get_instance().initialize(_gamepads);
#endif

    /* The ESP32's serial output (its log, boot messages) into the kept log (Board/LogRing.h),
     * readable later through the monitor mode. */
    boot_stage::set(boot_stage::Stage::ESP32_UART);
    uart_init(uart0, 115200);
    gpio_set_function(UART0_TX_PIN, GPIO_FUNC_UART);
#if defined(UART0_TX_ALT_PIN)
    gpio_set_function(UART0_TX_ALT_PIN, GPIO_FUNC_UART);  // same TX signal, second pad (Config.h)
#endif
    gpio_set_function(UART0_RX_PIN, GPIO_FUNC_UART);
    esp_rx_start();

    boot_stage::set(boot_stage::Stage::ESP32_RESET);
    esp32_api::reset();

    DeviceDriver* device_driver = DeviceManager::get_instance().get_driver();

    boot_stage::set(boot_stage::Stage::USB_DEVICE);
    tud_init(BOARD_TUD_RHPORT);

    /* Hang recovery: a stuck main loop (USB, the ESP32's log, the USB host port) reboots the
     * adapter instead of leaving it frozen; the report then shows "watchdog timeout (hang
     * recovery)" as the last reset. The settings writes in the loop take well under this. The
     * reboots that arm the watchdog themselves (mode change, flash writes) never come back here. */
    watchdog_enable(kHangWatchdogMs, true);
    boot_stage::set(boot_stage::Stage::RUNNING);
    mode_indicator::begin(_current_driver);

    while (true) {
        watchdog_update();
        boot_stage::set(boot_stage::Stage::LOOP_TASKS);
        TaskQueue::Core0::process_tasks();
        boot_stage::set(boot_stage::Stage::LOOP_FLASH_LOG);
        flash_log::tick(board_api::ms_since_boot(), true);
        diag::tick(board_api::ms_since_boot());  // Custom fix: per-second rates roll over every second, not only on web app requests

        boot_stage::set(boot_stage::Stage::LOOP_USB);
        for (uint8_t i = 0; i < MAX_GAMEPADS; ++i) {
            device_driver->process(i, _gamepads[i]);
            tud_task();
        }
        /* The ESP32's output into the kept log a whole line at a time, so the RP2040's own lines
         * never land in the middle of one; a line left unfinished goes in after 100 ms. */
        boot_stage::set(boot_stage::Stage::LOOP_ESP_RX);
        static char esp_line[192];
        static size_t esp_len = 0;
        static uint32_t esp_last_ms = 0;
        const uint32_t now_ms = board_api::ms_since_boot();
        auto esp_byte = [&](uint8_t b) {
            const char c = static_cast<char>(b);
            esp_line[esp_len++] = c;
            esp_last_ms = now_ms;
            if (c == '\n' || esp_len == sizeof(esp_line)) {
                log_ring_append("esp", esp_line, esp_len);
                esp_len = 0;
            }
        };
        uint32_t lost = 0;
        if (s_esp_rx_dma >= 0) {
            lost = s_esp_rx.drain(esp_rx_produced(), esp_byte);
        } else {
            while (uart_is_readable(uart0))  // no DMA channel: the FIFO, as before
                esp_byte(static_cast<uint8_t>(uart_getc(uart0)));
        }
        if (lost) {
            char note[40];
            const int n = std::snprintf(note, sizeof(note), "\n(%lu bytes lost)\n", static_cast<unsigned long>(lost));
            log_ring_append("esp", note, static_cast<size_t>(n));
        }
        if (esp_len && now_ms - esp_last_ms >= 100) {
            log_ring_append("esp", esp_line, esp_len);
            esp_len = 0;
        }
#if defined(CONFIG_EN_USB_HOST)
        boot_stage::set(boot_stage::Stage::LOOP_USB_HOST);
        usb_host_tick();
#endif
        boot_stage::set(boot_stage::Stage::LOOP_LED);
        status_led_tick();
        sleep_ms(1);
    }
}

// #else // OGXM_BOARD == ESP32_BLUEPAD32_I2C

// void esp32_bp32_i2c::initialize() {}
// void esp32_bp32_i2c::run() {}

#endif // OGXM_BOARD == ESP32_BLUEPAD32_I2C