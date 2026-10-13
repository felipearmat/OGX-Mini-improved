#ifndef _I2C_DRIVER_H_
#define _I2C_DRIVER_H_

#include <cstdint>
#include <cstring>
#include <array>
#include <atomic>
#include <functional>
#include <driver/i2c.h>

#include "sdkconfig.h"
#include "RingBuffer.h"
#include "I2C/Esp32Bridge.h"
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

private:
    using TaskQueue = RingBuffer<std::function<void()>, CONFIG_I2C_RING_BUFFER_SIZE>;

    std::atomic<uint32_t> write_errors_{0};
    std::atomic<uint32_t> read_errors_{0};
    std::atomic<uint32_t> queue_drops_{0};

    void push(std::function<void()> task)
    {
        if (!task_queue_.push(task))
        {
            ++queue_drops_;
        }
    }
    esp_err_t write(uint8_t address, const void* data, size_t len)
    {
        const esp_err_t ret = i2c_write_blocking(address, reinterpret_cast<const uint8_t*>(data), len);
        if (ret != ESP_OK)
        {
            ++write_errors_;
        }
        return ret;
    }
    esp_err_t read(uint8_t address, void* data, size_t len)
    {
        const esp_err_t ret = i2c_read_blocking(address, reinterpret_cast<uint8_t*>(data), len);
        if (ret != ESP_OK)
        {
            ++read_errors_;
        }
        return ret;
    }
    
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

        esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(2));
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
        
        esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(2));
        i2c_cmd_link_delete(cmd);
        return ret;
    }
}; // class I2CDriver

#endif // _I2C_DRIVER_H_