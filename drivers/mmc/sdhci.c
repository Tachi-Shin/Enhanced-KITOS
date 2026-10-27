#include <arch/io.h>
#include <kernel/clock.h>
#include <kernel/error.h>
#include <drivers/mmc/sdhci.h>

#define SDHCI_BLOCK_SIZE_COUNT       0x04U
#define SDHCI_ARGUMENT               0x08U
#define SDHCI_TRANSFER_MODE          0x0CU
#define SDHCI_COMMAND                0x0EU
#define SDHCI_RESPONSE               0x10U
#define SDHCI_BUFFER                 0x20U
#define SDHCI_PRESENT_STATE          0x24U
#define SDHCI_HOST_CONTROL           0x28U
#define SDHCI_POWER_CONTROL          0x29U
#define SDHCI_CLOCK_CONTROL          0x2CU
#define SDHCI_TIMEOUT_CONTROL        0x2EU
#define SDHCI_SOFTWARE_RESET         0x2FU
#define SDHCI_INT_STATUS             0x30U
#define SDHCI_INT_STATUS_ENABLE      0x34U
#define SDHCI_INT_SIGNAL_ENABLE      0x38U
#define SDHCI_CAPABILITIES           0x40U
#define SDHCI_HOST_VERSION           0xFEU

#define SDHCI_STATE_CMD_INHIBIT      (1U << 0)
#define SDHCI_STATE_DAT_INHIBIT      (1U << 1)
#define SDHCI_STATE_CARD_INSERTED    (1U << 16)

#define SDHCI_CLOCK_INT_ENABLE       (1U << 0)
#define SDHCI_CLOCK_INT_STABLE       (1U << 1)
#define SDHCI_CLOCK_CARD_ENABLE      (1U << 2)

#define SDHCI_RESET_ALL              (1U << 0)
#define SDHCI_RESET_CMD              (1U << 1)
#define SDHCI_RESET_DATA             (1U << 2)

#define SDHCI_INT_COMMAND_COMPLETE   (1U << 0)
#define SDHCI_INT_TRANSFER_COMPLETE  (1U << 1)
#define SDHCI_INT_BUFFER_WRITE_READY (1U << 4)
#define SDHCI_INT_BUFFER_READ_READY  (1U << 5)
#define SDHCI_INT_ERROR_MASK         0xFFFF0000U

#define SDHCI_TRNS_BLOCK_COUNT_EN    (1U << 1)
#define SDHCI_TRNS_READ              (1U << 4)
#define SDHCI_TRNS_MULTI             (1U << 5)

#define SDHCI_CMD_RESP_NONE          0U
#define SDHCI_CMD_RESP_LONG          1U
#define SDHCI_CMD_RESP_SHORT         2U
#define SDHCI_CMD_RESP_SHORT_BUSY    3U
#define SDHCI_CMD_CRC                (1U << 3)
#define SDHCI_CMD_INDEX              (1U << 4)
#define SDHCI_CMD_DATA               (1U << 5)

#define SDHCI_TIMEOUT_MS 1000U

static int sdhci_wait_clear(struct sdhci_host *host, uint32_t mask)
{
    uint64_t start = clock_ms();

    while (clock_ms() - start < SDHCI_TIMEOUT_MS) {
        if ((sdhci_read32(host, SDHCI_PRESENT_STATE) & mask) == 0U) {
            return 0;
        }
    }
    return -K_ETIMEDOUT;
}

static int sdhci_wait_interrupt(struct sdhci_host *host, uint32_t wanted)
{
    uint64_t start = clock_ms();
    uint32_t status;

    while (clock_ms() - start < SDHCI_TIMEOUT_MS) {
        status = sdhci_read32(host, SDHCI_INT_STATUS);
        if ((status & SDHCI_INT_ERROR_MASK) != 0U) {
            host->last_error = status;
            sdhci_write32(host, SDHCI_INT_STATUS, status);
            return -K_EIO;
        }
        if ((status & wanted) != 0U) {
            sdhci_write32(host, SDHCI_INT_STATUS, wanted);
            return 0;
        }
    }
    return -K_ETIMEDOUT;
}

static int sdhci_wait_data_done(struct sdhci_host *host)
{
    uint64_t start = clock_ms();
    uint32_t status;

    while (clock_ms() - start < SDHCI_TIMEOUT_MS) {
        status = sdhci_read32(host, SDHCI_INT_STATUS);
        if ((status & SDHCI_INT_ERROR_MASK) != 0U) {
            host->last_error = status;
            sdhci_write32(host, SDHCI_INT_STATUS, status);
            return -K_EIO;
        }
        if ((status & SDHCI_INT_TRANSFER_COMPLETE) != 0U) {
            sdhci_write32(host, SDHCI_INT_STATUS,
                         SDHCI_INT_TRANSFER_COMPLETE);
            return 0;
        }
    }
    return -K_ETIMEDOUT;
}

