#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H
#include <drivers/char/char.h>
#include <uapi/terminal.h>
void console_bind(struct char_device *device);
int console_try_getc(char *out);
bool console_input_ready(void);
void console_putc(char value);
void console_write(const char *text);
void console_poll(void);
void console_info(struct kitos_terminal_info *);
void console_worker(void *);
int console_work(int (*job)(void));
void console_print_display(void);
bool console_graphics_active(void);
void console_graphics_exit(uint32_t);
void console_graphics_poll(void);
int console_graphics(uint32_t,unsigned int,uint32_t,unsigned int,unsigned int,unsigned int);
#endif
