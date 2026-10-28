#include <ekitos/driver.h>
#include <kernel/error.h>
static const struct ek_driver_api *api;
#define usb_control(...) api->usb_control(__VA_ARGS__)
#define usb_interrupt_in(...) api->usb_interrupt_in(__VA_ARGS__)
#define usb_input_emit(...) api->input_emit(__VA_ARGS__)
#include "../usb/hid.c"
static int init(const struct ek_driver_api *provided)
{
    if(!provided || provided->abi!=EK_DRIVER_ABI || provided->size<sizeof(*provided))return -K_EINVAL;
    api=provided;
    return api->usb_register_class(api->owner,&usb_boot_hid_driver);
}
static int fini(void) {return api->usb_unregister_class(api->owner,&usb_boot_hid_driver);}
EK_DRIVER_EXPORT const struct ek_driver_manifest ek_driver_manifest={
    EK_DRIVER_MAGIC,EK_DRIVER_ABI,sizeof(struct ek_driver_manifest),"boot-hid",init,fini};
