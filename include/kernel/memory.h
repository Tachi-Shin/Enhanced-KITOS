#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H
#include <kernel/page.h>
int memory_init(struct memory_range ram, uint64_t dtb_physical);
void memory_print_stats(void);
#endif
