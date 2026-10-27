#include <drivers/mmc/sdhci.h>
#include <arch/io.h>
/* QEMU's generic SDHCI implements native byte and halfword accesses. */
uint8_t sdhci_read8(struct sdhci_host *h, uint32_t r) { return mmio_read8(h->base_address + r); }
uint16_t sdhci_read16(struct sdhci_host *h, uint32_t r) { return mmio_read16(h->base_address + r); }
uint32_t sdhci_read32(struct sdhci_host *h, uint32_t r) { return mmio_read32(h->base_address + r); }
void sdhci_write8(struct sdhci_host *h, uint32_t r, uint8_t v) { mmio_write8(h->base_address + r, v); mmio_dsb(); }
void sdhci_write16(struct sdhci_host *h, uint32_t r, uint16_t v) { mmio_write16(h->base_address + r, v); mmio_dsb(); }
void sdhci_write32(struct sdhci_host *h, uint32_t r, uint32_t v) { mmio_write32(h->base_address + r, v); mmio_dsb(); }
