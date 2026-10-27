#include <platform/platform.h>
#include <drivers/mailbox/mailbox.h>
#include <kernel/page.h>
#include <kernel/error.h>
#include <kernel/boot_memory.h>
#include <arch/memory.h>

int platform_memory(struct memory_range *ram)
{
    uint32_t base, size;
    if (!ram) return -K_EINVAL;
    int rc = mailbox_arm_memory(&base, &size);
    if (rc) return rc;
    /* Firmware returns the low ARM bank, excluding VideoCore RAM. Do not infer
     * further banks from the board revision or probe potentially absent RAM. */
    if (!size || (uint64_t)base + size > 0xFC000000ULL) return -K_EINVAL;
    *ram = (struct memory_range){base, size};
    return 0;
}

int platform_boot_reservations(struct memory_range ram, uint64_t boot_address,
                               struct boot_memory *out)
{
    *out = (struct boot_memory){0};
    if (boot_address) {
        if (ram.base || (boot_address & 7) || boot_address >= ram.size) return -K_EINVAL;
        int rc = boot_memory_read(arch_phys_to_virt(boot_address),
                                  (size_t)(ram.size - boot_address), out);
        /* QEMU raw AArch64 images without -dtb use the traditional ATAG location. */
        if (rc && boot_address == 0x100)
            rc = boot_memory_read_atags(arch_phys_to_virt(boot_address), 0x80000 - 0x100, out);
        if (rc) return rc;
    }
    /* Retain firmware handoff area, spin tables and parked-core boot code. */
    if (out->count == BOOT_RESERVED_MAX) return -K_ENOSPC;
    out->reserved[out->count++] = (struct memory_range){0, 0x80000};
    return 0;
}

const struct memory_range *platform_mmio_ranges(unsigned int *count)
{
    static const struct memory_range ranges[] = {
        {0xFE00B000, 0x1000}, /* firmware mailbox */
        {0xFE200000, 0x1000}, /* GPIO */
        {0xFE201000, 0x1000}, /* PL011 */
        {0xFE215000, 0x1000}, /* AUX / mini UART */
        {0xFE300000, 0x1000}, /* QEMU SDHCI */
        {0xFE340000, 0x1000}, /* BCM2711 eMMC2 */
        {0xFF841000, 0x1000}, /* GIC distributor */
        {0xFF842000, 0x2000}, /* GIC CPU interface incl DIR at +0x1000 */
    };
    *count = sizeof(ranges) / sizeof(ranges[0]);
    return ranges;
}
