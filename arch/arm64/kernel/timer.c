#include <arch/timer.h>
#include <arch/arm64/armv8util.h>
uint64_t arch_counter_read(void)
{
    __asm__ volatile("isb" ::: "memory");
    return ReadSysReg(CNTPCT_EL0);
}
uint64_t arch_counter_frequency(void) { return ReadSysReg(CNTFRQ_EL0); }
void arch_timer_program(uint64_t deadline)
{
    WriteSysReg(CNTP_CVAL_EL0, deadline);
    WriteSysReg(CNTP_CTL_EL0, 1U);
    __asm__ volatile("isb" ::: "memory");
}
