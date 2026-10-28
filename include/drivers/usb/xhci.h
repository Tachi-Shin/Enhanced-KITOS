#ifndef USB_XHCI_H
#define USB_XHCI_H
#include <ekitos/driver.h>
struct xhci_protocol {
    uint8_t first, count, major, slot_type;
    uint8_t speed_ids[4]; /* indexed by enum usb_speed; 0 means unsupported */
};
int xhci_parse_protocol(const uint32_t header[4],const uint32_t *psi,struct xhci_protocol *);
/* One controller per module instance, USB 2 and 5 Gbit/s SuperSpeed. */
struct usb_host *xhci_host(const struct ek_driver_api *, uint64_t bar, uint64_t dma_offset);
#endif
