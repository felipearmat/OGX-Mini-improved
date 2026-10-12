#include "Board/Config.h"
#include "OGXMini/Board/ESP32_Bluepad32_I2C.h"
#if (OGXM_BOARD == ESP32_BLUEPAD32_I2C)

#include <algorithm>
#include <cstring>
#include <pico/multicore.h>
#include <pico/i2c_slave.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>

#include "tusb.h"
#include "bsp/board_api.h"

#include "USBDevice/DeviceManager.h"
#include "UserSettings/UserSettings.h"
#include "Board/board_api.h"
#include "Board/esp32_api.h"
#include "Gamepad/Gamepad.h"
#include "Gamepad/MotionImu.h"
#include "I2C/Esp32Bridge.h"
#include "UserSettings/DongleSettings.h"
#include "Diagnostics/Diagnostics.h"
#include "USBDevice/DeviceDriver/MotionOutputActive.h"
#include "TaskQueue/TaskQueue.h"

// Wire format shared with the ESP32 companion firmware: I2C/Esp32Bridge.h
using Esp32Bridge::PacketID;
using Esp32Bridge::PacketIn;
using Esp32Bridge::PacketOut;
using Esp32Bridge::PacketInExt;
using Esp32Bridge::PacketOutExt;
using Esp32Bridge::PacketDiag;
using Esp32Bridge::PacketProfile;

/* The settings live here (USB web app, flash); the ESP32 maps its pads with them and its BLE web
 * app edits them through the I2C link (GET_PROFILE / SET_PROFILE). Saving reboots the RP2040, so
 * the copy taken at boot stays current. */
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
            diag::searching(diag.data[0] != 0);
            break;
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
        const int32_t gyro[3] = {ext.gyro[0], ext.gyro[1], ext.gyro[2]};
        MotionImu::apply_bt_motion(pad_in, ext.motion_source, accel, gyro,
                                   (ext.flags & PacketInExt::JOYCON_LEFT) != 0,
                                   (ext.flags & PacketInExt::JOYCON_RIGHT) != 0,
                                   (ext.flags & PacketInExt::JOYCON_PAIRED) != 0,
                                   _wii_gyro[packet.index], to_ms_since_boot(get_absolute_time()));
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
    reply.settings = dongle_settings::get();
    reply.driver = _current_driver;
    reply.active_profile = index < MAX_GAMEPADS ? _active_profile[index] : 1;
    reply.profiles_crc = _profiles_crc;
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
    static PacketProfile profile_request;  // answer the next read with this chunk
    static bool profile_requested = false;
    static uint8_t incoming[sizeof(UserProfile)];  // a profile saved over BLE, chunk by chunk
    static_assert(sizeof(PacketInExt) <= sizeof(PacketIn), "the receive buffer holds every ESP32 packet");
    static DeviceDriverType current_device_type = 
        UserSettings::get_instance().get_current_driver();

    switch (event) {
        case I2C_SLAVE_RECEIVE:
            if (count < sizeof(PacketIn)) {
                reinterpret_cast<uint8_t*>(&packet_in)[count] = i2c_read_byte_raw(i2c);
                ++count;
            }
            break;
        case I2C_SLAVE_FINISH:
            if (count == 0) {
                break;
            }
            switch (packet_in.packet_id) {
                case PacketID::SET_PAD:
                    if (packet_in.index < MAX_GAMEPADS) {
                        _gamepads[packet_in.index].set_pad_in(pad_in_from_packet(packet_in));
                    }
                    if (packet_in.bt_slot > 0 && packet_in.bt_slot <= diag::kSlots) {
                        diag::slot_report(packet_in.bt_slot - 1u, to_ms_since_boot(get_absolute_time()));
                    }
                    break;
                case PacketID::SET_DIAG: {
                    PacketDiag diag_packet;
                    std::memcpy(&diag_packet, &packet_in, sizeof(diag_packet));
                    record_diag(diag_packet);
                    break;
                }
                case PacketID::SET_PAD_EXT: {
                    PacketInExt ext;
                    std::memcpy(&ext, &packet_in, sizeof(ext));
                    if (ext.index < MAX_GAMEPADS) {
                        _pad_ext[ext.index] = ext;
                    }
                    break;
                }
                case PacketID::GET_EXT:
                    ext_requested = true;
                    break;
                case PacketID::GET_PROFILE:
                    std::memcpy(&profile_request, &packet_in, sizeof(profile_request));
                    profile_requested = true;
                    break;
                case PacketID::SET_PROFILE: {
                    PacketProfile chunk;
                    std::memcpy(&chunk, &packet_in, sizeof(chunk));
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
                case PacketID::SET_DRIVER:
                    if (packet_in.device_driver != DeviceDriverType::NONE &&
                        packet_in.device_driver != current_device_type) {
                        OGXM_LOG("I2C: Driver change detected.\n");
                        //Any writes to flash should be done on Core0
                        TaskQueue::Core0::queue_delayed_task(
                            TaskQueue::Core0::get_new_task_id(), 1000, false, 
                            [new_device_type = packet_in.device_driver] { 
                                UserSettings::get_instance().store_driver_type(new_device_type);
                            }
                        );
                    }
                    break;
                default:
                    break;
            }
            count = 0;
            break;
        case I2C_SLAVE_REQUEST:
            if (profile_requested) {
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
                i2c_write_raw_blocking(i2c, reinterpret_cast<const uint8_t*>(&reply), reply.packet_len);
                break;
            }
            if (ext_requested) {
                ext_requested = false;
                const PacketOutExt reply = ext_reply(packet_in.index);
                i2c_write_raw_blocking(i2c, reinterpret_cast<const uint8_t*>(&reply), reply.packet_len);
                break;
            }
            if (packet_in.index < MAX_GAMEPADS) {
                packet_out.index = packet_in.index;
                /* Custom fix: the strongest rumble since the ESP32's last read, as the Pico W
                 * Bluetooth path does; the current value lost host pulses shorter than the
                 * ESP32's read period. */
                const Gamepad::PadOut pad_out = _gamepads[packet_in.index].take_pad_out_peak();
                packet_out.rumble_l = pad_out.rumble_l;
                packet_out.rumble_r = pad_out.rumble_r;
            }
            i2c_write_raw_blocking( i2c, 
                                    reinterpret_cast<const uint8_t*>(&packet_out), 
                                    packet_out.packet_len);
            break;
        default:
            break;
    }
}