static int sdhci_reset_lines(struct sdhci_host *host, uint8_t mask)
{
    uint64_t start = clock_ms();

    sdhci_write8(host, SDHCI_SOFTWARE_RESET, mask);
    while (clock_ms() - start < SDHCI_TIMEOUT_MS) {
        if ((sdhci_read8(host, SDHCI_SOFTWARE_RESET) & mask) == 0U) {
            return 0;
        }
    }
    return -K_ETIMEDOUT;
}

int sdhci_set_clock(struct sdhci_host *host, uint32_t frequency_hz)
{
    uint32_t divisor;
    uint32_t encoded_divisor;
    uint16_t clock;
    uint64_t start = clock_ms();

    if (host == NULL || frequency_hz == 0U || host->input_clock_hz == 0U) {
        return -K_EINVAL;
    }

    clock = sdhci_read16(host, SDHCI_CLOCK_CONTROL);
    clock &= (uint16_t)~SDHCI_CLOCK_CARD_ENABLE;
    sdhci_write16(host, SDHCI_CLOCK_CONTROL, clock);

    divisor = (uint32_t)(((uint64_t)host->input_clock_hz + frequency_hz - 1U) / frequency_hz);
    if ((host->version & 0xFFU) < 2U) {
        uint32_t power = 1U;
        while (power < divisor && power < 256U) power <<= 1;
        if (power < divisor) return -K_EINVAL;
        divisor = power;
    } else if (divisor > 1U && (divisor & 1U)) ++divisor;
    encoded_divisor = divisor > 1U ? divisor / 2U : 0U;
    if (encoded_divisor > 0x3FFU) return -K_EINVAL;

    clock = (uint16_t)SDHCI_CLOCK_INT_ENABLE;
    clock |= (uint16_t)((encoded_divisor & 0xFFU) << 8);
    clock |= (uint16_t)((encoded_divisor & 0x300U) >> 2);
    sdhci_write16(host, SDHCI_CLOCK_CONTROL, clock);

    while (clock_ms() - start < SDHCI_TIMEOUT_MS) {
        clock = sdhci_read16(host, SDHCI_CLOCK_CONTROL);
        if ((clock & SDHCI_CLOCK_INT_STABLE) != 0U) {
            clock |= (uint16_t)SDHCI_CLOCK_CARD_ENABLE;
            sdhci_write16(host, SDHCI_CLOCK_CONTROL, clock);
            host->current_clock_hz = host->input_clock_hz / (divisor ? divisor : 1U);
            return 0;
        }
    }
    return -K_ETIMEDOUT;
}

int sdhci_init(struct sdhci_host *host)
{
    uint32_t capabilities;
    uint32_t base_clock_mhz;
    uint64_t start = clock_ms();

    if (host == NULL || host->base_address == 0UL) {
        return -K_EIO;
    }
    int rc = sdhci_reset_lines(host, SDHCI_RESET_ALL);
    if (rc) return rc;

    host->version = sdhci_read16(host, SDHCI_HOST_VERSION);
    capabilities = sdhci_read32(host, SDHCI_CAPABILITIES);
    base_clock_mhz = (capabilities >> 8) & 0xFFU;
    if (!host->input_clock_hz) host->input_clock_hz = base_clock_mhz * 1000000U;
    if (!host->input_clock_hz) return -K_ENODEV;

    sdhci_write8(host, SDHCI_POWER_CONTROL, 0x0FU);
    sdhci_write8(host, SDHCI_TIMEOUT_CONTROL, 0x0EU);
    sdhci_write32(host, SDHCI_INT_STATUS, 0xFFFFFFFFU);
    sdhci_write32(host, SDHCI_INT_STATUS_ENABLE, 0xFFFFFFFFU);
    sdhci_write32(host, SDHCI_INT_SIGNAL_ENABLE, 0U);

    rc = sdhci_set_clock(host, 400000U);
    if (rc) return rc;
    clock_delay_us(1000U); /* power/initial clock stabilization */
    start = clock_ms();
    while (clock_ms() - start < SDHCI_TIMEOUT_MS) {
        if ((sdhci_read32(host, SDHCI_PRESENT_STATE) &
             SDHCI_STATE_CARD_INSERTED) != 0U) {
            return 0;
        }
    }
    return -K_ETIMEDOUT;
}

static uint16_t sdhci_encode_command(const struct sdhci_command *command)
{
    uint16_t value = (uint16_t)command->index << 8;

    switch (command->response_type) {
    case SDHCI_RESPONSE_136:
        value |= SDHCI_CMD_RESP_LONG;
        break;
    case SDHCI_RESPONSE_48:
        value |= SDHCI_CMD_RESP_SHORT;
        break;
    case SDHCI_RESPONSE_48_BUSY:
        value |= SDHCI_CMD_RESP_SHORT_BUSY;
        break;
    default:
        value |= SDHCI_CMD_RESP_NONE;
        break;
    }
    if (command->crc_check) {
        value |= SDHCI_CMD_CRC;
    }
    if (command->index_check) {
        value |= SDHCI_CMD_INDEX;
    }
    if (command->data_present) {
        value |= SDHCI_CMD_DATA;
    }
    return value;
}

