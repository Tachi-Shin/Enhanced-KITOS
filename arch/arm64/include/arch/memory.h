#ifndef ARCH_MEMORY_H
#define ARCH_MEMORY_H
#include <arch/arm64/mmu.h>
#include <kernel/page.h>
#define ARCH_USER_END (1ULL << 48)
int arch_kernel_memory_init(struct memory_range ram,
    const struct memory_range *reserved, unsigned int count,
    const struct memory_range *mmio, unsigned int mmio_count);
int arch_kernel_guard(void *page);
int arch_memory_check(void);
#endif
