#ifndef KERNEL_SCHED_H
#define KERNEL_SCHED_H
#include <dos/type.h>
bool sched_quantum_expired(void);
void sched_irq_exit(bool requested);
#endif
