/* Explicit SDK imports for the existing hardware C sources. No kernel symbol
 * lookup: these shims call the versioned function table. */
#include <ekitos/runtime.h>
#include <drivers/uart/uart.h>
#include <kernel/clock.h>
#include <kernel/task.h>
#include <kernel/sync.h>
#include <arch/io.h>
#include <arch/memory.h>
const struct ek_driver_api *ek_api;
uint8_t mmio_read8(uintptr_t p) { return ek_api->read8(p); }
uint16_t mmio_read16(uintptr_t p) { return ek_api->read16(p); }
uint32_t mmio_read32(uintptr_t p) { return ek_api->read32(p); }
uint64_t mmio_read64(uintptr_t p) { return mmio_read32(p) | ((uint64_t)mmio_read32(p + 4) << 32); }
void mmio_write8(uintptr_t p, uint8_t v) { ek_api->write8(p, v); }
void mmio_write16(uintptr_t p, uint16_t v) { ek_api->write16(p, v); }
void mmio_write32(uintptr_t p, uint32_t v) { ek_api->write32(p, v); }
void mmio_write64(uintptr_t p, uint64_t v) { mmio_write32(p, (uint32_t)v); mmio_write32(p + 4, v >> 32); }
void mmio_dmb(void) { __asm__ volatile("dmb sy" ::: "memory"); }
void mmio_dsb(void) { __asm__ volatile("dsb sy" ::: "memory"); }
void mmio_isb(void) { __asm__ volatile("isb" ::: "memory"); }
uint64_t clock_now(void) { return ek_api->counter_read(); }
uint64_t clock_frequency(void) { return ek_api->counter_frequency(); }
uint64_t clock_ms(void) { return ek_api->now_ms(); }
uint64_t clock_after_ms(uint32_t ms)
{ uint64_t n = (clock_frequency() * ms + 999U) / 1000U; return clock_now() + (n ? n : 1); }
void clock_delay_us(uint32_t us) { ek_api->delay_us(us); }
int task_sleep_ms(uint32_t ms) { ek_api->sleep_ms(ms); return 0; }
void arch_dma_sync(void *p, size_t n) { ek_api->dma_sync(p, n, EK_DMA_TO_DEVICE); }
bool kernel_gate_try(struct kernel_gate *g) { return ek_api->gate_try(g); }
void kernel_gate_release(struct kernel_gate *g) { ek_api->gate_release(g); }
void uart_rx_push(struct uart_device *d, char c) { ek_api->uart_rx_push(d, c); }
void uart_rx_overrun(struct uart_device *d) { ek_api->uart_rx_overrun(d); }
