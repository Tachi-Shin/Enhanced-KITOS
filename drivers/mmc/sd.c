#include <block/block.h>
#include <drivers/mmc/sd.h>
#include <kernel/clock.h>
#include <kernel/error.h>

#define SD_CMD_GO_IDLE             0U
#define SD_CMD_SEND_IF_COND        8U
#define SD_CMD_ALL_SEND_CID        2U
#define SD_CMD_SEND_RELATIVE_ADDR  3U
#define SD_CMD_SEND_CSD            9U
#define SD_CMD_SELECT_CARD         7U
#define SD_CMD_SET_BLOCKLEN        16U
#define SD_CMD_READ_SINGLE         17U
#define SD_CMD_WRITE_SINGLE        24U
#define SD_CMD_APP                 55U
#define SD_ACMD_SEND_OP_COND       41U

#define SD_OCR_BUSY                (1U << 31)
#define SD_OCR_HIGH_CAPACITY       (1U << 30)
#define SD_OCR_VOLTAGE             0x00FF8000U
#define SD_INIT_TIMEOUT_MS 2000U

static int sd_block_init(struct block_device *device);
static int sd_block_read(struct block_device *device, uint64_t lba,
                         uint32_t count, void *buffer);
static int sd_block_write(struct block_device *device, uint64_t lba,
                          uint32_t count, const void *buffer);

static const struct block_device_ops sd_block_ops = {
    .init = sd_block_init,
    .read = sd_block_read,
    .write = sd_block_write,
};

static void sd_prepare_command(struct sdhci_command *command, uint8_t index,
                               uint32_t argument,
                               enum sdhci_response_type response,
                               bool crc, bool index_check, bool data)
{
    unsigned int i;

    command->index = index;
    command->argument = argument;
    command->response_type = response;
    command->crc_check = crc;
    command->index_check = index_check;
    command->data_present = data;
    for (i = 0U; i < 4U; i++) {
        command->response[i] = 0U;
    }
}

static int sd_command(struct sd_card *card, uint8_t index, uint32_t argument,
                      enum sdhci_response_type response, bool crc,
                      bool index_check, uint32_t *response0)
{
    struct sdhci_command command;
    int result;

    sd_prepare_command(&command, index, argument, response, crc,
                       index_check, false);
    result = sdhci_send_command(&card->host, &command);
    if (result == 0 && response == SDHCI_RESPONSE_48_BUSY &&
        (command.response[0] & 0xFDFFE008U)) return -K_EIO;
    if (result == 0 && (index == SD_CMD_APP || index == SD_CMD_SET_BLOCKLEN) &&
        (command.response[0] & 0xFDFFE008U)) return -K_EIO;
    if (result == 0 && response0 != NULL) {
        *response0 = command.response[0];
    }
    return result;
}

static int sd_app_command(struct sd_card *card, uint8_t index,
                          uint32_t argument, uint32_t *response0)
{
    if (sd_command(card, SD_CMD_APP, card->relative_address << 16,
                   SDHCI_RESPONSE_48, true, true, NULL) != 0) {
        return -K_EIO;
    }
    return sd_command(card, index, argument, SDHCI_RESPONSE_48,
                      false, false, response0);
}

/* SDHCIの136-bit応答はCRCを除いたため、CSD bit 8がresponse[0] bit 0。 */
static uint32_t sd_csd_bits(const uint32_t response[4],
                            unsigned int high, unsigned int low)
{
    uint32_t value = 0U;
    unsigned int bit;
    for (bit = low; bit <= high; bit++) {
        unsigned int shifted = bit - 8U;
        value |= ((response[shifted / 32U] >> (shifted % 32U)) & 1U)
                 << (bit - low);
    }
    return value;
}

static int sd_read_capacity(struct sd_card *card)
{
    struct sdhci_command command;
    uint32_t structure;

    sd_prepare_command(&command, SD_CMD_SEND_CSD,
                       card->relative_address << 16,
                       SDHCI_RESPONSE_136, true, false, false);
    if (sdhci_send_command(&card->host, &command) != 0) {
        return -K_EIO;
    }
    structure = sd_csd_bits(command.response, 127U, 126U);
    if (structure == 1U) {
        uint32_t size = sd_csd_bits(command.response, 69U, 48U);
        card->block_count = ((uint64_t)size + 1ULL) * 1024ULL;
    } else if (structure == 0U) {
        uint32_t read_length = sd_csd_bits(command.response, 83U, 80U);
        uint32_t size = sd_csd_bits(command.response, 73U, 62U);
        uint32_t multiplier = sd_csd_bits(command.response, 49U, 47U);
        uint64_t bytes;
        if (read_length > 31U || multiplier > 7U) {
            return -K_EIO;
        }
        bytes = ((uint64_t)size + 1ULL) *
                (1ULL << (multiplier + 2U)) * (1ULL << read_length);
        card->block_count = bytes / BLOCK_SECTOR_SIZE;
    } else {
        return -K_EIO;
    }
    return card->block_count == 0ULL ? -K_EIO : 0;
}

