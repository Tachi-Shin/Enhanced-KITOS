#include <kernel/sync.h>
#include <kernel/task.h>
#include <arch/irq.h>
bool kernel_gate_try(struct kernel_gate *gate)
{
    uint64_t flags = arch_irq_save();
    bool acquired = !__atomic_exchange_n(&gate->locked, 1U, __ATOMIC_ACQUIRE);
    if (acquired) task_resource_get();
    arch_irq_restore(flags);
    return acquired;
}
void kernel_gate_release(struct kernel_gate *gate)
{
    uint64_t flags = arch_irq_save();
    __atomic_store_n(&gate->locked, 0U, __ATOMIC_RELEASE);
    task_resource_put();
    arch_irq_restore(flags);
}
