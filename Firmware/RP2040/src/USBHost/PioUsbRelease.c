/* C: Pico-PIO-USB's internal header (pio_usb_ll.h) does not compile as C++. See PioHostPort.cpp. */
#include "Board/Config.h"
#if defined(CONFIG_EN_USB_HOST) && defined(PIO_USB_DP_PIN)

#include <hardware/dma.h>
#include <hardware/pio.h>

#include "pio_usb.h"
#include "pio_usb_ll.h"
#include "usb_rx.pio.h"

#include "USBHost/PioUsbRelease.h"

void pio_usb_release_hw(void)
{
    pio_port_t* pp = &pio_port[0];
    pio_sm_set_enabled(pp->pio_usb_tx, pp->sm_tx, false);
    pio_sm_set_enabled(pp->pio_usb_rx, pp->sm_rx, false);
    pio_sm_set_enabled(pp->pio_usb_rx, pp->sm_eop, false);
    pio_remove_program(pp->pio_usb_tx, pp->fs_tx_program, pp->offset_tx);
    pio_remove_program(pp->pio_usb_rx, &usb_nrzi_decoder_program, pp->offset_rx);
    pio_remove_program(pp->pio_usb_rx, &usb_edge_detector_program, pp->offset_eop);
    pio_sm_unclaim(pp->pio_usb_tx, pp->sm_tx);
    pio_sm_unclaim(pp->pio_usb_rx, pp->sm_rx);
    pio_sm_unclaim(pp->pio_usb_rx, pp->sm_eop);
    dma_channel_abort(pp->tx_ch);
    dma_channel_unclaim(pp->tx_ch);
}

#endif
