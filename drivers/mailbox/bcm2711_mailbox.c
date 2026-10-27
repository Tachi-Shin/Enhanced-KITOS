#include <drivers/mailbox/mailbox.h>
#include <kernel/clock.h>
#include <arch/io.h>
#include <arch/arm64/mmu.h>
#include <kernel/error.h>
/* Boot-only, serialized, caches off. After timeout firmware may still own this
 * buffer; keep it reserved for the lifetime of the kernel and never reuse it. */
static volatile uint32_t message[16] __attribute__((aligned(64)));
static bool failed;

static int property(uint32_t tag, uint32_t first, uint32_t second,
                    uint32_t *out_first, uint32_t *out_second)
{
    uint64_t physical = arch_virt_to_phys((const void *)message);
    if (failed || physical >= 0x40000000UL) return -K_EIO;
    message[0] = 36; message[1] = 0;
    message[2] = tag;
    message[3] = 12; message[4] = 0;
    message[5] = first; message[6] = second; message[7] = 0; message[8] = 0;
    uint32_t request = (uint32_t)physical | 0xC0000008U;
    uint64_t start = clock_ms();
    while (mmio_read32(0xFE00B8B8UL) & 0x80000000U)
        if (clock_ms() - start >= 1000U) { failed = true; return -K_ETIMEDOUT; }
    mmio_dsb();
    mmio_write32(0xFE00B8A0UL, request);
    for (;;) {
        if (clock_ms() - start >= 1000U) { failed = true; return -K_ETIMEDOUT; }
        if (mmio_read32(0xFE00B898UL) & 0x40000000U) continue;
        if (mmio_read32(0xFE00B880UL) != request) continue;
        mmio_dsb();
        if (message[1] != 0x80000000U || !(message[4] & 0x80000000U) ||
            (message[4] & 0x7FFFFFFFU) < 8 || (message[4] & 0x7FFFFFFFU) > 12) return -K_EIO;
        *out_first = message[5]; *out_second = message[6];
        return 0;
    }
}

uint32_t mailbox_clock_rate(uint32_t id, uint32_t hz)
{
    uint32_t returned_id, rate;
    return property(hz ? 0x00038002U : 0x00030002U, id, hz, &returned_id, &rate) ||
           returned_id != id ? 0 : rate;
}

int mailbox_arm_memory(uint32_t *base, uint32_t *size)
{
    if (!base || !size) return -K_EINVAL;
    return property(0x00010005U, 0, 0, base, size);
}
