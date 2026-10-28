#include <ekitos/runtime.h>
#include <drivers/uart/uart.h>
#include <drivers/uart/pl011/pl011.h>
#include <drivers/uart/mini_uart/mini_uart.h>
#include <drivers/gpio/gpio.h>
#include <lib/string.h>
static int init(const struct ek_driver_api *api)
{
    if (!api || api->abi != EK_DRIVER_ABI || api->size < sizeof(*api)) return -EK_EINVAL;
    ek_api = api; bool qemu = !strcmp(api->platform, "rpi4/qemu");
    if (!qemu && strcmp(api->platform, "rpi4/hardware")) return -EK_ENODEV;
    unsigned int function = qemu ? GPIO_ALT5 : GPIO_ALT0;
    if (api->gpio_function(14, function) || api->gpio_function(15, function) ||
        api->gpio_pull(14, GPIO_PULL_NONE) || api->gpio_pull(15, GPIO_PULL_NONE)) return -EK_ENODEV;
    if (!qemu && api->firmware_clock(2, 48000000) != 48000000) return -EK_EIO;
    return api->device_publish(api->owner, EK_DEVICE_UART, qemu ? mini_uart_get_device() : pl011_get_device());
}
EK_DRIVER_EXPORT const struct ek_driver_manifest ek_driver_manifest = {
    EK_DRIVER_MAGIC, EK_DRIVER_ABI, sizeof(struct ek_driver_manifest), "bcm2711-uart", init, NULL};
