#include <ekitos/runtime.h>
#include <lib/string.h>
#define sdhci_read8 qemu_sdhci_read8
#define sdhci_read16 qemu_sdhci_read16
#define sdhci_read32 qemu_sdhci_read32
#define sdhci_write8 qemu_sdhci_write8
#define sdhci_write16 qemu_sdhci_write16
#define sdhci_write32 qemu_sdhci_write32
#include "../../platform/rpi4/qemu/sdhci_io.c"
#undef sdhci_read8
#undef sdhci_read16
#undef sdhci_read32
#undef sdhci_write8
#undef sdhci_write16
#undef sdhci_write32
#define sdhci_read8 hardware_sdhci_read8
#define sdhci_read16 hardware_sdhci_read16
#define sdhci_read32 hardware_sdhci_read32
#define sdhci_write8 hardware_sdhci_write8
#define sdhci_write16 hardware_sdhci_write16
#define sdhci_write32 hardware_sdhci_write32
#include "../../platform/rpi4/hardware/sdhci_io.c"
#undef sdhci_read8
#undef sdhci_read16
#undef sdhci_read32
#undef sdhci_write8
#undef sdhci_write16
#undef sdhci_write32
static bool qemu(void) { return !strcmp(ek_api->platform, "rpi4/qemu"); }
uint8_t sdhci_read8(struct sdhci_host *h, uint32_t r)
{ return qemu() ? qemu_sdhci_read8(h, r) : hardware_sdhci_read8(h, r); }
void sdhci_write8(struct sdhci_host *h, uint32_t r, uint8_t v)
{ if (qemu()) qemu_sdhci_write8(h, r, v); else hardware_sdhci_write8(h, r, v); }
uint16_t sdhci_read16(struct sdhci_host *h, uint32_t r)
{ return qemu() ? qemu_sdhci_read16(h, r) : hardware_sdhci_read16(h, r); }
void sdhci_write16(struct sdhci_host *h, uint32_t r, uint16_t v)
{ if (qemu()) qemu_sdhci_write16(h, r, v); else hardware_sdhci_write16(h, r, v); }
uint32_t sdhci_read32(struct sdhci_host *h, uint32_t r)
{ return qemu() ? qemu_sdhci_read32(h, r) : hardware_sdhci_read32(h, r); }
void sdhci_write32(struct sdhci_host *h, uint32_t r, uint32_t v)
{ if (qemu()) qemu_sdhci_write32(h, r, v); else hardware_sdhci_write32(h, r, v); }
