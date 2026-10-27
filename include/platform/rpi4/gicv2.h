// include/platform/rpi4/gicv2.h
#ifndef __GICV2_H__
#define __GICV2_H__

#include <platform/rpi4/gicv2def.h>
#include <dos/type.h>
#include <arch/io.h>

/* GICD */
static inline void WriteGICD8(uint32_t offset, uint8_t data) {
    mmio_write8(GICD_BASE + offset, data);
}

static inline uint32_t ReadGICD32(uint32_t offset) {
    return mmio_read32(GICD_BASE + offset);
}

static inline void WriteGICD32(uint32_t offset, uint32_t data) {
    mmio_write32(GICD_BASE + offset, data);
}

static inline uint64_t ReadGICD64(uint32_t offset) {
    return mmio_read64(GICD_BASE + offset);
}

static inline void WriteGICD64(uint32_t offset, uint64_t data) {
    mmio_write64(GICD_BASE + offset, data);
}

/* GICC */
static inline void WriteGICC8(uint32_t offset, uint8_t data) {
    mmio_write8(GICC_BASE + offset, data);
}

static inline uint32_t ReadGICC32(uint32_t offset) {
    return mmio_read32(GICC_BASE + offset);
}

static inline void WriteGICC32(uint32_t offset, uint32_t data) {
    mmio_write32(GICC_BASE + offset, data);
}

static inline uint64_t ReadGICC64(uint32_t offset) {
    return mmio_read64(GICC_BASE + offset);
}

static inline void WriteGICC64(uint32_t offset, uint64_t data) {
    mmio_write64(GICC_BASE + offset, data);
}

static inline uint32_t GetIRQId(uint32_t intID) {
    return intID & GICC_IA_IRQ;
}

#endif /* __GICV2_H__ */