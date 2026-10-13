#include "Board/Config.h"
#if defined(CONFIG_EN_USB_HOST) && defined(PIO_USB_DP_PIN)

#include "USBHost/PioHostPort.h"

#include <pico/time.h>

#include "tusb.h"
#include "pio_usb.h"
#include "USBHost/PioUsbRelease.h"
#include "Board/board_api_private/board_api_private.h"

namespace pio_host_port {

static repeating_timer_t s_sof_timer;
static bool s_sof_timer_running = false;
static bool s_hw_claimed = false;  // PIO USB took its DMA channel, state machines and programs

/* PIO USB claims its TX DMA channel and three state machines and loads its programs on every host
 * start, and nothing gives them back (tuh_deinit has no PIO USB hcd_deinit): a second start, after
 * an unplug or a stop, would panic ("DMA channel N is already claimed"). Release them first. */
static void release_hw()
{
    pio_usb_release_hw();  // PioUsbRelease.c
    s_hw_claimed = false;
}

static bool sof_timer_cb(repeating_timer_t* rt)
{
    (void)rt;
    pio_usb_host_frame();
    return true;
}

bool sof_timer_running()
{
    return s_sof_timer_running;
}

void start_sof_timer()
{
    if (s_sof_timer_running) {
        return;
    }
    if (add_repeating_timer_us(-1000, sof_timer_cb, nullptr, &s_sof_timer)) {
        s_sof_timer_running = true;
    }
}

void stop_sof_timer()
{
    if (!s_sof_timer_running) {
        return;
    }
    cancel_repeating_timer(&s_sof_timer);
    s_sof_timer_running = false;
}

void service_frames()
{
    if (s_sof_timer_running) {
        return;
    }
    static absolute_time_t next_frame;
    static bool next_set = false;
    if (!next_set) {
        next_frame = get_absolute_time();
        next_set = true;
    }
    absolute_time_t now = get_absolute_time();
    while (absolute_time_diff_us(next_frame, now) >= 0) {
        pio_usb_host_frame();
        next_frame = delayed_by_us(next_frame, 1000);
        now = get_absolute_time();
    }
}

bool start(int tx_dma_channel)
{
    pio_usb_configuration_t pio_cfg = PIO_USB_CONFIG;
    pio_cfg.alarm_pool = nullptr;
    pio_cfg.skip_alarm_pool = true;
    if (tx_dma_channel >= 0) {
        pio_cfg.tx_ch = static_cast<uint8_t>(tx_dma_channel);
    }
    stop_sof_timer();
    if (s_hw_claimed) {
        (void)tuh_deinit(BOARD_TUH_RHPORT);  // no-op when the caller already took the stack down
        release_hw();
    }
    tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    board_api_usbh::suspend_line_irq();
    if (!tuh_init(BOARD_TUH_RHPORT)) {
        return false;
    }
    s_hw_claimed = true;
    start_sof_timer();
    return true;
}

void stop()
{
    stop_sof_timer();
    (void)tuh_deinit(BOARD_TUH_RHPORT);
}

} // namespace pio_host_port

#endif // CONFIG_EN_USB_HOST && PIO_USB_DP_PIN
