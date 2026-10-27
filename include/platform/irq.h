#ifndef PLATFORM_IRQ_H
#define PLATFORM_IRQ_H
#include <dos/type.h>
void irq_controller_init(void);
void irq_controller_enable(uint32_t id, uint32_t priority);
void irq_controller_disable(uint32_t id);
uint32_t irq_controller_ack(uint32_t *id);
void irq_controller_eoi(uint32_t token);
unsigned int platform_timer_irq(void);
#endif
