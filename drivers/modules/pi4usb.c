#include <drivers/pci/bcm2711.h>
#include <drivers/usb/xhci.h>
#include <lib/string.h>
static const struct ek_driver_api *api;
static struct usb_host *host;
static int init(const struct ek_driver_api *a)
{
    if(a->abi!=EK_DRIVER_ABI || a->size<sizeof(*a))return -EK_ENOTSUP;
    if(strcmp(a->platform,"rpi4/hardware"))return -EK_ENODEV;
    api=a;uint64_t bar;int rc=bcm2711_vl805_start(a,&bar);if(rc)return rc;
    host=xhci_host(a,bar,0);if(!host)return -EK_EBUSY;
    rc=a->usb_attach_host(a->owner,host);
    if(rc)(void)host->ops->stop(host->context);
    return rc;
}
static int fini(void)
{
    if(!host)return 0;
    int rc=api->usb_detach_host(api->owner,host);
    if(rc==-EK_ENODEV)rc=host->ops->stop(host->context);
    if(!rc)host=NULL;
    return rc;
}
const struct ek_driver_manifest ek_driver_manifest={EK_DRIVER_MAGIC,EK_DRIVER_ABI,sizeof(struct ek_driver_manifest),"pi4-usb",init,fini};
