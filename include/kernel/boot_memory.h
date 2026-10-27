#ifndef KERNEL_BOOT_MEMORY_H
#define KERNEL_BOOT_MEMORY_H
#include <kernel/page.h>

#define BOOT_RESERVED_MAX 64U
struct boot_memory {
    struct memory_range reserved[BOOT_RESERVED_MAX];
    unsigned int count;
    uint32_t dtb_size;
};
/* Bounded DTB v17 reader. The caller reserves the blob itself separately.
 * Includes memreserve, fixed /reserved-memory reg, and /chosen initrd.
 * No partial output may be used after an error. */
int boot_memory_read(const void *dtb, size_t accessible, struct boot_memory *out);
/* QEMU's raw-image boot without -dtb uses little-endian ARM ATAGs. */
int boot_memory_read_atags(const void *tags, size_t accessible, struct boot_memory *out);
#endif
