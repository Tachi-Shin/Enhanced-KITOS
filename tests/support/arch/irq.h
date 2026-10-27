#ifndef HOST_IRQ_H
#define HOST_IRQ_H
#include <dos/type.h>
extern uint64_t test_irq_mask;
static inline uint64_t arch_irq_save(void) { uint64_t old = test_irq_mask; test_irq_mask = 128; return old; }
static inline void arch_irq_restore(uint64_t value) { test_irq_mask = value; }
#endif
