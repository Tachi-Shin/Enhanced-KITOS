#include <kernel/console.h>
#include <kernel/error.h>
static struct char_device *console;
void console_bind(struct char_device *device) { console = device; }
int console_try_getc(char *out)
{
    if (!console || !console->try_getc || !out) return -K_ENODEV;
    return console->try_getc(console->context, out);
}
void console_putc(char value)
{
    if (!console || !console->putc) return;
    if (value == '\n') console->putc(console->context, '\r');
    console->putc(console->context, value);
}
void console_write(const char *text)
{
    if (text) while (*text) console_putc(*text++);
}
