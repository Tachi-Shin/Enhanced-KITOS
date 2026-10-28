#include <arch/memory.h>
void arch_dma_sync(void *address, size_t length)
{
    uint64_t ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    uintptr_t line = 4UL << ((ctr >> 16) & 15), start = (uintptr_t)address;
    for (uintptr_t p = start & ~(line - 1); p < start + length; p += line)
        __asm__ volatile("dc civac, %0" :: "r"(p) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}
