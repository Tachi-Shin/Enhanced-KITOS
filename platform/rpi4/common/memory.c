#include <platform/platform.h>
#include <drivers/mailbox/mailbox.h>
#include <kernel/page.h>
#include <kernel/error.h>
#include <kernel/boot_memory.h>
#include <arch/memory.h>

/* Read before BSS clearing: a raw-image bootloader can place the DTB
 * immediately after the file, overlapping the kernel's NOLOAD region. */
int platform_boot_info(uint64_t address,struct boot_memory *out)
{
    if(!out||!address||(address&7)||address>=0xFC000000ULL)return -K_EINVAL;
    size_t available=(size_t)(0xFC000000ULL-address);
    if(available>2U*1024U*1024U)available=2U*1024U*1024U;
    int rc=boot_memory_read(arch_phys_to_virt(address),available,out);
    if(rc&&address==0x100)rc=boot_memory_read_atags(arch_phys_to_virt(address),0x80000-0x100,out);
    if(rc||out->ram.base||!out->ram.size)return rc?rc:-K_EINVAL;
    if(out->ram.size>0xFC000000ULL)out->ram.size=0xFC000000ULL;
    if(address>=out->ram.size||out->dtb_size>out->ram.size-address)return -K_EINVAL;
    if(out->count==BOOT_RESERVED_MAX)return -K_ENOSPC;
    out->reserved[out->count++]=(struct memory_range){0,0x80000};return 0;
}

const struct memory_range *platform_mmio_ranges(unsigned int *count)
{
    static const struct memory_range ranges[] = {
        {0xFD500000, 0x10000}, /* BCM2711 PCIe root complex/configuration */
        {0x600000000ULL, 0x10000}, /* VL805 BAR0 outbound PCIe window */
        {0xFE00B000, 0x1000}, /* firmware mailbox */
        {0xFE200000, 0x1000}, /* GPIO */
        {0xFE201000, 0x1000}, /* PL011 */
        {0xFE215000, 0x1000}, /* AUX / mini UART */
        {0xFE300000, 0x1000}, /* QEMU SDHCI */
        {0xFE340000, 0x1000}, /* BCM2711 eMMC2 */
        {0xFE980000, 0x1000}, /* USB DWC2 registers (QEMU host backend) */
        {0xFF841000, 0x1000}, /* GIC distributor */
        {0xFF842000, 0x2000}, /* GIC CPU interface incl DIR at +0x1000 */
    };
    *count = sizeof(ranges) / sizeof(ranges[0]);
    return ranges;
}
