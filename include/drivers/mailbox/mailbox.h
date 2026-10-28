#ifndef MAILBOX_H
#define MAILBOX_H
#include <dos/type.h>
/* Serialized property calls, with runtime DMA cache maintenance and timeouts.
 * clock_rate returns 0 on failure. */
uint32_t mailbox_clock_rate(uint32_t id, uint32_t set_hz);
int mailbox_arm_memory(uint32_t *base, uint32_t *size);
int mailbox_notify_xhci_reset(uint32_t bdf);
/* Tag headers/payloads only, no message header or terminating zero. <=4084 B. */
int mailbox_property_list(void *tags, uint32_t bytes);
#endif
