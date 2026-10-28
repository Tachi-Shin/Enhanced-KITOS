#include <arch/timer.h>
#include <arch/arm64/armv8util.h>
void arch_timer_program(uint64_t deadline)
{
    WriteSysReg(CNTP_CVAL_EL0, deadline);
    WriteSysReg(CNTP_CTL_EL0, 1U);
    __asm__ volatile("isb" ::: "memory");
}
