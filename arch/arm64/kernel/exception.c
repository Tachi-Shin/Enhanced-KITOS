#include <arch/arm64/kernel/exception.h>
#include <arch/irq.h>
#include <arch/arm64/armv8util.h>
#include <kernel/printk.h>
#include <kernel/task.h>
#include <kernel/syscall.h>
#include <kernel/error.h>
#include <uapi/error.h>

void arch_exception_init(void)
{
    WriteSysReg(VBAR_EL1, (uintptr_t)vector_table);
    __asm__ volatile("isb" ::: "memory");
}
void exception_fatal(struct exception_frame *frame)
{
    (void)arch_irq_save();
    printk("[panic] EL1/unsupported exception ESR=%llx FAR=%llx PC=%llx PSTATE=%llx\n",
           frame->esr, frame->far, frame->pc, frame->pstate);
    for (;;) __asm__ volatile("wfe" ::: "memory");
}
void sync_dispatch(struct exception_frame *frame)
{
    if ((frame->pstate & 15) != 0 || !task_current_space()) exception_fatal(frame);
    unsigned int ec = (unsigned int)(frame->esr >> 26) & 63;
    if (ec == 0x15) { /* SVC from AArch64 EL0 */
        if ((frame->esr & 0xFFFF) != 0) { frame->x[0] = (uint64_t)-KITOS_ENOSYS; return; }
        /* Full user state is on this task's kernel stack. Syscalls may be
         * preempted or sleep; no GIC acknowledgement is involved here. */
        arch_irq_restore(0);
        frame->x[0] = (uint64_t)syscall_dispatch(frame->x[8], frame->x);
        (void)arch_irq_save();
        return;
    }
    /* Keep diagnostics in task metadata; do not print from exception context. */
    task_finish(-K_EFAULT, frame->esr, (ec == 0x20 || ec == 0x24) ? frame->far : 0, frame->pc);
}
