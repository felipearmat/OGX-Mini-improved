#include <cstring>
#include <string>
#include <algorithm>

#include "btstack.h"
#include "ble/att_server.h"  // not pulled in by btstack.h on every port

#include "BLEServer/att_delayed_response.h"  // generated per firmware from Bluepad32/att_delayed_response.gatt
#include "Bluepad32/BLEServer.h"
#include "Bluepad32/BleStore.h"
#include "UserSettings/UserProfile.h"

namespace BLEServer {

static constexpr uint16_t PACKET_LEN_MAX = 20;
static constexpr size_t kLivePadLen = 23;  // the web app's legacy pad layout (Gamepad/I2CWirePad.h)

namespace Handle {
    static constexpr uint16_t FW_VERSION    = ATT_CHARACTERISTIC_12345678_1234_1234_1234_123456789020_01_VALUE_HANDLE;
    static constexpr uint16_t FW_NAME       = ATT_CHARACTERISTIC_12345678_1234_1234_1234_123456789021_01_VALUE_HANDLE;

    static constexpr uint16_t SETUP_READ  = ATT_CHARACTERISTIC_12345678_1234_1234_1234_123456789030_01_VALUE_HANDLE;
    static constexpr uint16_t SETUP_WRITE = ATT_CHARACTERISTIC_12345678_1234_1234_1234_123456789031_01_VALUE_HANDLE;
    static constexpr uint16_t GET_SETUP   = ATT_CHARACTERISTIC_12345678_1234_1234_1234_123456789032_01_VALUE_HANDLE;

    static constexpr uint16_t PROFILE  = ATT_CHARACTERISTIC_12345678_1234_1234_1234_123456789040_01_VALUE_HANDLE;

    static constexpr uint16_t GAMEPAD  = ATT_CHARACTERISTIC_12345678_1234_1234_1234_123456789050_01_VALUE_HANDLE;
    /* Custom: dongle options (UserSettings/DongleSettings). */
    static constexpr uint16_t DONGLE_SETTINGS = ATT_CHARACTERISTIC_12345678_1234_1234_1234_123456789060_01_VALUE_HANDLE;
    /* Custom: mouse + keyboard mode mapping (USBDevice/DeviceDriver/KBM/KbmSettings, 48 bytes). */
    static constexpr uint16_t KBM_SETTINGS = ATT_CHARACTERISTIC_12345678_1234_1234_1234_123456789070_01_VALUE_HANDLE;
}

namespace ADV {
    // Flags general discoverable, BR/EDR not supported
    static const uint8_t FLAGS[]    = { 0x02, 0x01, 0x06 };
    static const uint8_t NAME_TYPE  = 0x09;

    #pragma pack(push, 1)
    struct Data {
        uint8_t flags[sizeof(FLAGS)];
        uint8_t name_len;
        uint8_t name_type;
        uint8_t name[sizeof(FIRMWARE_NAME) - 1];

        Data() {
            std::memcpy(flags, FLAGS, sizeof(flags));
            name_len = sizeof(FIRMWARE_NAME);
            name_type = NAME_TYPE;
            std::string fw_name = FIRMWARE_NAME;
            std::memcpy(name, fw_name.c_str(), std::min(sizeof(name), fw_name.size()));
        }
    };
    static_assert(sizeof(Data) == 5 + sizeof(FIRMWARE_NAME) - 1, "BLEServer::ADV::Data struct size mismatch");
    #pragma pack(pop)
}

#pragma pack(push, 1)
struct SetupPacket {
    DeviceDriverType device_type{DeviceDriverType::NONE};
    uint8_t max_gamepads{1};
    uint8_t player_idx{0};
    uint8_t profile_id{0};
};
static_assert(sizeof(SetupPacket) == 4, "BLEServer::SetupPacket struct size mismatch");
#pragma pack(pop)

class ProfileReader {
public:
    ProfileReader() = default;
    ~ProfileReader() = default;

    void set_setup_packet(const SetupPacket& setup_packet) {
        setup_packet_ = setup_packet;
        current_offset_ = 0;
    }

