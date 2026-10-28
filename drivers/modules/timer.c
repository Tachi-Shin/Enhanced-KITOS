#include <ekitos/runtime.h>
#include <arch/timer.h>
static const struct ek_timer_ops ops = {arch_timer_program};
static int init(const struct ek_driver_api *api)
{
    if (!api || api->abi != EK_DRIVER_ABI || api->size < sizeof(*api)) return -EK_EINVAL;
    ek_api = api; return api->device_publish(api->owner, EK_DEVICE_TIMER, &ops);
}
EK_DRIVER_EXPORT const struct ek_driver_manifest ek_driver_manifest = {
    EK_DRIVER_MAGIC, EK_DRIVER_ABI, sizeof(struct ek_driver_manifest), "arm-timer", init, NULL};
