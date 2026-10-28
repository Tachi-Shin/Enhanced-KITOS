#ifndef KERNEL_IRQ_H
#define KERNEL_IRQ_H
#include <dos/type.h>
/* Handler returns a scheduling request; EOI always precedes switching. */
typedef bool (*irq_handler)(void *context);
int irq_register(unsigned int id, irq_handler handler, void *context);
int irq_unregister(unsigned int id, irq_handler handler, void *context);
void irq_dispatch(void);
#endif