static void core1_task() {
    i2c_init(I2C_PORT, I2C_BAUDRATE);

    gpio_init(I2C_SDA_PIN);
    gpio_init(I2C_SCL_PIN);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    i2c_slave_init(I2C_PORT, Esp32Bridge::SLAVE_ADDR, &slave_handler);

    OGXM_LOG("I2C Driver initialized\n");

    while (true) {
        tight_loop_contents();
    }
}

void run_uart_bridge() {
    esp32_api::enter_programming_mode();

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
    board_api::init_board();
    esp32_api::init();

    UserSettings& user_settings = UserSettings::get_instance();
    user_settings.initialize_flash();

    for (uint8_t i = 0; i < UserSettings::MAX_PROFILES; ++i) {
        _profiles[i] = user_settings.get_profile_by_id(i + 1);
    }
    _profiles_crc = Esp32Bridge::profiles_crc(_profiles, UserSettings::MAX_PROFILES);
    for (uint8_t i = 0; i < MAX_GAMEPADS; ++i) {
        _active_profile[i] = user_settings.get_active_profile_id(i);
    }
    _current_driver = user_settings.get_current_driver();

    //MODE_SEL_PIN is used to determine if UART bridge should be run
    _uart_bridge_mode = 
        (esp32_api::uart_bridge_mode() || update_needed(user_settings));

    DeviceDriverType driver_type = 
        _uart_bridge_mode ? 
            DeviceDriverType::UART_BRIDGE : user_settings.get_current_driver();

    DeviceManager::get_instance().initialize_driver(driver_type, _gamepads);
}

void esp32_bp32_i2c::run() {
    if (_uart_bridge_mode) {
        run_uart_bridge();
        return;
    }

    multicore_reset_core1();
    multicore_launch_core1(core1_task);

    esp32_api::reset();

    DeviceDriver* device_driver = DeviceManager::get_instance().get_driver();

    tud_init(BOARD_TUD_RHPORT);

    while (true) {
        TaskQueue::Core0::process_tasks();

        for (uint8_t i = 0; i < MAX_GAMEPADS; ++i) {
            device_driver->process(i, _gamepads[i]);
            tud_task();
        }
        sleep_ms(1);
    }
}

// #else // OGXM_BOARD == ESP32_BLUEPAD32_I2C

// void esp32_bp32_i2c::initialize() {}
// void esp32_bp32_i2c::run() {}

#endif // OGXM_BOARD == ESP32_BLUEPAD32_I2C