#include <drivers/uart/uart.h>
#include <drivers/char/char.h>
static int input(void *context, char *out) { return uart_try_getc(context, out); }
static void output(void *context, char value) { uart_putc(context, value); }
struct char_device *uart_char_device(struct uart_device *uart)
{
    /* One boot console, lifetime is the entire kernel. */
    static struct char_device device;
    device = (struct char_device){uart->name, uart, input, output};
    return &device;
}
