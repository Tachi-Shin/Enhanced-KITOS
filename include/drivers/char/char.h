#ifndef DRIVERS_CHAR_H
#define DRIVERS_CHAR_H
#include <dos/type.h>
struct char_device {
    const char *name;
    void *context;
    /* Nonblocking input: 1 byte, 0 empty, negative error. */
    int (*try_getc)(void *context, char *out);
    void (*putc)(void *context, char value);
};
#endif
