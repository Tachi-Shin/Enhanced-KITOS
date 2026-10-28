#include <ekitos/runtime.h>
#include <platform/irq.h>
static const struct ek_irq_ops ops = {
    irq_controller_init, irq_controller_enable, irq_controller_disable, irq_controller_ack, irq_controller_eoi};
static int init(const struct ek_driver_api *api)
{
    if (!api || api->abi != EK_DRIVER_ABI || api->size < sizeof(*api)) return -EK_EINVAL;
    ek_api = api; return api->device_publish(api->owner, EK_DEVICE_IRQ, &ops);
}
EK_DRIVER_EXPORT const struct ek_driver_manifest ek_driver_manifest = {
    EK_DRIVER_MAGIC, EK_DRIVER_ABI, sizeof(struct ek_driver_manifest), "gicv2", init, NULL};
