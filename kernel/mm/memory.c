#include <kernel/memory.h>
#include <arch/memory.h>
#include <platform/platform.h>
#include <kernel/boot_memory.h>
#include <kernel/error.h>
#include <kernel/printk.h>

int memory_init(struct memory_range ram, uint64_t dtb_physical)
{
    extern uint8_t __kernel_start[], __kernel_end[];
    struct boot_memory boot = {0};
    struct memory_range reserved[BOOT_RESERVED_MAX + 2];
    uint64_t kernel_start = arch_virt_to_phys(__kernel_start);
    uint64_t kernel_end = arch_virt_to_phys(__kernel_end);
    if (ram.base > kernel_start || kernel_end <= kernel_start ||
        kernel_end - ram.base > ram.size) return -K_EINVAL;
    int rc = platform_boot_reservations(ram, dtb_physical, &boot);
    if (rc) return rc;
    unsigned int count = 0;
    /* Platform-owned reservations come from the boot protocol adapter. */
    reserved[count++] = (struct memory_range){kernel_start, kernel_end - kernel_start};
    if (boot.dtb_size) reserved[count++] = (struct memory_range){dtb_physical, boot.dtb_size};
    for (unsigned int i = 0; i < boot.count; ++i) reserved[count++] = boot.reserved[i];
    rc = page_init(ram, reserved, count);
    if (rc) return rc;
    unsigned int io_count;
    const struct memory_range *io = platform_mmio_ranges(&io_count);
    rc = arch_kernel_memory_init(ram, boot.reserved, boot.count, io, io_count);
    if (!rc) {
        printk("[memory] ARM RAM=%llu MiB reservations=%u DTB=%p\n",
               ram.size / (1024U * 1024U), count, (void *)(uintptr_t)dtb_physical);
        printk("[memory] MMU enabled; TTBR1 kernel; TTBR0 empty; W^X; caches on\n");
        memory_print_stats();
    }
    return rc;
}

void memory_print_stats(void)
{
    struct page_stats stats;
    page_get_stats(&stats);
    printk("[memory] pages total=%llu free=%llu used=%llu page_size=%u\n",
           stats.total, stats.free, stats.total - stats.free, (unsigned int)PAGE_SIZE);
}
