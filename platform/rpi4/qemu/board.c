#include <platform/platform.h>
#include <block/block.h>
#include <drivers/mmc/sd.h>
#include <drivers/uart/mini_uart/mini_uart.h>
#include <drivers/gpio/bcm2711/bcm2711_gpio.h>
static struct sd_card card = { .host = { .base_address = 0xFE300000UL } };
static struct block_device storage;
int platform_init(void)
{
    if (gpio_init(&bcm2711_gpio_device)) return -1;
    return gpio_set_function(14, GPIO_ALT5) || gpio_set_function(15, GPIO_ALT5) ||
           gpio_set_pull(14, GPIO_PULL_NONE) || gpio_set_pull(15, GPIO_PULL_NONE) ? -1 : 0;
}
struct uart_device *platform_uart(void) { return mini_uart_get_device(); }
struct block_device *platform_storage(void)
{
    sd_block_setup(&storage, &card, "QEMU SDHCI (FE300000)");
    return &storage;
}
const char *platform_name(void) { return "rpi4/qemu"; }
