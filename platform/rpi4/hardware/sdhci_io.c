#include <drivers/mmc/sdhci.h>
#include <arch/io.h>
#include <kernel/clock.h>
/* BCM2711 eMMC2: 32-bit bus only. Do not use RMW for W1C status or CMDTM. */
uint32_t sdhci_read32(struct sdhci_host *h, uint32_t r)
{ mmio_dsb(); uint32_t v = mmio_read32(h->base_address + r); mmio_dsb(); return v; }
uint8_t sdhci_read8(struct sdhci_host *h, uint32_t r)
{ return (uint8_t)(sdhci_read32(h, r & ~3U) >> ((r & 3U) * 8U)); }
uint16_t sdhci_read16(struct sdhci_host *h, uint32_t r)
{ return (uint16_t)(sdhci_read32(h, r & ~3U) >> ((r & 3U) * 8U)); }
void sdhci_write32(struct sdhci_host *h, uint32_t r, uint32_t v)
{
    mmio_write32(h->base_address + r, v);
    mmio_dsb();
    /* Low-speed clock-domain crossing; data FIFO is exempt. */
    if (r != 0x20U && h->current_clock_hz <= 400000U)
        clock_delay_us(h->current_clock_hz ?
            (4000000U + h->current_clock_hz - 1U) / h->current_clock_hz : 10U);
}
void sdhci_write8(struct sdhci_host *h, uint32_t r, uint8_t v)
{
    uint32_t shift = (r & 3U) * 8U, old = sdhci_read32(h, r & ~3U);
    sdhci_write32(h, r & ~3U, (old & ~(0xFFU << shift)) | ((uint32_t)v << shift));
}
void sdhci_write16(struct sdhci_host *h, uint32_t r, uint16_t v)
{
    uint32_t shift = (r & 3U) * 8U, old = sdhci_read32(h, r & ~3U);
    sdhci_write32(h, r & ~3U, (old & ~(0xFFFFU << shift)) | ((uint32_t)v << shift));
}
