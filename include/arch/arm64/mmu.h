#ifndef ARCH_ARM64_MMU_H
#define ARCH_ARM64_MMU_H
#define KERNEL_VA_BASE 0xffff000000000000
#ifndef __ASSEMBLER__
#include <dos/type.h>
/* Only for the kernel's linear physical mapping, not arbitrary virtual memory. */
static inline void *arch_phys_to_virt(uint64_t physical)
{ return (void *)(uintptr_t)(KERNEL_VA_BASE + physical); }
static inline uint64_t arch_virt_to_phys(const void *virtual)
{ return (uintptr_t)virtual - KERNEL_VA_BASE; }
void arch_tlb_flush(void);
void arch_user_table_switch(uint64_t root);
void arch_sync_code(void *address, size_t length);
#endif
#endif
