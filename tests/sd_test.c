#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <drivers/mmc/sd.h>
#include <block/block.h>
#include <kernel/clock.h>
#include <kernel/error.h>
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "SD line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static unsigned int checks, commands, acmds, transfers;
static bool legacy, wrong_echo, never_ready, fail_transfer;
static uint32_t last_address, last_command;
static uint64_t elapsed;
uint64_t clock_ms(void) { return elapsed++; }
void clock_delay_us(uint32_t us) { elapsed += us / 1000; }
int sdhci_init(struct sdhci_host *host) { (void)host; acmds = 0; return 0; }
int sdhci_set_clock(struct sdhci_host *host, uint32_t rate) { host->current_clock_hz = rate; return 0; }
static void csd_bits(uint32_t response[4], unsigned int high, unsigned int low, uint32_t value)
{
    for (unsigned int bit = low; bit <= high; ++bit)
        response[(bit - 8) / 32] |= ((value >> (bit - low)) & 1U) << ((bit - 8) % 32);
}
int sdhci_send_command(struct sdhci_host *host, struct sdhci_command *cmd)
{
    (void)host; ++commands;
    switch (cmd->index) {
    case 0: break;
    case 8:
        if (legacy) return -K_ETIMEDOUT;
        cmd->response[0] = wrong_echo ? 0x1AB : 0x1AA; break;
    case 55: cmd->response[0] = 1U << 5; break;
    case 41:
        CHECK(!!(cmd->argument & (1U << 30)) != legacy);
        cmd->response[0] = 0x00FF8000U | (legacy ? 0 : 1U << 30);
        if (++acmds > 1 && !never_ready) cmd->response[0] |= 1U << 31;
        break;
    case 2: break;
    case 3: cmd->response[0] = 0x12340000; break;
    case 9:
        if (legacy) {
            csd_bits(cmd->response, 83, 80, 9);
            csd_bits(cmd->response, 73, 62, 1023);
            csd_bits(cmd->response, 49, 47, 2);
        } else {
            csd_bits(cmd->response, 127, 126, 1);
            csd_bits(cmd->response, 69, 48, 7);
        }
        break;
    case 7: case 16: break;
    default: CHECK(false);
    }
    return 0;
}
int sdhci_transfer(struct sdhci_host *host, struct sdhci_command *cmd,
                    void *buffer, uint16_t block_size, uint16_t block_count, bool write)
{
    (void)host; (void)buffer;
    CHECK(block_size == 512 && block_count == 1);
    CHECK(cmd->index == (write ? 24 : 17));
    ++transfers; last_address = cmd->argument; last_command = cmd->index;
    return fail_transfer ? -K_ETIMEDOUT : 0;
}
int main(void)
{
    struct sd_card card = {0}; unsigned char data[1024];
    CHECK(sd_card_init(&card) == 0 && card.block_count == 8192 && card.high_capacity && card.initialized);
    CHECK(sd_read_blocks(&card, 7, 2, data) == 0 && last_address == 8 && last_command == 17);
    CHECK(sd_write_blocks(&card, 9, 1, data) == 0 && last_address == 9 && last_command == 24);
    CHECK(sd_read_blocks(&card, 8191, 2, data) < 0);
    CHECK(sd_write_blocks(&card, ~0ULL, 1, data) < 0);
    CHECK(sd_read_blocks(NULL, 0, 1, data) < 0);
    CHECK(sd_read_blocks(&card, 0, 1, NULL) < 0);
    fail_transfer = true; unsigned int saved = transfers;
    CHECK(sd_read_blocks(&card, 0, 1, data) == -K_ETIMEDOUT && transfers == saved + 2);
    saved = transfers;
    CHECK(sd_write_blocks(&card, 0, 1, data) == -K_ETIMEDOUT && transfers == saved + 1);
    fail_transfer = false; legacy = true;
    CHECK(sd_card_init(&card) == 0 && !card.high_capacity && card.block_count == 16384);
    CHECK(sd_read_blocks(&card, 3, 1, data) == 0 && last_address == 1536);
    legacy = false; wrong_echo = true;
    CHECK(sd_card_init(&card) < 0 && !card.initialized);
    wrong_echo = false; never_ready = true;
    uint64_t start = elapsed;
    CHECK(sd_card_init(&card) == -K_ETIMEDOUT && elapsed - start >= 2000 && !card.initialized);
    printf("SD v1/v2/capacity/addressing/retry/timeout: PASS (%u checks)\n", checks);
}
