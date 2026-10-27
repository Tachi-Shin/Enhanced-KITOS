#include <kernel/irq.h>
#include <kernel/error.h>
#include <kernel/sched.h>
#include <platform/irq.h>
#include <arch/irq.h>
#include <arch/io.h>
#define IRQ_COUNT 256U
static struct { irq_handler handler; void *context; } handlers[IRQ_COUNT];
int irq_register(unsigned int id, irq_handler handler, void *context)
{
    if (id >= IRQ_COUNT || !handler) return -K_EINVAL;
    uint64_t flags = arch_irq_save();
    if (handlers[id].handler) { arch_irq_restore(flags); return -K_EBUSY; }
    handlers[id].context = context;
    handlers[id].handler = handler;
    arch_irq_restore(flags);
    return 0;
}
void irq_dispatch(void)
{
    uint32_t id;
    uint32_t token = irq_controller_ack(&id);
    if (id >= 1020U) return;
    bool reschedule = false;
    if (id < IRQ_COUNT && handlers[id].handler)
        reschedule = handlers[id].handler(handlers[id].context);
    else irq_controller_disable(id);
    mmio_dsb();
    irq_controller_eoi(token);
    mmio_dsb();
    sched_irq_exit(reschedule);
}
