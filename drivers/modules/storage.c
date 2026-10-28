#include <drivers/usb/msc.h>
const struct ek_driver_manifest ek_driver_manifest={EK_DRIVER_MAGIC,EK_DRIVER_ABI,sizeof(struct ek_driver_manifest),"usb-storage",usb_msc_init,usb_msc_fini};
