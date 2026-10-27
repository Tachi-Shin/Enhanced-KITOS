#ifndef ARCH_TIMER_H
#define ARCH_TIMER_H
#include <dos/type.h>
uint64_t arch_counter_read(void);
uint64_t arch_counter_frequency(void);
void arch_timer_program(uint64_t deadline);
#endif
