#ifndef IPC_TEST_MEMORY_H
#define IPC_TEST_MEMORY_H
#include <stdint.h>
static inline void *arch_phys_to_virt(uint64_t p){return (void *)(uintptr_t)p;}
#endif
