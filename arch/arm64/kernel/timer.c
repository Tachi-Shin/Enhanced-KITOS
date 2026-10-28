#include <arch/timer.h>
#include <arch/arm64/armv8util.h>
#include <kernel/device.h>
uint64_t arch_counter_read(void)
{
    __asm__ volatile("isb" ::: "memory");
    return ReadSysReg(CNTPCT_EL0);
}
uint64_t arch_counter_frequency(void) { return ReadSysReg(CNTFRQ_EL0); }
void arch_timer_program(uint64_t deadline)
{
    const struct ek_timer_ops *ops = device_backend(EK_DEVICE_TIMER);
    if (ops) ops->program(deadline);
}
