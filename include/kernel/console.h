#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H
#include <drivers/char/char.h>
void console_bind(struct char_device *device);
int console_try_getc(char *out);
void console_putc(char value);
void console_write(const char *text);
#endif
