#include <kernel/sched.h>
#include <kernel/task.h>
bool sched_quantum_expired(void) { return true; }
void sched_irq_exit(bool requested) { if (requested) task_yield(); }
