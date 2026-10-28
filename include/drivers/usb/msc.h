#ifndef USB_MSC_H
#define USB_MSC_H
#include <ekitos/driver.h>
int usb_msc_init(const struct ek_driver_api *);
int usb_msc_fini(void);
#endif
