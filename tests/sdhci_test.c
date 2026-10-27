#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <drivers/mmc/sdhci.h>
#include <kernel/error.h>
#include <kernel/clock.h>
#include <arch/io.h>
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "SDHCI line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint32_t registers[64], fifo_words, issued, resets;
static unsigned int checks, delays;
static uint64_t milliseconds;
static bool inject_error, never_complete;
uint64_t clock_ms(void) { return milliseconds++; }
void clock_delay_us(uint32_t us) { CHECK(us >= 10); ++delays; }
void mmio_dmb(void) {}
void mmio_dsb(void) {}
uint32_t mmio_read32(uintptr_t address)
{
    CHECK(address >= 0x1000 && address < 0x1100 && !(address & 3));
    unsigned int offset = (unsigned int)(address - 0x1000);
    if (offset == 0x20) {
        if (++fifo_words == 128) registers[0x30 / 4] |= 2;
        return 0x03020100U;
    }
    return registers[offset / 4];
}
void mmio_write32(uintptr_t address, uint32_t value)
{
    CHECK(address >= 0x1000 && address < 0x1100 && !(address & 3));
    unsigned int offset = (unsigned int)(address - 0x1000);
    if (offset == 0x30) registers[offset / 4] &= ~value;
    else if (offset == 0x2C) {
        if (value >> 24) ++resets;
        registers[offset / 4] = (value & 0x00FFFFFFU) | (value & 1 ? 2 : 0);
    } else if (offset == 0x0C) {
        ++issued; registers[offset / 4] = value;
        fifo_words = 0;
        registers[0x10 / 4] = 0x900U;
        if (!never_complete) registers[0x30 / 4] = inject_error ? 0x18000U : 1U | ((value & 0x20U << 16) ? 0x30U : 2U);
    } else if (offset == 0x20) {
        if (++fifo_words == 128) registers[0x30 / 4] |= 2;
    } else registers[offset / 4] = value;
}
int main(void)
{
    struct sdhci_host host = {.base_address = 0x1000};
    registers[0xFC / 4] = 2U << 16;
    registers[0x40 / 4] = 100U << 8;
    registers[0x24 / 4] = 1U << 16;
    CHECK(sdhci_init(&host) == 0 && host.current_clock_hz == 400000 && delays > 0);
    CHECK(registers[0x38 / 4] == 0);
    CHECK(sdhci_set_clock(&host, 25000000) == 0 && host.current_clock_hz == 25000000);
    struct sdhci_command command = {.index = 17, .response_type = SDHCI_RESPONSE_48,
        .crc_check = true, .index_check = true, .data_present = true};
    unsigned char buffer[512];
    CHECK(sdhci_transfer(&host, &command, buffer, 512, 1, false) == 0 && fifo_words == 128);
    CHECK(issued == 1 && (registers[0x0C / 4] >> 24) == 17 && (registers[0x0C / 4] & 0xFFFF) == 18);
    for (unsigned int i = 0; i < 512; ++i) CHECK(buffer[i] == i % 4);
    command.index = 24;
    CHECK(sdhci_transfer(&host, &command, buffer, 512, 1, true) == 0);
    CHECK(issued == 2 && (registers[0x0C / 4] & 0xFFFF) == 2);
    CHECK(sdhci_transfer(&host, &command, buffer, 512, 2, true) < 0);
    inject_error = true; command.data_present = false;
    unsigned int old_resets = resets;
    CHECK(sdhci_send_command(&host, &command) < 0 && host.last_error == 0x18000U && resets > old_resets);
    inject_error = false; never_complete = true;
    uint64_t start = milliseconds;
    CHECK(sdhci_send_command(&host, &command) < 0 && milliseconds - start >= 1000);
    never_complete = false;
    host.version = 1;
    CHECK(sdhci_set_clock(&host, 400000) == 0 && host.current_clock_hz == 390625);
    CHECK(sdhci_set_clock(&host, 1) == -K_EINVAL);
    printf("BCM2711 SDHCI 32-bit MMIO/PIO/errors/timeouts/clocks: PASS (%u checks)\n", checks);
}
