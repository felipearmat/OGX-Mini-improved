#ifndef _OGXM_PIO_HOST_PORT_H_
#define _OGXM_PIO_HOST_PORT_H_

#include <cstdint>

/* The USB host port on two GPIOs (Pico-PIO-USB), as every board with one starts and serves it: the
 * standard boards, the Pico W / Pico 2 W and the RP2040 + ESP32 hybrid. PIO USB has no hardware
 * SOF: pio_usb_host_frame() runs from a 1 kHz SDK repeating timer (the alarm pool PIO USB would use
 * collides with BTstack / TaskQueue timers), or from the caller's loop when that timer could not
 * start. */
namespace pio_host_port {

    /* Configure PIO USB, turn off the D+ / D- edge interrupt (PIO owns the lines: left on, the IO
     * IRQ floods), start the host stack and the SOF timer. tx_dma_channel: a fixed DMA channel for
     * PIO USB TX (-1: its default). False when the host stack did not start. Safe to call again
     * after stop() or tuh_deinit() (a replug): PIO USB's DMA channel, state machines and programs
     * from the previous start are released first. */
    bool start(int tx_dma_channel = -1);

    /* SOF timer off and the host stack down (unplug, mode change). */
    void stop();

    bool sof_timer_running();
    void start_sof_timer();
    void stop_sof_timer();

    /* From the caller's loop: the frames due when the SOF timer is not running. */
    void service_frames();

} // namespace pio_host_port

#endif // _OGXM_PIO_HOST_PORT_H_
