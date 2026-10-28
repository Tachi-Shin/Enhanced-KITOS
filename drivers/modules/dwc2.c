#include <ekitos/runtime.h>
#include <lib/string.h>
extern void dwc2_release_dma(void);
static struct usb_host *host;
static int init(const struct ek_driver_api *api)
{
    if (!api || api->abi != EK_DRIVER_ABI || api->size < sizeof(*api)) return -EK_EINVAL;
    if (strcmp(api->platform, "rpi4/qemu")) return -EK_ENODEV;
    ek_api = api; host = dwc2_qemu_host();
    return api->usb_attach_host(api->owner, host);
}
static int fini(void)
{
    int rc = ek_api->usb_detach_host(ek_api->owner, host);
    if (!rc) dwc2_release_dma();
    return rc;
}
EK_DRIVER_EXPORT const struct ek_driver_manifest ek_driver_manifest = {
    EK_DRIVER_MAGIC, EK_DRIVER_ABI, sizeof(struct ek_driver_manifest), "qemu-dwc2", init, fini};
