#ifndef PLATFORM_PLATFORM_H
#define PLATFORM_PLATFORM_H
#include <dos/type.h>
struct uart_device;
struct block_device;
struct memory_range;
struct boot_memory;
int platform_init(void);
int platform_memory(struct memory_range *ram);
int platform_boot_reservations(struct memory_range ram, uint64_t boot_address,
                               struct boot_memory *out);
const struct memory_range *platform_mmio_ranges(unsigned int *count);
struct uart_device *platform_uart(void);
struct block_device *platform_storage(void);
const char *platform_name(void);
#endif
