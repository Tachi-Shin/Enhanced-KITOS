// drivers/uart/pl011.c

#include <dos/type.h>

#include <arch/io.h>

#include <drivers/uart/uart.h>
#include <drivers/uart/pl011/pl011.h>

/*
 * Raspberry Pi 4 / BCM2711のGPIO物理アドレス。
 */
#define GPIO_BASE       0xFE200000UL

#define GPIO_GPFSEL1    0x04U
#define GPIO_PUP_PDN0   0xE4U

/*
 * Raspberry Pi 4 / BCM2711のPL011 UART0物理アドレス。
 */
#define PL011_BASE      0xFE201000UL

/*
 * PL011レジスタオフセット。
 */
#define UART_DR         0x00U
#define UART_FR         0x18U
#define UART_IBRD       0x24U
#define UART_FBRD       0x28U
#define UART_LCRH       0x2CU
#define UART_CR         0x30U
#define UART_IFLS       0x34U
#define UART_IMSC       0x38U
#define UART_ICR        0x44U

/*
 * UART_FRビット。
 */
#define UART_FR_BUSY    (1U << 3)
#define UART_FR_RXFE    (1U << 4)
#define UART_FR_TXFF    (1U << 5)

/*
 * UART_LCRHビット。
 */
#define UART_LCRH_FEN   (1U << 4)
#define UART_LCRH_WLEN8 (3U << 5)

/*
 * UART_CRビット。
 */
#define UART_CR_UARTEN  (1U << 0)
#define UART_CR_TXE     (1U << 8)
#define UART_CR_RXE     (1U << 9)

static int pl011_init(struct uart_device *dev);
static void pl011_putc(struct uart_device *dev, char c);

static void pl011_enable_rx_irq(struct uart_device *dev);
static void pl011_handle_irq(struct uart_device *dev);

static const struct uart_ops pl011_ops = {
    .init = pl011_init,
    .putc = pl011_putc,
    .enable_rx_irq = pl011_enable_rx_irq,
    .handle_irq = pl011_handle_irq,
};

static struct uart_device pl011_device = {
    .name         = "PL011",
    .base_address = PL011_BASE,
    .irq_intid    = 153U,
    .ops          = &pl011_ops,
};

struct uart_device *pl011_get_device(void)
{
    return &pl011_device;
}

static int pl011_init(struct uart_device *dev)
{
    uintptr_t base;

    if (dev == NULL) {
        return -1;
    }

    base = dev->base_address;

    /*
     * PL011を無効化する。
     */
    mmio_write32(base + UART_CR, 0U);

    /*
     * 送信処理が残っている場合は完了を待つ。
     */
    while ((mmio_read32(base + UART_FR) & UART_FR_BUSY) != 0U) {
    }

    /*
     * GPIO14/15をTXD0/RXD0へ接続する。
     */
    /* Pin routing is configured by platform_init(). */

    /*
     * UARTクロック48 MHz、115200 baud。
     *
     * baud divisor:
     *
     * 48,000,000 / (16 × 115,200)
     * = 26.041666...
     *
     * IBRD = 26
     * FBRD = round(0.041666... × 64)
     *      = 3
     */
    mmio_write32(base + UART_IBRD, 26U);
    mmio_write32(base + UART_FBRD, 3U);

    /*
     * 8データビット、パリティなし、
     * 1ストップビット、FIFO有効。
     */
    mmio_write32(
        base + UART_LCRH,
        UART_LCRH_WLEN8 | UART_LCRH_FEN
    );

    /*
     * FIFO割り込みレベル。
     * 現在は割り込みを使わないため0とする。
     */
    mmio_write32(base + UART_IFLS, 0U);

    /*
     * 全UART割り込みを無効化する。
     */
    mmio_write32(base + UART_IMSC, 0U);

    /*
     * 保留中のUART割り込みをすべて消去する。
     */
    mmio_write32(base + UART_ICR, 0x7FFU);

    /*
     * UART本体、送信、受信を有効化する。
     */
    mmio_write32(
        base + UART_CR,
        UART_CR_UARTEN |
        UART_CR_TXE |
        UART_CR_RXE
    );

    mmio_dsb();

    return 0;
}

static void pl011_putc(struct uart_device *dev, char c)
{
    uintptr_t base;

    if (dev == NULL) {
        return;
    }

    base = dev->base_address;

    /*
     * TX FIFOが満杯でなくなるまで待つ。
     */
    while ((mmio_read32(base + UART_FR) & UART_FR_TXFF) != 0U) {
    }

    mmio_write32(
        base + UART_DR,
        (uint32_t)(uint8_t)c
    );
}

static void pl011_enable_rx_irq(struct uart_device *dev)
{
    mmio_write32(dev->base_address + UART_ICR, 0x7FFU);
    mmio_write32(dev->base_address + UART_IMSC, (1U << 4) | (1U << 6) | (1U << 10));
    mmio_dsb();
}
static void pl011_handle_irq(struct uart_device *dev)
{
    for (unsigned int budget = 0; budget < 64; ++budget) {
        if (mmio_read32(dev->base_address + UART_FR) & UART_FR_RXFE) break;
        uint32_t data = mmio_read32(dev->base_address + UART_DR);
        if (data & 0xF00U) uart_rx_overrun(dev);
        else uart_rx_push(dev, (char)data);
    }
    mmio_write32(dev->base_address + UART_ICR, 0x7FFU);
    mmio_dsb();
}