    const SetupPacket& get_setup_packet() const {
        return setup_packet_;
    }

    uint16_t get_xfer_len() {
        return static_cast<uint16_t>(std::min(static_cast<size_t>(PACKET_LEN_MAX), sizeof(UserProfile) - current_offset_));
    }

    uint16_t get_profile_data(uint8_t* buffer, uint16_t buffer_len) {
        size_t copy_len = get_xfer_len();
        if (!buffer || buffer_len < copy_len) {
            return 0;
        }
        if (current_offset_ == 0 && !set_profile()) {
            return 0;
        }
        std::memcpy(buffer, reinterpret_cast<uint8_t*>(&profile_) + current_offset_, copy_len);
        current_offset_ += copy_len;
        if (current_offset_ >= sizeof(UserProfile)) {
            current_offset_ = 0;
        }
        return copy_len;
    }

private:
    SetupPacket setup_packet_;
    UserProfile profile_;
    size_t current_offset_ = 0;

    bool set_profile() {
        if (setup_packet_.profile_id == 0xFF) {
            if (setup_packet_.player_idx >= ble_store::kMaxProfiles) {
                return false;
            }
            profile_ = ble_store::profile_by_index(setup_packet_.player_idx);
        } else {
            if (setup_packet_.profile_id > ble_store::kMaxProfiles) {
                return false;
            }
            profile_ = ble_store::profile_by_id(setup_packet_.profile_id);
        }
        return true;
    }
};

class ProfileWriter
{
public:
    ProfileWriter() = default;
    ~ProfileWriter() = default;

    void set_setup_packet(const SetupPacket& setup_packet) {
        setup_packet_ = setup_packet;
        current_offset_ = 0;
    }

    const SetupPacket& get_setup_packet() const {
        return setup_packet_;
    }

    uint16_t get_xfer_len() {
        return static_cast<uint16_t>(std::min(static_cast<size_t>(PACKET_LEN_MAX), sizeof(UserProfile) - current_offset_));
    }

    size_t set_profile_data(const uint8_t* buffer, uint16_t buffer_len) {
        size_t copy_len = get_xfer_len();
        if (!buffer || buffer_len < copy_len) {
            return 0;
        }

        std::memcpy(reinterpret_cast<uint8_t*>(&profile_) + current_offset_, buffer, copy_len);

        current_offset_ += copy_len;
        size_t ret = current_offset_;

        if (current_offset_ >= sizeof(UserProfile)) {
            current_offset_ = 0;
        }
        return ret;
    }

