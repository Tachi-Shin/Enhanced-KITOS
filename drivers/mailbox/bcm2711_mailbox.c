#include <drivers/mailbox/mailbox.h>
#include <kernel/clock.h>
#include <arch/io.h>
#include <arch/arm64/mmu.h>
#include <kernel/error.h>
#include <arch/memory.h>
#include <kernel/sync.h>
#include <lib/string.h>
/* Serialized, cache-maintained. After timeout firmware may still own this
 * buffer; keep it reserved for the lifetime of the kernel and never reuse it. */
#ifdef EKITOS_MODULE
#include <ekitos/runtime.h>
static volatile uint32_t *message;
static uint64_t message_physical;
int mailbox_driver_init(void)
{
    message = ek_api->dma_alloc(ek_api->owner, &message_physical);
    return message ? 0 : -K_ENOMEM;
}
#else
static volatile uint32_t message[1024] __attribute__((aligned(4096)));
#endif
static void message_sync(bool from_device)
{
#ifdef EKITOS_MODULE
    ek_api->dma_sync((void *)message,4096,from_device?EK_DMA_FROM_DEVICE:EK_DMA_TO_DEVICE);
#else
    (void)from_device;arch_dma_sync((void *)message,4096);
#endif
}
static bool failed;
static struct kernel_gate property_gate;

static int transfer(void)
{
#ifdef EKITOS_MODULE
    uint64_t physical = message_physical;
#else
    uint64_t physical = arch_virt_to_phys((const void *)message);
#endif
    if (failed || physical >= 0x40000000UL) return -K_EIO;
    message_sync(false);
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
        message_sync(true);
        return message[1] == 0x80000000U ? 0 : -K_EIO;
    }
}
int mailbox_property_list(void *tags,uint32_t bytes)
{
    if(!tags||((uintptr_t)tags&3)||bytes<12||bytes>4084||(bytes&3))return -K_EINVAL;
    uint32_t *p=tags;
    for(uint32_t off=0;off<bytes;){
        if(bytes-off<12)return -K_EINVAL;
        uint32_t *t=p+off/4,n=t[1];
        if(!t[0]||n>bytes-off-12||(n&3)||t[2]>n)return -K_EINVAL;
        for(uint32_t prev=0;prev<off;prev+=12+p[prev/4+1])if(p[prev/4]==t[0])return -K_EINVAL;
        off+=12+n;
    }
    if(!kernel_gate_try(&property_gate))return -K_EBUSY;
    message[0]=bytes+12;message[1]=0;
    memcpy((void *)(message+2),tags,bytes);message[2+bytes/4]=0;
    int rc=transfer();if(!rc)memcpy(tags,(const void *)(message+2),bytes);
    kernel_gate_release(&property_gate);return rc;
}
static int property(uint32_t tag,uint32_t first,uint32_t second,uint32_t bytes,uint32_t *a,uint32_t *b)
{
    uint32_t t[6]={tag,bytes,tag==0x30058U?4U:0U,first,second,0};
    int rc=mailbox_property_list(t,12+bytes);
    if(rc)return rc;
    if(!(t[2]&0x80000000U)||(t[2]&0x7fffffffU)<(bytes==4?4U:8U)||(t[2]&0x7fffffffU)>bytes)return -K_EIO;
    *a=t[3];*b=bytes==4?0:t[4];return 0;
}

uint32_t mailbox_clock_rate(uint32_t id, uint32_t hz)
{
    uint32_t returned_id, rate;
    return property(hz ? 0x00038002U : 0x00030002U, id, hz, 12, &returned_id, &rate) ||
           returned_id != id ? 0 : rate;
}

int mailbox_arm_memory(uint32_t *base, uint32_t *size)
{
    if (!base || !size) return -K_EINVAL;
    return property(0x00010005U, 0, 0, 12, base, size);
}
int mailbox_notify_xhci_reset(uint32_t bdf)
{
    uint32_t a,b;
    return property(0x00030058U,bdf,0,4,&a,&b);
}