static void sdhci_read_response(struct sdhci_host *host,
                                struct sdhci_command *command)
{
    unsigned int words = command->response_type == SDHCI_RESPONSE_136 ? 4U : 1U;
    unsigned int index;

    for (index = 0U; index < words; index++) {
        command->response[index] =
            sdhci_read32(host, SDHCI_RESPONSE + index * 4U);
    }
}

static int sdhci_issue_command(struct sdhci_host *host,
                               struct sdhci_command *command,
                               uint16_t transfer_mode)
{
    uint32_t inhibit = SDHCI_STATE_CMD_INHIBIT;

    if (command->data_present || command->response_type == SDHCI_RESPONSE_48_BUSY) {
        inhibit |= SDHCI_STATE_DAT_INHIBIT;
    }
    int rc = sdhci_wait_clear(host, inhibit);
    if (rc) { (void)sdhci_reset_lines(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA); return rc; }
    host->last_error = 0;

    sdhci_write32(host, SDHCI_INT_STATUS, 0xFFFFFFFFU);
    sdhci_write32(host, SDHCI_ARGUMENT, command->argument);
    mmio_dmb();
    /* One transaction: no command is issued by a transfer-mode RMW. */
    sdhci_write32(host, SDHCI_TRANSFER_MODE,
                  transfer_mode | ((uint32_t)sdhci_encode_command(command) << 16));

    rc = sdhci_wait_interrupt(host, SDHCI_INT_COMMAND_COMPLETE);
    if (rc) {
        (void)sdhci_reset_lines(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
        return rc;
    }
    sdhci_read_response(host, command);
    return 0;
}

int sdhci_send_command(struct sdhci_host *host,
                       struct sdhci_command *command)
{
    if (host == NULL || command == NULL || command->data_present) {
        return -K_EIO;
    }
    int rc = sdhci_issue_command(host, command, 0U);
    if (rc) return rc;
    if (command->response_type == SDHCI_RESPONSE_48_BUSY) {
        rc = sdhci_wait_interrupt(host, SDHCI_INT_TRANSFER_COMPLETE);
        if (rc) { (void)sdhci_reset_lines(host, SDHCI_RESET_DATA); return rc; }
    }
    return 0;
}

int sdhci_transfer(struct sdhci_host *host, struct sdhci_command *command,
                   void *buffer, uint16_t block_size, uint16_t block_count,
                   bool write)
{
    uint16_t mode = SDHCI_TRNS_BLOCK_COUNT_EN;
    uint32_t ready = write ? SDHCI_INT_BUFFER_WRITE_READY
                           : SDHCI_INT_BUFFER_READ_READY;
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t word_count;
    uint32_t block;
    uint32_t word;

    if (host == NULL || command == NULL || buffer == NULL ||
        block_size == 0U || block_size > 2048U || block_count == 0U ||
        block_count != 1U || (block_size & 3U) != 0U || !command->data_present) {
        return -K_EIO;
    }
    if (!write) {
        mode |= SDHCI_TRNS_READ;
    }

    sdhci_write32(host, SDHCI_BLOCK_SIZE_COUNT,
                 (uint32_t)block_size | ((uint32_t)block_count << 16));
    int rc = sdhci_issue_command(host, command, mode);
    if (rc) return rc;

    word_count = (uint32_t)block_size / 4U;
    for (block = 0U; block < block_count; block++) {
        rc = sdhci_wait_interrupt(host, ready);
        if (rc) {
            (void)sdhci_reset_lines(host, SDHCI_RESET_DATA);
            return rc;
        }
        for (word = 0U; word < word_count; word++) {
            uint32_t position =
                (block * word_count + word) * (uint32_t)sizeof(uint32_t);
            if (write) {
                uint32_t value = (uint32_t)bytes[position] |
                    ((uint32_t)bytes[position + 1U] << 8) |
                    ((uint32_t)bytes[position + 2U] << 16) |
                    ((uint32_t)bytes[position + 3U] << 24);
                sdhci_write32(host, SDHCI_BUFFER,
                             value);
            } else {
                uint32_t value =
                    sdhci_read32(host, SDHCI_BUFFER);
                bytes[position] = (uint8_t)value;
                bytes[position + 1U] = (uint8_t)(value >> 8);
                bytes[position + 2U] = (uint8_t)(value >> 16);
                bytes[position + 3U] = (uint8_t)(value >> 24);
            }
        }
    }
    rc = sdhci_wait_data_done(host);
    if (rc) {
        (void)sdhci_reset_lines(host, SDHCI_RESET_DATA);
        return rc;
    }
    mmio_dmb();
    return 0;
}