    bool commit_profile() {
        return ble_store::save_profile(setup_packet_.player_idx, profile_, setup_packet_.device_type);
    }

private:
    SetupPacket setup_packet_;
    UserProfile profile_;
    size_t current_offset_ = 0;
};

ProfileReader profile_reader_;
ProfileWriter profile_writer_;

static int verify_write(const uint16_t buffer_size, const uint16_t expected_size) {
    if (buffer_size != expected_size) {
        return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
    }
    return 0;
}

/* Custom: KBM settings can be longer than one ATT packet: reads go out in blobs (offset) and a
 * long write arrives as prepared writes, gathered here until the execute. */
static constexpr size_t kKbmSettingsLen = 48;  // USBDevice/DeviceDriver/KBM/KbmSettings.h
static uint8_t s_kbm_write_buf[kKbmSettingsLen];
static uint16_t s_kbm_write_len = 0;

static int kbm_settings_write(uint16_t transaction_mode, uint16_t offset, const uint8_t* buffer, uint16_t buffer_size) {
    if (transaction_mode == ATT_TRANSACTION_MODE_CANCEL) {
        s_kbm_write_len = 0;
        return 0;
    }
    if (transaction_mode == ATT_TRANSACTION_MODE_NONE || transaction_mode == ATT_TRANSACTION_MODE_ACTIVE) {
        if (transaction_mode == ATT_TRANSACTION_MODE_NONE)
            s_kbm_write_len = 0;
        if (static_cast<size_t>(offset) + buffer_size > sizeof(s_kbm_write_buf))
            return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
        std::memcpy(&s_kbm_write_buf[offset], buffer, buffer_size);
        if (offset + buffer_size > s_kbm_write_len)
            s_kbm_write_len = static_cast<uint16_t>(offset + buffer_size);
        if (transaction_mode == ATT_TRANSACTION_MODE_ACTIVE)
            return 0;  // wait for the execute
    }
    const bool ok = ble_store::save_kbm_settings(s_kbm_write_buf, s_kbm_write_len);
    s_kbm_write_len = 0;
    return ok ? 0 : ATT_ERROR_VALUE_NOT_ALLOWED;
}

static void disconnect_client_cb(btstack_timer_source_t *ts) {
    hci_con_handle_t connection_handle = *static_cast<hci_con_handle_t*>(ts->context);
    hci_send_cmd(&hci_disconnect, connection_handle);
    delete static_cast<hci_con_handle_t*>(ts->context);
}

static void queue_disconnect(hci_con_handle_t connection_handle, uint32_t dealy_ms) {
    static btstack_timer_source_t disconnect_timer;

    hci_con_handle_t* connection_handle_ptr = new hci_con_handle_t(connection_handle);

    disconnect_timer.process = disconnect_client_cb;
    disconnect_timer.context = connection_handle_ptr;

    btstack_run_loop_set_timer(&disconnect_timer, dealy_ms);
    btstack_run_loop_add_timer(&disconnect_timer);
}

static uint16_t att_read_callback(  hci_con_handle_t connection_handle,
                                    uint16_t att_handle,
                                    uint16_t offset,
                                    uint8_t *buffer,
                                    uint16_t buffer_size) {
    std::string fw_version;
    std::string fw_name;

    switch (att_handle) {
        case Handle::FW_VERSION:
            fw_version = FIRMWARE_VERSION;
            if (buffer)  {
                std::memcpy(buffer, reinterpret_cast<const uint8_t*>(fw_version.c_str()), fw_version.size());
            }
            return static_cast<uint16_t>(fw_version.size());

        case Handle::FW_NAME:
            fw_name = FIRMWARE_NAME;
            if (buffer) {
                std::memcpy(buffer, reinterpret_cast<const uint8_t*>(fw_name.c_str()), fw_name.size());;
            }
            return static_cast<uint16_t>(fw_name.size());

        case Handle::GET_SETUP:
            if (buffer) {
                buffer[0] = static_cast<uint8_t>(ble_store::current_driver());
                buffer[1] = ble_store::max_gamepads();
                buffer[2] = 0;
                buffer[3] = ble_store::active_profile(0);
            }
            return static_cast<uint16_t>(sizeof(SetupPacket));

        case Handle::PROFILE:
            if (buffer) {
                return profile_reader_.get_profile_data(buffer, buffer_size);
            }
            return profile_reader_.get_xfer_len();

        case Handle::DONGLE_SETTINGS:
            /* The record, then this hardware's capabilities (UserSettings/Capabilities.h, 4 bytes
             * little-endian): an older web app reads the record's length and ignores the rest. */
            {
                uint8_t data[sizeof(dongle_settings::Settings) + 4];
                std::memcpy(data, &ble_store::dongle_settings(), sizeof(dongle_settings::Settings));
                const uint32_t caps = ble_store::capabilities();
                for (int i = 0; i < 4; ++i)
                    data[sizeof(dongle_settings::Settings) + i] = static_cast<uint8_t>(caps >> (8 * i));
                // longer than one ATT packet: long reads come with an offset
                return att_read_callback_handle_blob(data, static_cast<uint16_t>(sizeof(data)), offset, buffer, buffer_size);
            }

        case Handle::KBM_SETTINGS:
        {
            uint8_t settings[kKbmSettingsLen]{};
            const size_t len = ble_store::kbm_settings(settings, sizeof(settings));
            return att_read_callback_handle_blob(settings, static_cast<uint16_t>(len), offset, buffer, buffer_size);
        }

        case Handle::GAMEPAD:
            /* Custom fix: legacy 23-byte layout expected by the web app (PadIn grew IMU and
             * touchpad fields), and never copy more than the ATT buffer holds. */
            if (buffer) {
                return static_cast<uint16_t>(ble_store::live_pad(buffer, buffer_size));
            }
            return static_cast<uint16_t>(kLivePadLen);

        default:
            break;
    }
    return 0;
}

static int att_write_callback(  hci_con_handle_t connection_handle,
                                uint16_t att_handle,
                                uint16_t transaction_mode,
                                uint16_t offset,
                                uint8_t *buffer,
                                uint16_t buffer_size) {
    int ret = 0;

    /* Custom: BTstack ends a long (prepared) write with handle 0; finish a pending KBM write. */
    if (att_handle == 0 && (transaction_mode == ATT_TRANSACTION_MODE_EXECUTE ||
                            transaction_mode == ATT_TRANSACTION_MODE_CANCEL)) {
        return s_kbm_write_len > 0 ? kbm_settings_write(transaction_mode, 0, nullptr, 0) : 0;
    }

    switch (att_handle) {
        case Handle::SETUP_READ:
            if ((ret = verify_write(buffer_size, sizeof(SetupPacket))) != 0) {
                break;
            }
            profile_reader_.set_setup_packet(*reinterpret_cast<SetupPacket*>(buffer));
            break;

        case Handle::SETUP_WRITE:
            if ((ret = verify_write(buffer_size, sizeof(SetupPacket))) != 0) {
                break;
            }
            profile_writer_.set_setup_packet(*reinterpret_cast<SetupPacket*>(buffer));
            break;

        case Handle::PROFILE:
            if ((ret = verify_write(buffer_size, profile_writer_.get_xfer_len())) != 0) {
                break;
            }
            if (profile_writer_.set_profile_data(buffer, buffer_size) == sizeof(UserProfile)) {
                queue_disconnect(connection_handle, 500);
                profile_writer_.commit_profile();
            }
            break;

        case Handle::DONGLE_SETTINGS:
        {
            dongle_settings::Settings settings{};
            /* Version 2 record, or version 1 from an older web app. */
            /* Any record version the firmware reads (an older web app writes an older one). */
            if (buffer_size != sizeof(settings) && buffer_size != dongle_settings::kV4Length &&
                buffer_size != dongle_settings::kV3Length && buffer_size != dongle_settings::kV1Length) {
                ret = ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
                break;
            }
            if (!dongle_settings::decode(buffer, buffer_size, settings)) {
                ret = ATT_ERROR_VALUE_NOT_ALLOWED;
                break;
            }
            /* Stored, then the board restarts, like a profile write. */
            queue_disconnect(connection_handle, 500);
            ble_store::save_dongle_settings(settings);
            break;
        }

        case Handle::KBM_SETTINGS:
            ret = kbm_settings_write(transaction_mode, offset, buffer, buffer_size);
            break;

        default:
            break;
    }
    return ret;
}

void init_server() {
    // setup ATT server
    att_server_init(profile_data, att_read_callback, att_write_callback);

    // setup advertisements
    static ADV::Data adv_data = ADV::Data();
    gap_advertisements_set_data(static_cast<uint8_t>(sizeof(adv_data)), reinterpret_cast<uint8_t*>(&adv_data));
    set_advertising(Advertising::Fast);
}

void set_advertising(Advertising mode) {
    static bool s_set = false;
    static Advertising s_mode = Advertising::Off;
    if (s_set && mode == s_mode) {
        return;
    }
    s_set = true;
    s_mode = mode;
    if (mode == Advertising::Off) {
        gap_advertisements_enable(0);
        return;
    }
    // 0.625 ms units: 30 ms fast, 300 ms slow
    const uint16_t interval = mode == Advertising::Fast ? 0x0030 : 0x01E0;
    bd_addr_t null_addr;
    std::memset(null_addr, 0, sizeof(null_addr));
    gap_advertisements_enable(0);
    gap_advertisements_set_params(interval, interval, 0, 0, null_addr, 0x07, 0x00);
    gap_advertisements_enable(1);
}

} // namespace BLEServer