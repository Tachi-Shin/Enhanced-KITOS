#include <ekitos/runtime.h>
#include <drivers/mailbox/mailbox.h>
extern int mailbox_driver_init(void);
static const struct ek_firmware_ops ops = {mailbox_clock_rate, mailbox_arm_memory, mailbox_notify_xhci_reset,mailbox_property_list};
static int init(const struct ek_driver_api *api)
{
    if (!api || api->abi != EK_DRIVER_ABI || api->size < sizeof(*api)) return -EK_EINVAL;
    ek_api = api; int rc = mailbox_driver_init();
    return rc ? rc : api->device_publish(api->owner, EK_DEVICE_FIRMWARE, &ops);
}
EK_DRIVER_EXPORT const struct ek_driver_manifest ek_driver_manifest = {
    EK_DRIVER_MAGIC, EK_DRIVER_ABI, sizeof(struct ek_driver_manifest), "bcm2711-mailbox", init, NULL};
