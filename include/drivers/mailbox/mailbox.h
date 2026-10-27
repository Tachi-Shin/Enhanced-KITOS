#ifndef MAILBOX_H
#define MAILBOX_H
#include <dos/type.h>
/* Boot-only, caches off, serialized. clock_rate returns 0 on failure. */
uint32_t mailbox_clock_rate(uint32_t id, uint32_t set_hz);
int mailbox_arm_memory(uint32_t *base, uint32_t *size);
#endif
