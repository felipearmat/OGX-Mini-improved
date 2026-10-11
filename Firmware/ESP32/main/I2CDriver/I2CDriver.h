#ifndef _I2C_DRIVER_H_
#define _I2C_DRIVER_H_

#include <cstdint>
#include <cstring>
#include <functional>
#include <driver/i2c.h>

#include "sdkconfig.h"
#include "RingBuffer.h"
#include "I2C/Esp32Bridge.h"

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
    enum class PacketResp : uint8_t { OK = 1, ERROR };

    I2CDriver() = default;
    ~I2CDriver();

    void initialize_i2c(i2c_port_t i2c_port, gpio_num_t sda, gpio_num_t scl, uint32_t clk_speed);

    //Does not return
    void run_tasks();

    void write_packet(uint8_t address, const PacketIn& data_in);
    void read_packet(uint8_t address, std::function<void(const PacketOut&)> callback);

private:
    using TaskQueue = RingBuffer<std::function<void()>, CONFIG_I2C_RING_BUFFER_SIZE>;
    
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