#include <kernel/device.h>
#include <kernel/error.h>
#include <drivers/gpio/gpio.h>
#include <drivers/uart/uart.h>
#include <arch/irq.h>
static const void *backends[EK_DEVICE_COUNT];
const void *device_backend(enum ek_device_kind kind)
{ return kind < EK_DEVICE_COUNT ? backends[kind] : NULL; }
int device_publish(enum ek_device_kind kind, const void *backend)
{
    if (kind >= EK_DEVICE_COUNT || !backend) return -K_EINVAL;
    if (backends[kind]) return -K_EBUSY;
    int rc = 0;
    if (kind == EK_DEVICE_GPIO) rc = gpio_init(backend);
    if (kind == EK_DEVICE_UART) {
        struct uart_device *uart = (void *)backend;
        rc = uart_init(uart); if (!rc) rc = uart_register(uart);
    }
    if (!rc) backends[kind] = backend;
    return rc;
}
/* Only used to roll back an unsuccessful publication before boot activates
 * callbacks. Loaded boot-service modules cannot be unloaded. */
void device_unpublish(enum ek_device_kind kind, const void *backend)
{ if (kind < EK_DEVICE_COUNT && backends[kind] == backend) backends[kind] = NULL; }
