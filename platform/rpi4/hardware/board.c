#include <platform/platform.h>
#include <drivers/uart/uart.h>
#include <block/block.h>
int platform_init(void) { return 0; }
struct uart_device *platform_uart(void) { return uart_get_device(0); }
struct block_device *platform_storage(void) { return block_get_device(0); }
struct usb_host *platform_usb_host(void) { return NULL; }
const char *platform_name(void) { return "rpi4/hardware"; }
