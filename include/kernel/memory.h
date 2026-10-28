#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H
#include <kernel/page.h>
void memory_boot_print(void);
struct boot_memory;
int memory_init(const struct boot_memory *,uint64_t dtb_physical);
void memory_print_stats(void);
#endif
