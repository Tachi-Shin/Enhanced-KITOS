#include <drivers/usb/usb.h>
#include <arch/io.h>
#include <arch/memory.h>
#include <arch/cpu.h>
#include <kernel/clock.h>
#include <kernel/error.h>
#include <kernel/task.h>
#include <lib/string.h>
/* Register protocol for the QEMU BCM283x DWC2 model. This is not the PCIe
 * VL805 connected to the four USB-A ports on a physical Raspberry Pi 4B. */
#define BASE 0xFE980000UL
#define GAHBCFG 0x008
#define GUSBCFG 0x00C
#define GRSTCTL 0x010
#define GSNPSID 0x040
#define HCFG 0x400
#define HFIR 0x404
#define HFNUM 0x408
#define HPRT 0x440
#define HCCHAR 0x500
#define HCSPLT 0x504
#define HCINT 0x508
#define HCINTMSK 0x50C
#define HCTSIZ 0x510
#define HCDMA 0x514
#define CHENA (1U << 31)
#define CHDIS (1U << 30)
#ifdef EKITOS_MODULE
#include <ekitos/runtime.h>
static uint8_t *dma_page;
static uint64_t dma_physical;
void dwc2_release_dma(void)
{ if (dma_page && !ek_api->dma_free(ek_api->owner, dma_page)) dma_page = NULL; }
#else
static uint8_t dma_page[4096] __attribute__((aligned(4096)));
#endif
static uint64_t dma_address(void)
{
#ifdef EKITOS_MODULE
    return dma_physical;
#else
    return arch_virt_to_phys(dma_page);
#endif
}
static void sync_dma(bool from_device)
{
#ifdef EKITOS_MODULE
    ek_api->dma_sync(dma_page,4096,from_device?EK_DMA_FROM_DEVICE:EK_DMA_TO_DEVICE);
#else
    (void)from_device;arch_dma_sync(dma_page,4096);
#endif
}
static bool poisoned;
static uint32_t read_reg(unsigned int reg) { return mmio_read32(BASE + reg); }
static void write_reg(unsigned int reg, uint32_t value) { mmio_write32(BASE + reg, value); mmio_dsb(); }
static bool wait_clear(unsigned int reg, uint32_t mask, uint32_t milliseconds)
{
    uint64_t until = clock_after_ms(milliseconds);
    do { if (!(read_reg(reg) & mask)) return true; arch_cpu_relax(); } while (clock_now() < until);
    return false;
}
static void port_write(uint32_t extra)
{ write_reg(HPRT, (read_reg(HPRT) & ~0x2EU) | extra); }
static int start(void *unused)
{
    (void)unused;
#ifdef EKITOS_MODULE
    if (!dma_page) dma_page = ek_api->dma_alloc(ek_api->owner, &dma_physical);
    if (!dma_page) return -K_ENOMEM;
#endif
    if ((read_reg(GSNPSID) & 0xFFFF0000U) != 0x4F540000U || dma_address() >= 0x40000000ULL) return -K_ENODEV;
    write_reg(0x018, 0); /* poll only; no USB IRQ callback or IRQ-context waits */
    write_reg(GUSBCFG, (read_reg(GUSBCFG) & ~(1U << 30)) | (1U << 29));
    write_reg(GRSTCTL, 1);
    if (!wait_clear(GRSTCTL, 1, 100)) return -K_ETIMEDOUT;
    write_reg(GAHBCFG, 1U << 5); /* DMA, global interrupts masked */
    write_reg(0xE00, 0); /* ungate controller clocks */
    port_write(1U << 12);
    return 0;
}
static int root_status(void *unused, unsigned int port, bool *connected, bool *changed)
{
    (void)unused;
    if (port != 1) return -K_EINVAL;
    uint32_t status = read_reg(HPRT);
    *connected = (status & 1) != 0; *changed = (status & 2) != 0;
    if (*changed) port_write(2);
    return poisoned ? -K_EIO : 0;
}
static int root_reset(void *unused, unsigned int port, enum usb_speed *speed)
{
    (void)unused;
    if (port != 1) return -K_EINVAL;
    port_write((1U << 12) | (1U << 8)); task_sleep_ms(50);
    write_reg(HPRT, (read_reg(HPRT) & ~(0x2EU | (1U << 8))) | (1U << 12));
    task_sleep_ms(20);
    uint32_t status = read_reg(HPRT);
    if ((status & 5) != 5 || status & (1U << 4)) return -K_ENODEV;
    unsigned int encoding = (status >> 17) & 3;
    if (encoding == 3) return -K_ENOTSUP;
    *speed = encoding == 0 ? USB_HIGH : encoding == 1 ? USB_FULL : USB_LOW;
    write_reg(HCFG, *speed == USB_HIGH ? 0 : *speed == USB_LOW ? 2 : 1);
    write_reg(HFIR, *speed == USB_LOW ? 6000 : *speed == USB_FULL ? 48000 : 60000);
    return 0;
}
static int transfer(void *unused, const struct usb_device *dev, const struct usb_endpoint *ep,
                     unsigned int pid, void *data, unsigned int length, unsigned int *done)
{
    (void)unused; *done = 0;
    if (poisoned) return -K_EIO;
    if (!ep->max_packet || ep->max_packet > 1024 || length > ep->max_packet ||
        (length && !data) || pid > 3 || dev->address > 127) return -K_EINVAL;
    if (!(read_reg(HPRT) & 1)) return -K_ENODEV;
    bool in = (ep->address & 0x80) != 0;
    /* The QEMU packet API sees HCTSIZ as the host's requested length. In
     * bulk transfers, padding a short request to MPS would consume data past
     * the BOT data stage (notably SCSI CHECK CONDITION with residue). */
    unsigned int bytes = in && length && ep->type!=2 ? ep->max_packet : length;
    if (!in && length) memcpy(dma_page, data, length);
    sync_dma(false);
    write_reg(HCINT, 0x3FFF); write_reg(HCINTMSK, 0);
    write_reg(HCSPLT, 0);
    write_reg(HCTSIZ, bytes | (1U << 19) | (pid << 29));
    write_reg(HCDMA, (uint32_t)dma_address() | 0xC0000000U);
    uint32_t characteristics = ep->max_packet | ((uint32_t)(ep->address & 15) << 11) |
        (in ? 1U << 15 : 0) | (dev->speed == USB_LOW ? 1U << 17 : 0) |
        ((uint32_t)ep->type << 18) | (1U << 20) | ((uint32_t)dev->address << 22) |
        (((read_reg(HFNUM) + 1) & 1) << 29);
    write_reg(HCCHAR, characteristics | CHENA);
    uint64_t until = clock_after_ms(5);
    uint32_t status;
    do {
        status = read_reg(HCINT);
        if (status & (1U | 2U | 8U | 16U | 0x784U)) break;
        arch_cpu_relax();
    } while (clock_now() < until);
    if (read_reg(HCCHAR) & CHENA) {
        write_reg(HCCHAR, characteristics | CHENA | CHDIS);
        if (!wait_clear(HCCHAR, CHENA, 10)) { poisoned = true; return -K_EIO; }
    }
    sync_dma(true);
    if (status & 8U) return -K_EPIPE;
    if (status & 0x784U) return -K_EIO;
    if (status & 16U) return -K_EAGAIN;
    if (!(status & 1U)) return -K_ETIMEDOUT;
    unsigned int remaining = read_reg(HCTSIZ) & 0x7FFFF;
    if (remaining > bytes || bytes - remaining > length) return -K_EOVERFLOW;
    *done = bytes - remaining;
    if (in && *done) memcpy(data, dma_page, *done);
    return 0;
}
static int stop(void *unused)
{
    (void)unused;
    write_reg(HCCHAR, read_reg(HCCHAR) | CHENA | CHDIS);
    if (!wait_clear(HCCHAR, CHENA, 10)) return -K_EBUSY;
    write_reg(GAHBCFG, 0); poisoned = false; return 0;
}
static const struct usb_host_ops ops = {.start=start, .stop=stop, .root_status=root_status, .root_reset=root_reset, .packet=transfer};
struct usb_host *dwc2_qemu_host(void)
{
    static struct usb_host controller = {.name="qemu-dwc2", .ops=&ops, .root_ports=1};
    return &controller;
}
