#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <driver/gpio.h>
#include <esp_log.h>

#include <algorithm>

#include "I2CDriver/I2CDriver.h"

I2CDriver::~I2CDriver()
{
    i2c_driver_delete(i2c_port_);
}

void I2CDriver::initialize_i2c(i2c_port_t i2c_port, gpio_num_t sda, gpio_num_t scl, uint32_t clk_speed)
{
    if (initialized_)
    {
        return;
    }

    i2c_port_ = i2c_port;

    i2c_config_t conf;
    std::memset(&conf, 0, sizeof(i2c_config_t));

    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = sda;
    conf.scl_io_num = scl;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    /* Custom fix: reads at CONFIG_I2C_READ_BAUDRATE, writes at clk_speed (Kconfig). The bus
     * timing of each rate is taken from the driver's own setup, then switched between
     * transfers. */
    conf.master.clk_speed = CONFIG_I2C_READ_BAUDRATE;
    i2c_param_config(i2c_port_, &conf);
    read_timing_ = get_timing();

    conf.master.clk_speed = clk_speed;
    i2c_param_config(i2c_port_, &conf);
    write_timing_ = get_timing();
    reading_ = false;

    i2c_driver_install(i2c_port_, conf.mode, 0, 0, 0);

    initialized_ = true;
}

I2CDriver::BusTiming I2CDriver::get_timing() const
{
    BusTiming t;
    i2c_get_period(i2c_port_, &t.high, &t.low);
    i2c_get_start_timing(i2c_port_, &t.start_setup, &t.start_hold);
    i2c_get_stop_timing(i2c_port_, &t.stop_setup, &t.stop_hold);
    i2c_get_data_timing(i2c_port_, &t.data_sample, &t.data_hold);
    i2c_get_timeout(i2c_port_, &t.timeout);
    return t;
}

void I2CDriver::set_rate(bool reading)
{
    if (reading == reading_)
    {
        return;
    }
    reading_ = reading;
    const BusTiming& t = reading ? read_timing_ : write_timing_;
    i2c_set_period(i2c_port_, t.high, t.low);
    i2c_set_start_timing(i2c_port_, t.start_setup, t.start_hold);
    i2c_set_stop_timing(i2c_port_, t.stop_setup, t.stop_hold);
    i2c_set_data_timing(i2c_port_, t.data_sample, t.data_hold);
    i2c_set_timeout(i2c_port_, t.timeout);
}

void I2CDriver::run_tasks()
{
    std::function<void()> task;

    while (true)
    {   
        while (task_queue_.pop(task))
        {
            task();
        }

        restart_if_requested();  // nothing queued: no transfer to wait for
        vTaskDelay(1);
    }
}

void I2CDriver::write_packet(uint8_t address, const PacketIn& data_in) 
{
    push([this, address, data_in]() 
    {
        write(address, &data_in, sizeof(PacketIn));
    });
}

void I2CDriver::read_packet(uint8_t address, std::function<void(const PacketOut&)> callback) 
{
    push([this, address, callback]() 
    {
        PacketOut data_out;
        if (read(address, &data_out, sizeof(PacketOut)) == ESP_OK)
        {
            if (data_out.packet_id == PacketID::GET_PAD)
            {
                callback(data_out);
            }
            else
            {
                ++wrong_replies_;
            }
        }
    });
}

void I2CDriver::write_packet(uint8_t address, const PacketInExt& data_in)
{
    push([this, address, data_in]()
    {
        write(address, &data_in, sizeof(PacketInExt));
    });
}

void I2CDriver::write_packet(uint8_t address, const PacketDiag& data_in)
{
    push([this, address, data_in]()
    {
        write(address, &data_in, sizeof(PacketDiag));
    });
}

void I2CDriver::write_packet(uint8_t address, const PacketPadRaw& data_in)
{
    push([this, address, data_in]()
    {
        write(address, &data_in, sizeof(PacketPadRaw));
    });
}

void I2CDriver::read_ext(uint8_t address, uint8_t index, std::function<void(const PacketOutExt&)> callback)
{
    push([this, address, index, callback]()
    {
        PacketIn request;
        request.packet_id = PacketID::GET_EXT;
        request.index = index;
        if (write(address, &request, sizeof(PacketIn)) != ESP_OK)
        {
            return;
        }
        PacketOutExt data_out;
        if (read(address, &data_out, sizeof(PacketOutExt)) == ESP_OK)
        {
            if (data_out.packet_id == PacketID::GET_EXT)
            {
                callback(data_out);
            }
            else
            {
                ++wrong_replies_;
            }
        }
    });
}

