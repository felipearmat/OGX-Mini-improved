#include "sdkconfig.h"
#include "Board/ExtRgbLed.h"

#if defined(CONFIG_ENABLE_EXT_RGB)

#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>

namespace ext_rgb_led
{

/* 10 MHz RMT clock: 0.1 us per tick. WS2812B bit timings: 0 = 0.3 us high + 0.9 us low,
 * 1 = 0.9 us high + 0.3 us low; the line idles low between writes (latch, writes are far apart). */
static constexpr uint32_t kResolutionHz = 10 * 1000 * 1000;

static rmt_channel_handle_t s_channel = nullptr;
static rmt_encoder_handle_t s_encoder = nullptr;
static uint8_t s_grb[3];  // RMT reads it during the transfer

void init()
{
    if (s_channel)
    {
        return;
    }
    rmt_tx_channel_config_t channel_config{};
    channel_config.gpio_num = static_cast<gpio_num_t>(CONFIG_EXT_RGB_PIN);
    channel_config.clk_src = RMT_CLK_SRC_DEFAULT;
    channel_config.resolution_hz = kResolutionHz;
    channel_config.mem_block_symbols = 64;
    channel_config.trans_queue_depth = 1;
    if (rmt_new_tx_channel(&channel_config, &s_channel) != ESP_OK)
    {
        s_channel = nullptr;
        return;
    }

    rmt_bytes_encoder_config_t encoder_config{};
    encoder_config.bit0.level0 = 1;
    encoder_config.bit0.duration0 = 3;
    encoder_config.bit0.level1 = 0;
    encoder_config.bit0.duration1 = 9;
    encoder_config.bit1.level0 = 1;
    encoder_config.bit1.duration0 = 9;
    encoder_config.bit1.level1 = 0;
    encoder_config.bit1.duration1 = 3;
    encoder_config.flags.msb_first = 1;
    if (rmt_new_bytes_encoder(&encoder_config, &s_encoder) != ESP_OK || rmt_enable(s_channel) != ESP_OK)
    {
        rmt_del_channel(s_channel);
        s_channel = nullptr;
        return;
    }
    set(0, 0, 0);
}

void set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_channel)
    {
        return;
    }
    /* The previous write (36 us) is long done; wait anyway so its buffer is free. */
    rmt_tx_wait_all_done(s_channel, 10);
    s_grb[0] = g;
    s_grb[1] = r;
    s_grb[2] = b;
    rmt_transmit_config_t transmit_config{};
    rmt_transmit(s_channel, s_encoder, s_grb, sizeof(s_grb), &transmit_config);
}

} // namespace ext_rgb_led

#else // CONFIG_ENABLE_EXT_RGB

namespace ext_rgb_led
{
void init() {}
void set(uint8_t, uint8_t, uint8_t) {}
}

#endif // CONFIG_ENABLE_EXT_RGB
