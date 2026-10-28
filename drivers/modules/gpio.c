#include <ekitos/runtime.h>
#include <drivers/gpio/bcm2711/bcm2711_gpio.h>
static int init(const struct ek_driver_api *api)
{
    if (!api || api->abi != EK_DRIVER_ABI || api->size < sizeof(*api)) return -EK_EINVAL;
    ek_api = api;
    return api->device_publish(api->owner, EK_DEVICE_GPIO, &bcm2711_gpio_device);
}
EK_DRIVER_EXPORT const struct ek_driver_manifest ek_driver_manifest = {
    EK_DRIVER_MAGIC, EK_DRIVER_ABI, sizeof(struct ek_driver_manifest), "bcm2711-gpio", init, NULL};
