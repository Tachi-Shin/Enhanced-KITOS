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
/* Dedicated, cache-line-aligned DMA buffer; clean/invalidate before ownership
 * passes to a device and after completion, before the CPU accesses it again. */
void arch_dma_sync(void *, size_t);
int arch_module_map(uint64_t virtual, uint64_t physical, unsigned int permissions);
int arch_module_unmap(uint64_t virtual);
void *arch_framebuffer_map(uint64_t physical,size_t bytes);
void arch_framebuffer_unmap(size_t bytes);
#endif