void I2CDriver::read_profiles(uint8_t address, std::function<void(const Profiles&)> callback)
{
    push([this, address, callback]()
    {
        static Profiles profiles;
        for (uint8_t id = 1; id <= PROFILE_COUNT; ++id)
        {
            for (uint8_t chunk = 0; chunk < PacketProfile::kChunks; ++chunk)
            {
                PacketProfile request;
                request.packet_id = PacketID::GET_PROFILE;
                request.profile_id = id;
                request.chunk = chunk;
                PacketProfile reply;
                if (write(address, &request, sizeof(request)) != ESP_OK ||
                    read(address, &reply, sizeof(reply)) != ESP_OK ||
                    reply.packet_id != PacketID::GET_PROFILE || reply.profile_id != id || reply.chunk != chunk)
                {
                    return;  // tried again on a later feedback tick
                }
                const size_t offset = chunk * PacketProfile::kData;
                const size_t len = std::min(PacketProfile::kData, sizeof(UserProfile) - offset);
                std::memcpy(reinterpret_cast<uint8_t*>(&profiles[id - 1]) + offset, reply.data, len);
            }
        }
        callback(profiles);
    });
}

void I2CDriver::write_profile(uint8_t address, const UserProfile& profile, uint8_t player_idx, DeviceDriverType driver)
{
    push([this, address, profile, player_idx, driver]()
    {
        for (uint8_t chunk = 0; chunk < PacketProfile::kChunks; ++chunk)
        {
            PacketProfile packet;
            packet.packet_id = PacketID::SET_PROFILE;
            packet.profile_id = profile.id;
            packet.chunk = chunk;
            packet.player_idx = player_idx;
            packet.driver = driver;
            const size_t offset = chunk * PacketProfile::kData;
            const size_t len = std::min(PacketProfile::kData, sizeof(UserProfile) - offset);
            std::memcpy(packet.data, reinterpret_cast<const uint8_t*>(&profile) + offset, len);
            write(address, &packet, sizeof(packet));
        }
    });
}

void I2CDriver::read_blob(uint8_t address, uint8_t kind, std::function<void(const uint8_t* data, size_t len)> callback)
{
    push([this, address, kind, callback]()
    {
        uint8_t blob[PacketBlob::kMaxLen];
        size_t total = 0;
        for (uint8_t chunk = 0; chunk * PacketBlob::kData < PacketBlob::kMaxLen; ++chunk)
        {
            PacketBlob request;
            request.packet_id = PacketID::GET_BLOB;
            request.kind = kind;
            request.chunk = chunk;
            PacketBlob reply;
            if (write(address, &request, sizeof(request)) != ESP_OK ||
                read(address, &reply, sizeof(reply)) != ESP_OK ||
                reply.packet_id != PacketID::GET_BLOB || reply.kind != kind || reply.chunk != chunk ||
                reply.total_len == 0 || reply.total_len > PacketBlob::kMaxLen)
            {
                return;
            }
            total = reply.total_len;
            const size_t offset = chunk * PacketBlob::kData;
            std::memcpy(blob + offset, reply.data, std::min(PacketBlob::kData, total - offset));
            if (offset + PacketBlob::kData >= total)
            {
                break;
            }
        }
        callback(blob, total);
    });
}

void I2CDriver::write_blob(uint8_t address, uint8_t kind, const uint8_t* data, size_t len)
{
    if (len == 0 || len > PacketBlob::kMaxLen)
    {
        return;
    }
    std::array<uint8_t, PacketBlob::kMaxLen> copy{};
    std::memcpy(copy.data(), data, len);
    push([this, address, kind, copy, len]()
    {
        for (size_t offset = 0; offset < len; offset += PacketBlob::kData)
        {
            PacketBlob packet;
            packet.packet_id = PacketID::SET_BLOB;
            packet.kind = kind;
            packet.chunk = static_cast<uint8_t>(offset / PacketBlob::kData);
            packet.total_len = static_cast<uint8_t>(len);
            std::memcpy(packet.data, copy.data() + offset, std::min(PacketBlob::kData, len - offset));
            write(address, &packet, sizeof(packet));
        }
    });
}
