#ifndef _I2C_DRIVER_H_
#define _I2C_DRIVER_H_

#include <cstdint>
#include <cstring>
#include <array>
#include <atomic>
#include <functional>
#include <driver/i2c.h>
#include <esp_system.h>

#include "sdkconfig.h"
#include "RingBuffer.h"
#include "I2C/Esp32Bridge.h"
#include "Gamepad/HdRumbleFeed.h"
#include "UserSettings/UserProfile.h"

class I2CDriver 
{
public:
    static constexpr bool MULTI_SLAVE = 
#if CONFIG_MULTI_SLAVE_MODE == 0
        false;
#else
        true;
#endif

    // Wire format shared with the RP2040 firmware: I2C/Esp32Bridge.h
    using PacketID = Esp32Bridge::PacketID;
    using PacketIn = Esp32Bridge::PacketIn;
    using PacketOut = Esp32Bridge::PacketOut;
    using PacketInExt = Esp32Bridge::PacketInExt;
    using PacketOutExt = Esp32Bridge::PacketOutExt;
    using PacketDiag = Esp32Bridge::PacketDiag;
    using PacketProfile = Esp32Bridge::PacketProfile;
    using PacketBlob = Esp32Bridge::PacketBlob;
    using PacketPadRaw = Esp32Bridge::PacketPadRaw;
    using PacketHdRumble = Esp32Bridge::PacketHdRumble;
    using PacketHdRumbleOut = Esp32Bridge::PacketHdRumbleOut;
    static constexpr uint8_t PROFILE_COUNT = 8;  // UserSettings::MAX_PROFILES on both sides
    using Profiles = std::array<UserProfile, PROFILE_COUNT>;
    enum class PacketResp : uint8_t { OK = 1, ERROR };

    I2CDriver() = default;
    ~I2CDriver();

    void initialize_i2c(i2c_port_t i2c_port, gpio_num_t sda, gpio_num_t scl, uint32_t clk_speed);

    //Does not return
    void run_tasks();

    void write_packet(uint8_t address, const PacketIn& data_in);
    void read_packet(uint8_t address, std::function<void(const PacketOut&)> callback);
    void write_packet(uint8_t address, const PacketInExt& data_in);
    void write_packet(uint8_t address, const PacketDiag& data_in);
    void write_packet(uint8_t address, const PacketPadRaw& data_in);
    /* GET_EXT request, then the RP2040's PacketOutExt (an older RP2040 answers a PacketOut,
     * which is dropped). */
    void read_ext(uint8_t address, uint8_t index, std::function<void(const PacketOutExt&)> callback);
    /* Switch mode HD Rumble (GET_HD_RUMBLE): ask with the last report `rx` took, call `callback` with
     * a new one. `rx` lives with the caller and is used on this task only. */
    void read_hd_rumble(uint8_t address, uint8_t index, hd_rumble::LinkReceiver& rx,
                        std::function<void(const hd_rumble::Frame&)> callback);
    /* Every profile the RP2040 keeps (GET_PROFILE, chunk by chunk, in one go). */
    void read_profiles(uint8_t address, std::function<void(const Profiles&)> callback);
    /* A profile saved over BLE, to the RP2040 (SET_PROFILE chunks). */
    void write_profile(uint8_t address, const UserProfile& profile, uint8_t player_idx, DeviceDriverType driver);
    /* A settings blob kept by the RP2040 (GET_BLOB chunks), and one saved over BLE (SET_BLOB). */
    void read_blob(uint8_t address, uint8_t kind, std::function<void(const uint8_t* data, size_t len)> callback);
    void write_blob(uint8_t address, uint8_t kind, const uint8_t* data, size_t len);

    /* For the diagnostics report: since boot. */
    uint32_t write_errors() const { return write_errors_.load(); }
    uint32_t read_errors() const { return read_errors_.load(); }
    uint32_t queue_drops() const { return queue_drops_.load(); }
    /* Reads that completed but carried another packet than the one asked for. */
    uint32_t wrong_replies() const { return wrong_replies_.load(); }

    /* Custom fix: restart the ESP32 from this task, before its next transfer. Restarting from a
     * timer could cut a transfer in the middle (a pad that kept dropping restarted the ESP32 over
     * and over, 2026-10-25), leaving the RP2040 halfway through a reply. */
    void restart_between_transfers() { restart_requested_.store(true); }

private:
    using TaskQueue = RingBuffer<std::function<void()>, CONFIG_I2C_RING_BUFFER_SIZE>;

    std::atomic<uint32_t> write_errors_{0};
    std::atomic<uint32_t> read_errors_{0};
    std::atomic<uint32_t> queue_drops_{0};
    std::atomic<uint32_t> wrong_replies_{0};
    std::atomic<bool> restart_requested_{false};

    /* The bus timing of one rate (as i2c_param_config sets it). */
    struct BusTiming
    {
        int high{0}, low{0};
        int start_setup{0}, start_hold{0};
        int stop_setup{0}, stop_hold{0};
        int data_sample{0}, data_hold{0};
        int timeout{0};
    };
    BusTiming read_timing_;
    BusTiming write_timing_;
    bool reading_{false};  // the bus is at the read rate

    BusTiming get_timing() const;
    void set_rate(bool reading);

    void restart_if_requested()
    {
        if (restart_requested_.load())
        {
            esp_restart();
        }
    }

    void push(std::function<void()> task)
    {
        if (!task_queue_.push(task))
        {
            ++queue_drops_;
        }
    }
    esp_err_t write(uint8_t address, const void* data, size_t len)
    {
        restart_if_requested();
        set_rate(false);
        const esp_err_t ret = i2c_write_blocking(address, reinterpret_cast<const uint8_t*>(data), len);
        if (ret != ESP_OK)
        {
            ++write_errors_;
        }
        return ret;
    }
    esp_err_t read(uint8_t address, void* data, size_t len)
    {
        restart_if_requested();
        set_rate(true);
        const esp_err_t ret = i2c_read_blocking(address, reinterpret_cast<uint8_t*>(data), len);
        if (ret != ESP_OK)
        {
            ++read_errors_;
        }
        return ret;
    }
    
    /* A 37-byte read takes about 0.9 ms at 400 kHz: 2 ticks (1-2 ms, as the call falls between
     * ticks) left little margin. */
    static constexpr TickType_t kTransferTimeoutTicks = pdMS_TO_TICKS(4);

    TaskQueue task_queue_;
    i2c_port_t i2c_port_ = I2C_NUM_0;
    bool initialized_ = false;

    static inline esp_err_t i2c_write_blocking(uint8_t address, const uint8_t* buffer, size_t len) 
    {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write(cmd, buffer, len, true);
        i2c_master_stop(cmd);

        esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, kTransferTimeoutTicks);
        i2c_cmd_link_delete(cmd);
        return ret;
    }

    static inline esp_err_t i2c_read_blocking(uint8_t address, uint8_t* buffer, size_t len) 
    {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_READ, true);
        
        if (len > 1) 
        {
            i2c_master_read(cmd, buffer, len - 1, I2C_MASTER_ACK);
        }

        i2c_master_read_byte(cmd, buffer + len - 1, I2C_MASTER_NACK);
        i2c_master_stop(cmd);
        
        esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, kTransferTimeoutTicks);
        i2c_cmd_link_delete(cmd);
        return ret;
    }
}; // class I2CDriver

#endif // _I2C_DRIVER_H_