int sd_card_init(struct sd_card *card)
{
    uint32_t response;
    uint64_t start;
    bool version2;

    if (card == NULL) {
        return -K_EIO;
    }
    card->relative_address = 0U;
    card->ocr = 0U;
    card->block_count = 0ULL;
    card->high_capacity = false;
    card->initialized = false;

    int init_rc = sdhci_init(&card->host);
    if (init_rc) return init_rc;
    if (sd_command(card, SD_CMD_GO_IDLE, 0U, SDHCI_RESPONSE_NONE,
                   false, false, NULL) != 0) {
        return -K_EIO;
    }
    int rc = sd_command(card, SD_CMD_SEND_IF_COND, 0x1AAU, SDHCI_RESPONSE_48,
                         true, true, &response);
    version2 = rc == 0 && !(response & (1U << 22));
    if (rc && rc != -K_ETIMEDOUT) return rc;
    if (version2 && (response & 0xFFFU) != 0x1AAU) return -K_EIO;

    start = clock_ms();
    do {
        if (sd_app_command(card, SD_ACMD_SEND_OP_COND,
                           (version2 ? SD_OCR_HIGH_CAPACITY : 0U) | SD_OCR_VOLTAGE,
                           &response) != 0) {
            return -K_EIO;
        }
        if ((response & SD_OCR_BUSY) != 0U) {
            if (!(response & SD_OCR_VOLTAGE)) return -K_EIO;
            card->ocr = response;
            card->high_capacity = (response & SD_OCR_HIGH_CAPACITY) != 0U;
            break;
        }
        clock_delay_us(1000U);
    } while (clock_ms() - start < SD_INIT_TIMEOUT_MS);
    if (!(card->ocr & SD_OCR_BUSY)) {
        return -K_ETIMEDOUT;
    }
    if (sd_command(card, SD_CMD_ALL_SEND_CID, 0U, SDHCI_RESPONSE_136,
                   true, false, NULL) != 0 ||
        sd_command(card, SD_CMD_SEND_RELATIVE_ADDR, 0U, SDHCI_RESPONSE_48,
                   true, true, &response) != 0) {
        return -K_EIO;
    }
    if (response & 0xE000U) return -K_EIO;
    card->relative_address = response >> 16;
    if (card->relative_address == 0U) {
        return -K_EIO;
    }
    if (sd_read_capacity(card) != 0 ||
        sd_command(card, SD_CMD_SELECT_CARD, card->relative_address << 16,
                   SDHCI_RESPONSE_48_BUSY, true, true, NULL) != 0) {
        return -K_EIO;
    }
    if (!card->high_capacity &&
        sd_command(card, SD_CMD_SET_BLOCKLEN, BLOCK_SECTOR_SIZE,
                   SDHCI_RESPONSE_48, true, true, NULL) != 0) {
        return -K_EIO;
    }
    if (sdhci_set_clock(&card->host, 25000000U) != 0) {
        return -K_EIO;
    }
    card->initialized = true;
    return 0;
}

static int sd_transfer_one(struct sd_card *card, uint64_t lba,
                           void *buffer, bool write)
{
    struct sdhci_command command;
    uint64_t address;

    if (card == NULL || !card->initialized || buffer == NULL) {
        return -K_EIO;
    }
    if (lba >= card->block_count) return -K_EINVAL;
    address = card->high_capacity ? lba : lba * BLOCK_SECTOR_SIZE;
    if (address > 0xFFFFFFFFULL) {
        return -K_EIO;
    }
    sd_prepare_command(&command,
                       write ? SD_CMD_WRITE_SINGLE : SD_CMD_READ_SINGLE,
                       (uint32_t)address, SDHCI_RESPONSE_48,
                       true, true, true);
    int rc = sdhci_transfer(&card->host, &command, buffer, BLOCK_SECTOR_SIZE, 1U, write);
    if (!rc && (command.response[0] & 0xFDFFE008U)) return -K_EIO;
    /* Never retry an ambiguous write; propagate the error to the filesystem. */
    return rc;
}

int sd_read_blocks(struct sd_card *card, uint64_t lba,
                   uint32_t count, void *buffer)
{
    uint32_t index;
    uint8_t *bytes = (uint8_t *)buffer;

    if (!card || !card->initialized || !buffer || !count ||
        lba >= card->block_count || count > card->block_count - lba) {
        return -K_EIO;
    }
    for (index = 0U; index < count; index++) {
        int rc = sd_transfer_one(card, lba + index, bytes + (size_t)index * BLOCK_SECTOR_SIZE, false);
        /* Reads are idempotent. Retry once after the host reset its lines. */
        if (rc) rc = sd_transfer_one(card, lba + index, bytes + (size_t)index * BLOCK_SECTOR_SIZE, false);
        if (rc) return rc;
    }
    return 0;
}

int sd_write_blocks(struct sd_card *card, uint64_t lba,
                    uint32_t count, const void *buffer)
{
    uint32_t index;
    const uint8_t *bytes = (const uint8_t *)buffer;

    if (!card || !card->initialized || !buffer || !count ||
        lba >= card->block_count || count > card->block_count - lba) {
        return -K_EIO;
    }
    for (index = 0U; index < count; index++) {
        int rc = sd_transfer_one(card, lba + index,
                                 (void *)(bytes + (size_t)index * BLOCK_SECTOR_SIZE), true);
        if (rc) return rc;
    }
    return 0;
}

static int sd_block_init(struct block_device *device)
{
    struct sd_card *card;

    if (device == NULL || device->private_data == NULL) {
        return -K_EIO;
    }
    card = (struct sd_card *)device->private_data;
    int rc = sd_card_init(card);
    if (rc) return rc;
    device->block_size = BLOCK_SECTOR_SIZE;
    device->block_count = card->block_count;
    return 0;
}

static int sd_block_read(struct block_device *device, uint64_t lba,
                         uint32_t count, void *buffer)
{
    return sd_read_blocks((struct sd_card *)device->private_data,
                          lba, count, buffer);
}

static int sd_block_write(struct block_device *device, uint64_t lba,
                          uint32_t count, const void *buffer)
{
    return sd_write_blocks((struct sd_card *)device->private_data,
                           lba, count, buffer);
}

void sd_block_setup(struct block_device *device, struct sd_card *card, const char *name)
{
    *device = (struct block_device){ .name = name, .block_size = BLOCK_SECTOR_SIZE,
        .ops = &sd_block_ops, .private_data = card };
}
