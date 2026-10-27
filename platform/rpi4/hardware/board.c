#include <platform/platform.h>
#include <block/block.h>
#include <drivers/mmc/sd.h>
#include <drivers/uart/pl011/pl011.h>
#include <drivers/gpio/bcm2711/bcm2711_gpio.h>
#include <drivers/mailbox/mailbox.h>
static struct sd_card card = { .host = { .base_address = 0xFE340000UL } };
static struct block_device storage;
int platform_init(void)
{
    if (gpio_init(&bcm2711_gpio_device)) return -1;
    if (gpio_set_function(14, GPIO_ALT0) || gpio_set_function(15, GPIO_ALT0) ||
        gpio_set_pull(14, GPIO_PULL_NONE) || gpio_set_pull(15, GPIO_PULL_NONE)) return -1;
    /* Firmware has already routed the boot SD slot to eMMC2. */
    card.host.input_clock_hz = mailbox_clock_rate(12U, 0U);
    return mailbox_clock_rate(2U, 48000000U) == 48000000U ? 0 : -1;
}
struct uart_device *platform_uart(void) { return pl011_get_device(); }
struct block_device *platform_storage(void)
{
    if (!card.host.input_clock_hz) return NULL;
    sd_block_setup(&storage, &card, "BCM2711 eMMC2 (FE340000)");
    return &storage;
}
const char *platform_name(void) { return "rpi4/hardware"; }
