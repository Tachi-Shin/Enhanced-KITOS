#include <kernel/clock.h>
#include <arch/timer.h>
#include <arch/cpu.h>
uint64_t clock_now(void) { return arch_counter_read(); }
uint64_t clock_frequency(void) { return arch_counter_frequency(); }
uint64_t clock_ms(void)
{
    uint64_t f = clock_frequency(), n = clock_now();
    return n / f * 1000ULL + n % f * 1000ULL / f;
}
uint64_t clock_after_ms(uint32_t ms)
{
    /* CNTFRQ is 32-bit; uint32_t ms therefore cannot overflow this product. */
    uint64_t cycles = (clock_frequency() * ms + 999ULL) / 1000ULL;
    return clock_now() + (cycles ? cycles : 1ULL);
}
void clock_delay_us(uint32_t us)
{
    uint64_t delay = (clock_frequency() * us + 999999ULL) / 1000000ULL;
    uint64_t start = clock_now();
    while (clock_now() - start < delay) arch_cpu_relax();
}
