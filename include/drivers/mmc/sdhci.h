#ifndef DOS_DRIVERS_MMC_SDHCI_H
#define DOS_DRIVERS_MMC_SDHCI_H

#include <dos/type.h>


enum sdhci_response_type {
    SDHCI_RESPONSE_NONE,
    SDHCI_RESPONSE_136,
    SDHCI_RESPONSE_48,
    SDHCI_RESPONSE_48_BUSY,
};

struct sdhci_command {
    uint8_t index;
    uint32_t argument;
    enum sdhci_response_type response_type;
    bool crc_check;
    bool index_check;
    bool data_present;
    uint32_t response[4];
};

struct sdhci_host {
    uintptr_t base_address;
    uint32_t input_clock_hz;
    uint32_t current_clock_hz;
    uint16_t version;
    uint32_t last_error;
};

/* Platform-selected accessors; offsets are SDHCI register offsets. */
uint8_t sdhci_read8(struct sdhci_host *host, uint32_t offset);
uint16_t sdhci_read16(struct sdhci_host *host, uint32_t offset);
uint32_t sdhci_read32(struct sdhci_host *host, uint32_t offset);
void sdhci_write8(struct sdhci_host *host, uint32_t offset, uint8_t value);
void sdhci_write16(struct sdhci_host *host, uint32_t offset, uint16_t value);
void sdhci_write32(struct sdhci_host *host, uint32_t offset, uint32_t value);
int sdhci_init(struct sdhci_host *host);
int sdhci_set_clock(struct sdhci_host *host, uint32_t frequency_hz);
int sdhci_send_command(struct sdhci_host *host,
                       struct sdhci_command *command);
int sdhci_transfer(struct sdhci_host *host, struct sdhci_command *command,
                   void *buffer, uint16_t block_size, uint16_t block_count,
                   bool write);

#endif
