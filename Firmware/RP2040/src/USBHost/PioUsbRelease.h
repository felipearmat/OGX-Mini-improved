#ifndef _OGXM_PIO_USB_RELEASE_H_
#define _OGXM_PIO_USB_RELEASE_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Give back what Pico-PIO-USB took on its last start: the TX DMA channel, the three state machines
 * and the programs (it has no hcd_deinit). Only after a start, with the host stack down. */
void pio_usb_release_hw(void);

#ifdef __cplusplus
}
#endif

#endif // _OGXM_PIO_USB_RELEASE_H_
