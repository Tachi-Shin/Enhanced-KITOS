#ifndef KERNEL_CLOCK_H
#define KERNEL_CLOCK_H
#include <dos/type.h>
/* Monotonic physical counter, independent of delivered interrupts. */
uint64_t clock_now(void);
uint64_t clock_frequency(void);
uint64_t clock_ms(void);
uint64_t clock_after_ms(uint32_t milliseconds);
void clock_delay_us(uint32_t microseconds);
#endif
