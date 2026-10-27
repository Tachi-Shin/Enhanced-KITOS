#include <block/block.h>
#include <fs/fat32.h>
#include <kernel/error.h>
#include <lib/unicode.h>
#include <lib/string.h>

#define FAT32_MAX_SECTOR_SIZE   4096U
#define FAT32_ENTRY_SIZE        32U
#define FAT32_LFN_ATTR          0x0FU
#define FAT32_DELETED           0xE5U
#define FAT32_EOC               0x0FFFFFFFU
#define FAT32_EOC_MIN           0x0FFFFFF8U
#define FAT32_BAD_CLUSTER       0x0FFFFFF7U
#define FAT32_MAX_LFN_UNITS     255U
#define FAT32_MAX_LFN_ENTRIES   20U

struct fat32_location {
    struct fat32_file_info info;
    uint64_t sector;
    uint16_t offset;
    uint8_t short_name[11];
    uint8_t lfn_count;
    uint64_t lfn_sector[FAT32_MAX_LFN_ENTRIES];
    uint16_t lfn_offset[FAT32_MAX_LFN_ENTRIES];
};

static bool fs_ready(const struct fat32_fs *fs)
{
    return fs && fs->mounted && fs->device && fs->device->initialized &&
        fs->bytes_per_sector >= 512U && fs->bytes_per_sector <= FAT32_MAX_SECTOR_SIZE &&
        !(fs->bytes_per_sector & (fs->bytes_per_sector - 1U)) && fs->blocks_per_sector &&
        fs->sectors_per_cluster && fs->sectors_per_cluster <= 128U;
}

static uint16_t get_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t get_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void put_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void bytes_zero(void *destination, size_t size)
{
    uint8_t *data = destination;
    size_t i;
    for (i = 0U; i < size; i++) {
        data[i] = 0U;
    }
}

static void bytes_copy(void *destination, const void *source, size_t size)
{
    uint8_t *to = destination;
    const uint8_t *from = source;
    size_t i;
    for (i = 0U; i < size; i++) {
        to[i] = from[i];
    }
}

static size_t string_length(const char *string)
{
    size_t length = 0U;
    while (string != NULL && string[length] != '\0') {
        length++;
    }
    return length;
}

static uint8_t ascii_upper(uint8_t value)
{
    if (value >= 'a' && value <= 'z') {
        return (uint8_t)(value - ('a' - 'A'));
    }
    return value;
}

static bool name_equal(const char *left, const char *right)
{
    uint32_t a, b;
    while (*left && *right) {
        if (utf8_next(&left, &a) != 1 || utf8_next(&right, &b) != 1 ||
            unicode_upper(a) != unicode_upper(b)) return false;
    }
    return !*left && !*right;
}

static int read_sector(struct fat32_fs *fs, uint64_t sector, uint8_t *buffer)
{
    if (sector >= fs->total_sectors) return -K_EIO;
    return block_read(fs->device, fs->volume_lba + sector * fs->blocks_per_sector,
                      fs->blocks_per_sector, buffer);
}

static int write_sector(struct fat32_fs *fs, uint64_t sector,
                        const uint8_t *buffer)
{
    if (sector >= fs->total_sectors) return -K_EIO;
    if (fs->write_failed) return -K_EROFS;
    int rc = block_write(fs->device, fs->volume_lba + sector * fs->blocks_per_sector,
                         fs->blocks_per_sector, buffer);
    if (rc) fs->write_failed = true; /* ambiguous media error: stop further mutation */
    return rc;
}

static bool valid_bpb(const uint8_t *sector)
{
    uint16_t bytes_per_sector = get_le16(sector + 11U);
    uint8_t sectors_per_cluster = sector[13U];
    uint16_t reserved = get_le16(sector + 14U);
    uint8_t fats = sector[16U];
    uint32_t fat_size = get_le32(sector + 36U);

    return sector[510U] == 0x55U && sector[511U] == 0xAAU &&
           (bytes_per_sector == 512U || bytes_per_sector == 1024U ||
            bytes_per_sector == 2048U || bytes_per_sector == 4096U) &&
           sectors_per_cluster != 0U && sectors_per_cluster <= 128U &&
           (sectors_per_cluster & (sectors_per_cluster - 1U)) == 0U &&
           reserved != 0U && fats != 0U && fat_size != 0U &&
           get_le16(sector + 17U) == 0U && get_le16(sector + 22U) == 0U;
}

static int finish_mount(struct fat32_fs *fs, struct block_device *device,
                        uint64_t volume_lba, const uint8_t *sector)
{
    uint32_t data_sectors;
    uint64_t first_data;

    bytes_zero(fs, sizeof(*fs));
    fs->device = device;
    fs->volume_lba = volume_lba;
    fs->bytes_per_sector = get_le16(sector + 11U);
    if (!device->block_size || fs->bytes_per_sector % device->block_size) return -K_EIO;
    fs->blocks_per_sector = fs->bytes_per_sector / device->block_size;
    fs->sectors_per_cluster = sector[13U];
    fs->fat_count = sector[16U];
    fs->total_sectors = get_le16(sector + 19U);
    if (fs->total_sectors == 0U) {
        fs->total_sectors = get_le32(sector + 32U);
    }
    fs->sectors_per_fat = get_le32(sector + 36U);
    fs->root_cluster = get_le32(sector + 44U) & 0x0FFFFFFFU;
    fs->fsinfo_sector = get_le16(sector + 48U);
    fs->backup_boot_sector = get_le16(sector + 50U);
    fs->first_fat_sector = get_le16(sector + 14U);
    first_data = fs->first_fat_sector + (uint64_t)fs->fat_count * fs->sectors_per_fat;
    if (first_data >= fs->total_sectors || get_le16(sector + 42U) != 0U) return -K_EIO;
    fs->first_data_sector = (uint32_t)first_data;
    fs->mirror_fats = (get_le16(sector + 40U) & 0x80U) == 0U;
    fs->active_fat = fs->mirror_fats ? 0U : (uint8_t)(get_le16(sector + 40U) & 15U);
    if (fs->active_fat >= fs->fat_count) return -K_EIO;
    if (fs->total_sectors <= fs->first_data_sector || fs->root_cluster < 2U) {
        return -K_EIO;
    }
    if (device->block_count != 0ULL &&
        (volume_lba >= device->block_count ||
         (uint64_t)fs->total_sectors * fs->blocks_per_sector > device->block_count - volume_lba)) {
        return -K_EIO;
    }
    data_sectors = fs->total_sectors - fs->first_data_sector;
    fs->cluster_count = data_sectors / fs->sectors_per_cluster;
    if (fs->cluster_count < 65525U || fs->cluster_count > 0x0FFFFFEEU ||
        fs->root_cluster >= fs->cluster_count + 2U ||
        (uint64_t)fs->sectors_per_fat * (fs->bytes_per_sector / 4U) < fs->cluster_count + 2U) {
        return -K_EIO;
    }
    fs->mounted = true;
    return 0;
}

int fat32_mount_at(struct fat32_fs *fs, struct block_device *device,
                   uint64_t volume_lba)
{
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];

    if (fs == NULL) {
        return -K_EIO;
    }
    bytes_zero(fs, sizeof(*fs));
    if (device == NULL || !device->initialized ||
        (device->block_size < 512U || device->block_size > FAT32_MAX_SECTOR_SIZE) ||
        (device->block_count != 0ULL && volume_lba >= device->block_count) ||
        block_read(device, volume_lba, 1U, sector) != 0 ||
        !valid_bpb(sector)) {
        return -K_EIO;
    }
    return finish_mount(fs, device, volume_lba, sector);
}

int fat32_mount(struct fat32_fs *fs, struct block_device *device)
{
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];
    if (!fs) return -K_EIO;
    bytes_zero(fs, sizeof(*fs));
    if (!device || !device->initialized || device->block_size < 512U ||
        device->block_size > FAT32_MAX_SECTOR_SIZE || block_read(device, 0, 1, sector)) return -K_EIO;
    if (valid_bpb(sector)) return finish_mount(fs, device, 0, sector);
    if (sector[510] != 0x55 || sector[511] != 0xAA) return -K_EIO;
    /* Existing KITOS media uses BOOT + DATA: prefer partition 2 when FAT32.
     * Otherwise use the first supported primary partition. Explicit mount_at
     * is available to callers which need a different partition. */
    const unsigned int order[4] = {1, 0, 2, 3};
    for (unsigned int i = 0; i < 4; ++i) {
        const uint8_t *entry = sector + 446 + order[i] * 16;
        if (entry[4] != 0x0B && entry[4] != 0x0C && entry[4] != 0x1B && entry[4] != 0x1C) continue;
        uint32_t start = get_le32(entry + 8), count = get_le32(entry + 12);
        if (!start || !count || start >= device->block_count || count > device->block_count - start) return -K_EIO;
        if (fat32_mount_at(fs, device, start)) return -K_EIO;
        if ((uint64_t)fs->total_sectors * fs->blocks_per_sector > count) { fs->mounted = false; return -K_EIO; }
        return 0;
    }
    return -K_EIO;
}

static uint64_t cluster_sector(const struct fat32_fs *fs, uint32_t cluster)
{
    return fs->first_data_sector +
           (uint64_t)(cluster - 2U) * fs->sectors_per_cluster;
}

static bool cluster_valid(const struct fat32_fs *fs, uint32_t cluster)
{
    return cluster >= 2U && cluster < fs->cluster_count + 2U;
}

static int fat_get(struct fat32_fs *fs, uint32_t cluster, uint32_t *value)
{
    if (!fs_ready(fs) || !value) return -K_EINVAL;
    const uint32_t bytes_per_sector = fs->bytes_per_sector;
    if (!bytes_per_sector) return -K_EINVAL;
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];
    uint32_t byte_offset = cluster * 4U;
    uint32_t fat_sector = fs->first_fat_sector +
                          (uint32_t)fs->active_fat * fs->sectors_per_fat + byte_offset / bytes_per_sector;
    uint32_t offset = byte_offset % bytes_per_sector;

    if (!cluster_valid(fs, cluster) || read_sector(fs, fat_sector, sector) != 0) {
        return -K_EIO;
    }
    *value = get_le32(sector + offset) & 0x0FFFFFFFU;
    return 0;
}

static int fat_set(struct fat32_fs *fs, uint32_t cluster, uint32_t value)
{
    if (!fs_ready(fs)) return -K_EINVAL;
    const uint32_t bytes_per_sector = fs->bytes_per_sector;
    if (!bytes_per_sector) return -K_EINVAL;
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];
    uint32_t byte_offset = cluster * 4U;
    uint32_t relative_sector = byte_offset / bytes_per_sector;
    uint32_t offset = byte_offset % bytes_per_sector;
    uint8_t fat;

    if (!cluster_valid(fs, cluster)) {
        return -K_EIO;
    }
    for (fat = 0U; fat < fs->fat_count; fat++) {
        uint32_t sector_number = fs->first_fat_sector +
            (uint32_t)fat * fs->sectors_per_fat + relative_sector;
        uint32_t old;
        if (!fs->mirror_fats && fat != fs->active_fat) continue;
        if (read_sector(fs, sector_number, sector) != 0) {
            return -K_EIO;
        }
        old = get_le32(sector + offset);
        put_le32(sector + offset, (old & 0xF0000000U) |
                 (value & 0x0FFFFFFFU));
        if (write_sector(fs, sector_number, sector) != 0) {
            return -K_EIO;
        }
    }
    return 0;
}

static bool fsinfo_valid(const uint8_t *sector)
{
    return get_le32(sector) == 0x41615252U &&
           get_le32(sector + 484U) == 0x61417272U &&
           get_le32(sector + 508U) == 0xAA550000U;
}

static int update_one_fsinfo(struct fat32_fs *fs, uint32_t sector_number,
                             int32_t free_delta, uint32_t next_free)
{
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];
    uint32_t free_count;

    if (sector_number == 0U || sector_number >= fs->first_fat_sector ||
        read_sector(fs, sector_number, sector) != 0 || !fsinfo_valid(sector)) {
        return 0;
    }
    free_count = get_le32(sector + 488U);
    if (free_count > fs->cluster_count) free_count = 0xFFFFFFFFU;
    if (free_count != 0xFFFFFFFFU) {
        if (free_delta < 0 && free_count >= (uint32_t)(-free_delta)) {
            free_count -= (uint32_t)(-free_delta);
        } else if (free_delta > 0 &&
                   free_count <= fs->cluster_count - (uint32_t)free_delta) {
            free_count += (uint32_t)free_delta;
        } else free_count = 0xFFFFFFFFU;
        put_le32(sector + 488U, free_count);
    }
    if (next_free >= 2U && next_free < fs->cluster_count + 2U) {
        put_le32(sector + 492U, next_free);
    }
    return write_sector(fs, sector_number, sector);
}

static int update_fsinfo(struct fat32_fs *fs, int32_t free_delta,
                         uint32_t next_free)
{
    uint32_t backup;
    if (update_one_fsinfo(fs, fs->fsinfo_sector, free_delta, next_free) != 0) {
        return -K_EIO;
    }
    backup = fs->backup_boot_sector + fs->fsinfo_sector;
    if (fs->backup_boot_sector != 0U && backup != fs->fsinfo_sector &&
        backup < fs->first_fat_sector &&
        update_one_fsinfo(fs, backup, free_delta, next_free) != 0) {
        return -K_EIO;
    }
    return 0;
}

static int zero_cluster(struct fat32_fs *fs, uint32_t cluster)
{
    uint8_t zero[FAT32_MAX_SECTOR_SIZE];
    uint8_t sector;
    bytes_zero(zero, sizeof(zero));
    for (sector = 0U; sector < fs->sectors_per_cluster; sector++) {
        if (write_sector(fs, cluster_sector(fs, cluster) + sector, zero) != 0) {
            return -K_EIO;
        }
    }
    return 0;
}

static int allocate_cluster(struct fat32_fs *fs, uint32_t *result)
{
    uint8_t info[FAT32_MAX_SECTOR_SIZE];
    uint32_t start = 2U;
    uint32_t cluster;
    uint32_t value;
    uint32_t cached = 0xFFFFFFFFU;
    uint8_t fat_sector[FAT32_MAX_SECTOR_SIZE];

    if (fs->fsinfo_sector != 0U &&
        read_sector(fs, fs->fsinfo_sector, info) == 0 && fsinfo_valid(info)) {
        uint32_t hint = get_le32(info + 492U);
        if (cluster_valid(fs, hint)) {
            start = hint;
        }
    }
    cluster = start;
    do {
        uint32_t number = cluster / (fs->bytes_per_sector / 4U);
        if (number != cached) {
            if (read_sector(fs, fs->first_fat_sector + fs->active_fat * fs->sectors_per_fat + number, fat_sector)) return -K_EIO;
            cached = number;
        }
        value = get_le32(fat_sector + (cluster * 4U) % fs->bytes_per_sector) & 0x0FFFFFFFU;
        if (value == 0U) {
            if (fat_set(fs, cluster, FAT32_EOC) != 0) return -K_EIO;
            if (zero_cluster(fs, cluster) != 0) {
                (void)fat_set(fs, cluster, 0U);
                return -K_EIO;
            }
            (void)update_fsinfo(fs, -1, cluster + 1U);
            *result = cluster;
            return 0;
        }
        cluster++;
        if (cluster >= fs->cluster_count + 2U) {
            cluster = 2U;
        }
    } while (cluster != start);
    return -K_ENOSPC;
}

static int chain_length(struct fat32_fs *, uint32_t, uint32_t *, uint32_t *);
static int free_chain(struct fat32_fs *fs, uint32_t first)
{
    uint32_t count, last, cluster = first;
    if (!first) return 0;
    if (chain_length(fs, first, &count, &last) || !count) return -K_EIO;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t next;
        if (fat_get(fs, cluster, &next) || fat_set(fs, cluster, 0)) return -K_EIO;
        cluster = next;
    }
    return update_fsinfo(fs, (int32_t)count, first);
}

static uint8_t short_checksum(const uint8_t name[11])
{
    uint8_t sum = 0U;
    unsigned int i;
    for (i = 0U; i < 11U; i++) {
        sum = (uint8_t)(((sum & 1U) << 7) + (sum >> 1) + name[i]);
    }
    return sum;
}

static void append_utf8(char *, size_t *, uint32_t);

static void decode_short_name(const uint8_t *entry, char *name)
{
    unsigned int i;
    size_t position = 0U;
    uint8_t nt = entry[12U];

    for (i = 0U; i < 8U && entry[i] != ' '; i++) {
        uint8_t c = entry[i];
        if (i == 0 && c == 0x05U) c = 0xE5U;
        if ((nt & 0x08U) != 0U && c >= 'A' && c <= 'Z') {
            c = (uint8_t)(c + ('a' - 'A'));
        }
        append_utf8(name, &position, unicode_cp437(c));
    }
    if (entry[8U] != ' ') {
        name[position++] = '.';
        for (i = 8U; i < 11U && entry[i] != ' '; i++) {
            uint8_t c = entry[i];
            if ((nt & 0x10U) != 0U && c >= 'A' && c <= 'Z') {
                c = (uint8_t)(c + ('a' - 'A'));
            }
            append_utf8(name, &position, unicode_cp437(c));
        }
    }
    name[position] = '\0';
}

static void lfn_extract(const uint8_t *entry, uint16_t *units, uint8_t ordinal)
{
    static const uint8_t offsets[13] = {
        1U, 3U, 5U, 7U, 9U, 14U, 16U, 18U, 20U, 22U, 24U, 28U, 30U
    };
    unsigned int i;
    uint32_t base = ((uint32_t)ordinal - 1U) * 13U;
    for (i = 0U; i < 13U && base + i < FAT32_MAX_LFN_UNITS; i++) {
        units[base + i] = get_le16(entry + offsets[i]);
    }
}

static void append_utf8(char *output, size_t *position, uint32_t codepoint)
{
    if (codepoint < 0x80U && *position + 1U < FAT32_MAX_NAME_UTF8) {
        output[(*position)++] = (char)codepoint;
    } else if (codepoint < 0x800U && *position + 2U < FAT32_MAX_NAME_UTF8) {
        output[(*position)++] = (char)(0xC0U | (codepoint >> 6));
        output[(*position)++] = (char)(0x80U | (codepoint & 0x3FU));
    } else if (codepoint < 0x10000U && *position + 3U < FAT32_MAX_NAME_UTF8) {
        output[(*position)++] = (char)(0xE0U | (codepoint >> 12));
        output[(*position)++] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
        output[(*position)++] = (char)(0x80U | (codepoint & 0x3FU));
    } else if (*position + 4U < FAT32_MAX_NAME_UTF8) {
        output[(*position)++] = (char)(0xF0U | (codepoint >> 18));
        output[(*position)++] = (char)(0x80U | ((codepoint >> 12) & 0x3FU));
        output[(*position)++] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
        output[(*position)++] = (char)(0x80U | (codepoint & 0x3FU));
    }
}

static bool lfn_to_utf8(const uint16_t *units, char *output)
{
    size_t position = 0U;
    unsigned int i;
    for (i = 0U; i < FAT32_MAX_LFN_UNITS; i++) {
        uint32_t codepoint = units[i];
        if (codepoint == 0U || codepoint == 0xFFFFU) {
            break;
        }
        if (codepoint >= 0xD800U && codepoint <= 0xDBFFU &&
            i + 1U < FAT32_MAX_LFN_UNITS && units[i + 1U] >= 0xDC00U &&
            units[i + 1U] <= 0xDFFFU) {
            codepoint = 0x10000U + ((codepoint - 0xD800U) << 10) +
                        (units[++i] - 0xDC00U);
        }
        if (codepoint >= 0xD800U && codepoint <= 0xDFFFU) return false;
        append_utf8(output, &position, codepoint);
    }
    output[position] = '\0';
    return true;
}

static void location_from_entry(struct fat32_location *location,
                                const uint8_t *entry, const char *name,
                                uint64_t sector, uint16_t offset)
{
    size_t length = string_length(name);
    if (length >= FAT32_MAX_NAME_UTF8) {
        length = FAT32_MAX_NAME_UTF8 - 1U;
    }
    bytes_copy(location->info.name, name, length);
    location->info.name[length] = '\0';
    location->info.number = sector * FAT32_MAX_SECTOR_SIZE + offset + 2U;
    location->info.attributes = entry[11U];
    location->info.first_cluster =
        ((uint32_t)(get_le16(entry + 20U) & 0x0FFFU) << 16) | get_le16(entry + 26U);
    location->info.size = get_le32(entry + 28U);
    location->info.create_tenths = entry[13U];
    location->info.create_time = get_le16(entry + 14U);
    location->info.create_date = get_le16(entry + 16U);
    location->info.access_date = get_le16(entry + 18U);
    location->info.modify_time = get_le16(entry + 22U);
    location->info.modify_date = get_le16(entry + 24U);
    location->sector = sector;
    location->offset = offset;
    bytes_copy(location->short_name, entry, 11U);
}

static int scan_directory(struct fat32_fs *fs, uint32_t directory,
                          const char *wanted, struct fat32_location *result,
                          fat32_list_callback callback, void *user)
{
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];
    uint16_t units[FAT32_MAX_LFN_UNITS];
    uint64_t pending_sector[FAT32_MAX_LFN_ENTRIES];
    uint16_t pending_offset[FAT32_MAX_LFN_ENTRIES];
    uint32_t cluster = directory;
    uint32_t next;
    uint32_t guard = 0U;
    uint8_t expected_checksum = 0U, expected_ordinal = 0U;
    uint8_t pending_count = 0U;
    bool lfn_valid = false;
    uint8_t s;
    uint16_t offset;

    bytes_zero(units, sizeof(units));
    while (cluster_valid(fs, cluster) && guard++ < fs->cluster_count) {
        for (s = 0U; s < fs->sectors_per_cluster; s++) {
            uint64_t sector_number = cluster_sector(fs, cluster) + s;
            if (read_sector(fs, sector_number, sector) != 0) {
                return -K_EIO;
            }
            for (offset = 0U; offset < fs->bytes_per_sector;
                 offset += FAT32_ENTRY_SIZE) {
                uint8_t *entry = sector + offset;
                if (entry[0] == 0U) {
                    return 1;
                }
                if (entry[0] == FAT32_DELETED) {
                    lfn_valid = false;
                    pending_count = 0U;
                    continue;
                }
                if (entry[11U] == FAT32_LFN_ATTR) {
                    uint8_t ordinal = entry[0] & 0x1FU;
                    if ((entry[0] & 0x40U) != 0U) {
                        bytes_zero(units, sizeof(units));
                        pending_count = 0U;
                        expected_checksum = entry[13U];
                        expected_ordinal = ordinal;
                        lfn_valid = ordinal != 0U &&
                                    ordinal <= FAT32_MAX_LFN_ENTRIES;
                    }
                    if (!lfn_valid || ordinal == 0U ||
                        ordinal > FAT32_MAX_LFN_ENTRIES ||
                        ordinal != expected_ordinal || entry[12U] != 0U ||
                        get_le16(entry + 26U) != 0U || entry[13U] != expected_checksum) {
                        lfn_valid = false;
                        pending_count = 0U;
                        continue;
                    }
                    lfn_extract(entry, units, ordinal);
                    --expected_ordinal;
                    if (pending_count < FAT32_MAX_LFN_ENTRIES) {
                        pending_sector[pending_count] = sector_number;
                        pending_offset[pending_count] = offset;
                        pending_count++;
                    }
                    continue;
                }
                if ((entry[11U] & FAT32_ATTR_VOLUME_ID) == 0U) {
                    char name[FAT32_MAX_NAME_UTF8];
                    if (lfn_valid && expected_ordinal == 0U && expected_checksum == short_checksum(entry)) {
                        if (!lfn_to_utf8(units, name)) {
                            decode_short_name(entry, name); pending_count = 0;
                        }
                    } else {
                        decode_short_name(entry, name);
                        pending_count = 0U;
                    }
                    char short_alias[FAT32_MAX_NAME_UTF8];
                    decode_short_name(entry, short_alias);
                    if (wanted == NULL || name_equal(name, wanted) || name_equal(short_alias, wanted)) {
                        uint8_t i;
                        location_from_entry(result, entry, name,
                                            sector_number, offset);
                        result->lfn_count = pending_count;
                        for (i = 0U; i < pending_count; i++) {
                            result->lfn_sector[i] = pending_sector[i];
                            result->lfn_offset[i] = pending_offset[i];
                        }
                        if (wanted != NULL) return 0;
                        if (!name_equal(name, ".") && !name_equal(name, "..") &&
                            callback(user, &result->info) != 0) return -K_EIO;
                    }
                }
                lfn_valid = false;
                pending_count = 0U;
            }
        }
        if (fat_get(fs, cluster, &next) != 0) {
            return -K_EIO;
        }
        if (next >= FAT32_EOC_MIN) {
            return 1;
        }
        if (!cluster_valid(fs, next) || next == FAT32_BAD_CLUSTER) {
            return -K_EIO;
        }
        cluster = next;
    }
    return -K_EIO;
}

static int find_in_directory(struct fat32_fs *fs, uint32_t directory,
                             const char *wanted, struct fat32_location *result)
{
    return scan_directory(fs, directory, wanted, result, NULL, NULL);
}

static int next_component(const char **path, char *component)
{
    const char *current = *path;
    size_t length = 0U;
    while (*current == '/') {
        current++;
    }
    while (*current != '\0' && *current != '/') {
        if (length + 1U >= FAT32_MAX_NAME_UTF8) {
            return -K_EIO;
        }
        component[length++] = *current++;
    }
    component[length] = '\0';
    while (*current == '/') {
        current++;
    }
    *path = current;
    return length == 0U ? 1 : 0;
}

static int lookup(struct fat32_fs *fs, const char *path,
                  struct fat32_location *location)
{
    char component[FAT32_MAX_NAME_UTF8];
    const char *current = path;
    uint32_t directory;
    int rc;
    if (!fs_ready(fs) || path == NULL || *path == '\0') return -K_EIO;
    directory = fs->root_cluster;
    bytes_zero(location, sizeof(*location));
    location->info.name[0] = '/';
    location->info.number = 1;
    location->info.attributes = FAT32_ATTR_DIRECTORY;
    location->info.first_cluster = directory;
    while ((rc = next_component(&current, component)) == 0) {
        if (name_equal(component, ".") ||
            (directory == fs->root_cluster && name_equal(component, ".."))) {
            if (*current == '\0') return 0;
            continue;
        }
        rc = find_in_directory(fs, directory, component, location);
        if (rc != 0) return rc;
        if ((location->info.attributes & FAT32_ATTR_DIRECTORY) != 0U &&
            location->info.first_cluster == 0U && name_equal(component, ".."))
            location->info.first_cluster = fs->root_cluster;
        if (*current == '\0') return 0;
        if (!(location->info.attributes & FAT32_ATTR_DIRECTORY) ||
            !cluster_valid(fs, location->info.first_cluster)) return -K_EIO;
        directory = location->info.first_cluster;
    }
    return rc < 0 ? -K_EIO : 0;
}

int fat32_stat(struct fat32_fs *fs, const char *path,
               struct fat32_file_info *info)
{
    struct fat32_location location;
    int result;
    if (info == NULL) {
        return -K_EIO;
    }
    result = lookup(fs, path, &location);
    if (result == 0) {
        bytes_copy(info, &location.info, sizeof(*info));
    }
    return result == 1 ? -K_ENOENT : result;
}

static int transfer_range(struct fat32_fs *fs, uint32_t first, uint32_t offset,
                           void *buffer, uint32_t size, bool write, bool zero,
                           uint32_t *done)
{
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];
    uint32_t cluster_bytes = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint32_t cluster = first, skip = offset / cluster_bytes, next;
    if (skip >= fs->cluster_count) return -K_EIO;
    for (uint32_t i = 0; i < skip; ++i) {
        if (fat_get(fs, cluster, &next) || !cluster_valid(fs, next)) return -K_EIO;
        cluster = next;
    }
    offset %= cluster_bytes;
    *done = 0;
    uint32_t guard = 0;
    while (*done < size) {
        if (!cluster_valid(fs, cluster) || guard++ >= fs->cluster_count) return -K_EIO;
        for (uint32_t s = offset / fs->bytes_per_sector;
             s < fs->sectors_per_cluster && *done < size; ++s) {
            uint32_t within = offset % fs->bytes_per_sector;
            uint32_t amount = fs->bytes_per_sector - within;
            if (amount > size - *done) amount = size - *done;
            uint64_t number = cluster_sector(fs, cluster) + s;
            if (read_sector(fs, number, sector)) return -K_EIO;
            if (write) {
                if (zero) bytes_zero(sector + within, amount);
                else bytes_copy(sector + within, (uint8_t *)buffer + *done, amount);
                if (write_sector(fs, number, sector)) return -K_EIO;
            } else bytes_copy((uint8_t *)buffer + *done, sector + within, amount);
            *done += amount;
            offset = 0;
        }
        if (*done < size) {
            if (fat_get(fs, cluster, &next) || !cluster_valid(fs, next)) return -K_EIO;
            cluster = next;
        }
    }
    return 0;
}
int fat32_pread(struct fat32_fs *fs, const char *path, uint32_t offset,
                void *buffer, uint32_t capacity, uint32_t *done)
{
    struct fat32_location location;
    if (!done || (!buffer && capacity)) return -K_EIO;
    *done = 0;
    if (lookup(fs, path, &location) || (location.info.attributes & FAT32_ATTR_DIRECTORY)) return -K_EIO;
    if (offset >= location.info.size || !capacity) return 0;
    if (capacity > location.info.size - offset) capacity = location.info.size - offset;
    return transfer_range(fs, location.info.first_cluster, offset, buffer, capacity, false, false, done);
}
int fat32_read_file(struct fat32_fs *fs, const char *path,
                    void *buffer, uint32_t capacity, uint32_t *done)
{ return fat32_pread(fs, path, 0, buffer, capacity, done); }

static int find_parent(struct fat32_fs *fs, const char *path,
                       uint32_t *parent, char *leaf)
{
    char component[FAT32_MAX_NAME_UTF8];
    struct fat32_location location;
    const char *current = path;
    uint32_t directory = fs->root_cluster;

    if (path == NULL) {
        return -K_EIO;
    }
    while (next_component(&current, component) == 0) {
        if (*current == '\0') {
            size_t length = string_length(component);
            bytes_copy(leaf, component, length + 1U);
            *parent = directory;
            return 0;
        }
        if (name_equal(component, ".") ||
            (directory == fs->root_cluster && name_equal(component, ".."))) continue;
        if (find_in_directory(fs, directory, component, &location) != 0 ||
            (location.info.attributes & FAT32_ATTR_DIRECTORY) == 0U) {
            return -K_EIO;
        }
        directory = location.info.first_cluster;
        if (directory == 0U && name_equal(component, "..")) directory = fs->root_cluster;
        if (!cluster_valid(fs, directory)) return -K_EIO;
    }
    return -K_EIO;
}

static int chain_length(struct fat32_fs *fs, uint32_t first,
                        uint32_t *length, uint32_t *last)
{
    uint32_t cluster = first;
    uint32_t next;
    uint32_t count = 0U, anchor = first, power = 1, distance = 0;
    if (first == 0U) {
        *length = 0U;
        *last = 0U;
        return 0;
    }
    if (!cluster_valid(fs, first)) return -K_EIO;
    while (count++ < fs->cluster_count) {
        if (fat_get(fs, cluster, &next) != 0) {
            return -K_EIO;
        }
        if (next >= FAT32_EOC_MIN) {
            *length = count;
            *last = cluster;
            return 0;
        }
        if (!cluster_valid(fs, next)) {
            return -K_EIO;
        }
        if (next == anchor) return -K_EIO;
        if (++distance == power) { anchor = next; distance = 0; power <<= 1; }
        cluster = next;
    }
    return -K_EIO;
}

static int resize_chain(struct fat32_fs *fs, uint32_t *first,
                        uint32_t required)
{
    uint32_t count;
    uint32_t last;
    uint32_t new_cluster;
    uint32_t next;
    uint32_t i;

    if (chain_length(fs, *first, &count, &last) != 0) {
        return -K_EIO;
    }
    if (required == 0U) {
        if (count != 0U && free_chain(fs, *first) != 0) {
            return -K_EIO;
        }
        *first = 0U;
        return 0;
    }
    while (count < required) {
        int rc = allocate_cluster(fs, &new_cluster);
        if (rc) return rc;
        if (count == 0U) {
            *first = new_cluster;
        } else if (fat_set(fs, last, new_cluster) != 0) {
            /* Error state prevents reusing a possibly published cluster. */
            return -K_EIO;
        }
        last = new_cluster;
        count++;
    }
    if (count > required) {
        last = *first;
        for (i = 1U; i < required; i++) {
            if (fat_get(fs, last, &next) != 0 || !cluster_valid(fs, next)) {
                return -K_EIO;
            }
            last = next;
        }
        if (fat_get(fs, last, &next) != 0 || fat_set(fs, last, FAT32_EOC) != 0 ||
            free_chain(fs, next) != 0) {
            return -K_EIO;
        }
    }
    return 0;
}

static int write_chain_data(struct fat32_fs *fs, uint32_t first,
                            const uint8_t *data, uint32_t size)
{
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];
    uint32_t cluster = first;
    uint32_t next;
    uint32_t position = 0U;
    uint8_t s;
    while (position < size) {
        if (!cluster_valid(fs, cluster)) {
            return -K_EIO;
        }
        for (s = 0U; s < fs->sectors_per_cluster && position < size; s++) {
            uint32_t amount = size - position;
            if (amount > fs->bytes_per_sector) {
                amount = fs->bytes_per_sector;
            }
            bytes_zero(sector, sizeof(sector));
            bytes_copy(sector, data + position, amount);
            if (write_sector(fs, cluster_sector(fs, cluster) + s, sector) != 0) {
                return -K_EIO;
            }
            position += amount;
        }
        if (position < size) {
            if (fat_get(fs, cluster, &next) != 0 || !cluster_valid(fs, next)) {
                return -K_EIO;
            }
            cluster = next;
        }
    }
    return 0;
}

static bool short_character(uint8_t c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return true;
    }
    switch (c) {
    case '$': case '%': case '\'': case '-': case '_': case '@': case '~':
    case '`': case '!': case '(': case ')': case '{': case '}': case '^':
    case '#': case '&':
        return true;
    default:
        return false;
    }
}

static bool direct_short_name(const char *name, uint8_t output[11])
{
    size_t length = string_length(name);
    size_t dot = length;
    size_t i;
    for (i = 0U; i < length; i++) {
        if (name[i] == '.') {
            if (dot != length) {
                return false;
            }
            dot = i;
        }
    }
    if (dot == 0U || dot > 8U ||
        (dot != length && (length - dot - 1U > 3U || dot + 1U == length))) {
        return false;
    }
    for (i = 0U; i < 11U; i++) {
        output[i] = ' ';
    }
    for (i = 0U; i < dot; i++) {
        uint8_t c = ascii_upper((uint8_t)name[i]);
        if (!short_character(c)) {
            return false;
        }
        output[i] = c;
    }
    if (dot != length) {
        for (i = dot + 1U; i < length; i++) {
            uint8_t c = ascii_upper((uint8_t)name[i]);
            if (!short_character(c)) {
                return false;
            }
            output[8U + i - dot - 1U] = c;
        }
    }
    return true;
}

static int short_exists(struct fat32_fs *fs, uint32_t directory,
                        const uint8_t name[11])
{
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];
    uint32_t cluster = directory;
    uint32_t next;
    uint32_t guard = 0U;
    uint8_t s;
    uint16_t offset;
    while (cluster_valid(fs, cluster) && guard++ <= fs->cluster_count) {
        for (s = 0U; s < fs->sectors_per_cluster; s++) {
            if (read_sector(fs, cluster_sector(fs, cluster) + s, sector) != 0) {
                return -K_EIO;
            }
            for (offset = 0U; offset < fs->bytes_per_sector;
                 offset += FAT32_ENTRY_SIZE) {
                unsigned int i;
                bool same = true;
                if (sector[offset] == 0U) {
                    return 0;
                }
                if (sector[offset] == FAT32_DELETED ||
                    sector[offset + 11U] == FAT32_LFN_ATTR) {
                    continue;
                }
                for (i = 0U; i < 11U; i++) {
                    if (sector[offset + i] != name[i]) {
                        same = false;
                    }
                }
                if (same) {
                    return 1;
                }
            }
        }
        if (fat_get(fs, cluster, &next) != 0) {
            return -K_EIO;
        }
        if (next >= FAT32_EOC_MIN) {
            return 0;
        }
        cluster = next;
    }
    return -K_EIO;
}

static int make_short_alias(struct fat32_fs *fs, uint32_t directory,
                            const char *name, uint8_t output[11],
                            bool *needs_lfn)
{
    size_t length = string_length(name);
    size_t dot = length;
    size_t i;
    unsigned int serial;

    if (direct_short_name(name, output)) {
        *needs_lfn = false;
        for (i = 0U; i < length; i++) {
            if ((uint8_t)name[i] != ascii_upper((uint8_t)name[i])) {
                *needs_lfn = true;
            }
        }
        int exists = short_exists(fs, directory, output);
        if (!exists) return 0;
        if (exists < 0) return exists;
        /* A case-only rename may need a fresh alias until the old one is removed. */
    }
    *needs_lfn = true;
    for (i = 0U; i < length; i++) {
        if (name[i] == '.') {
            dot = i;
        }
    }
    for (serial = 1U; serial <= 999U; serial++) {
        char digits[3];
        unsigned int digit_count = 0U;
        unsigned int value = serial;
        size_t base_limit;
        size_t position = 0U;
        for (i = 0U; i < 11U; i++) {
            output[i] = ' ';
        }
        do {
            digits[digit_count++] = (char)('0' + value % 10U);
            value /= 10U;
        } while (value != 0U);
        base_limit = 8U - 1U - digit_count;
        for (i = 0U; i < dot && position < base_limit; i++) {
            uint8_t c = ascii_upper((uint8_t)name[i]);
            if (short_character(c)) {
                output[position++] = c;
            }
        }
        if (position == 0U) {
            output[position++] = '_';
        }
        output[position++] = '~';
        while (digit_count != 0U) {
            output[position++] = (uint8_t)digits[--digit_count];
        }
        if (dot < length) {
            position = 8U;
            for (i = dot + 1U; i < length && position < 11U; i++) {
                uint8_t c = ascii_upper((uint8_t)name[i]);
                if (short_character(c)) {
                    output[position++] = c;
                }
            }
        }
        if (short_exists(fs, directory, output) == 0) {
            return 0;
        }
    }
    return -K_EIO;
}

static int utf8_to_utf16(const char *name, uint16_t *units, uint16_t *count)
{
    const uint8_t *p = (const uint8_t *)name;
    uint16_t n = 0U;
    while (*p) {
        uint32_t cp, minimum;
        unsigned int more;
        uint8_t c = *p++;
        if (c < 0x80U) { cp = c; more = 0U; minimum = 0U; }
        else if (c >= 0xC2U && c <= 0xDFU) { cp = c & 31U; more = 1U; minimum = 0x80U; }
        else if (c >= 0xE0U && c <= 0xEFU) { cp = c & 15U; more = 2U; minimum = 0x800U; }
        else if (c >= 0xF0U && c <= 0xF4U) { cp = c & 7U; more = 3U; minimum = 0x10000U; }
        else return -K_EIO;
        while (more--) {
            if ((*p & 0xC0U) != 0x80U) return -K_EIO;
            cp = (cp << 6) | (*p++ & 63U);
        }
        if (cp < minimum || cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)) return -K_EIO;
        if (cp < 0x10000U) {
            if (n >= FAT32_MAX_LFN_UNITS) return -K_EIO;
            units[n++] = (uint16_t)cp;
        } else {
            if (n + 1U >= FAT32_MAX_LFN_UNITS) return -K_EIO;
            cp -= 0x10000U;
            units[n++] = (uint16_t)(0xD800U | (cp >> 10));
            units[n++] = (uint16_t)(0xDC00U | (cp & 1023U));
        }
    }
    *count = n;
    return n ? 0 : -K_EIO;
}

static bool valid_leaf(const char *name)
{
    uint16_t units[FAT32_MAX_LFN_UNITS], count;
    size_t n = string_length(name), i;
    if (!n || name[n - 1U] == '.' || name[n - 1U] == ' ') return false;
    for (i = 0U; i < n; ++i) {
        uint8_t c = (uint8_t)name[i];
        if (c < 32U || c == 127U || c == '"' || c == '*' || c == '/' ||
            c == ':' || c == '<' || c == '>' || c == '?' || c == '\\' || c == '|') return false;
    }
    return utf8_to_utf16(name, units, &count) == 0;
}

struct entry_slot { uint64_t sector; uint16_t offset; };

static int store_slot(struct fat32_fs *fs, const struct entry_slot *slot,
                      const uint8_t entry[FAT32_ENTRY_SIZE])
{
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];
    if (read_sector(fs, slot->sector, sector) != 0) return -K_EIO;
    bytes_copy(sector + slot->offset, entry, FAT32_ENTRY_SIZE);
    return write_sector(fs, slot->sector, sector);
}

static int find_slots(struct fat32_fs *fs, uint32_t directory, uint8_t needed,
                      struct entry_slot *slots)
{
    uint8_t sector[FAT32_MAX_SECTOR_SIZE], empty[FAT32_ENTRY_SIZE];
    uint32_t cluster = directory, guard = 0U, next;
    uint8_t run = 0U, s;
    uint16_t offset;
    bool ended = false;
    bytes_zero(empty, sizeof(empty));
    while (cluster_valid(fs, cluster) && guard++ < fs->cluster_count) {
        for (s = 0U; s < fs->sectors_per_cluster; ++s) {
            uint64_t number = cluster_sector(fs, cluster) + s;
            if (read_sector(fs, number, sector) != 0) return -K_EIO;
            for (offset = 0U; offset < fs->bytes_per_sector; offset += FAT32_ENTRY_SIZE) {
                if (run == needed) {
                    struct entry_slot end;
                    end.sector = number; end.offset = offset;
                    return store_slot(fs, &end, empty);
                }
                if (sector[offset] == 0U) ended = true;
                if (ended || sector[offset] == FAT32_DELETED) {
                    slots[run].sector = number; slots[run].offset = offset;
                    if (++run == needed && !ended) return 0;
                } else run = 0U;
            }
        }
        if (fat_get(fs, cluster, &next) != 0) return -K_EIO;
        if (next >= FAT32_EOC_MIN) {
            if (run == needed) return 0; /* Chain end is also a directory end. */
            if (allocate_cluster(fs, &next) != 0 || fat_set(fs, cluster, next) != 0) return -K_EIO;
        }
        if (!cluster_valid(fs, next)) return -K_EIO;
        cluster = next;
    }
    return -K_EIO;
}

static void write_lfn_unit(uint8_t *entry, unsigned int index, uint16_t value)
{
    static const uint8_t offsets[13] = {
        1U, 3U, 5U, 7U, 9U, 14U, 16U, 18U, 20U, 22U, 24U, 28U, 30U
    };
    put_le16(entry + offsets[index], value);
}

static int create_entry(struct fat32_fs *fs, uint32_t directory,
                        const char *name, uint32_t first_cluster,
                        uint32_t size, uint8_t attributes,
                        struct fat32_location *location)
{
    uint8_t short_name[11], entry[FAT32_ENTRY_SIZE];
    uint16_t units[FAT32_MAX_LFN_UNITS], unit_count = 0U;
    struct entry_slot slots[FAT32_MAX_LFN_ENTRIES + 1U];
    uint8_t count, i;
    bool needs_lfn;
    if (!valid_leaf(name) || make_short_alias(fs, directory, name, short_name, &needs_lfn) != 0) return -K_EIO;
    if (needs_lfn && utf8_to_utf16(name, units, &unit_count) != 0) return -K_EIO;
    count = needs_lfn ? (uint8_t)((unit_count + 12U) / 13U) : 0U;
    if (find_slots(fs, directory, (uint8_t)(count + 1U), slots) != 0) return -K_EIO;
    for (i = 0U; i < count; ++i) {
        uint8_t ordinal = (uint8_t)(count - i);
        unsigned int j;
        bytes_zero(entry, sizeof(entry));
        entry[0] = ordinal | (i == 0U ? 0x40U : 0U);
        entry[11U] = FAT32_LFN_ATTR;
        entry[13U] = short_checksum(short_name);
        for (j = 0U; j < 13U; ++j) {
            uint32_t index = ((uint32_t)ordinal - 1U) * 13U + j;
            write_lfn_unit(entry, j, index < unit_count ? units[index] : index == unit_count ? 0U : 0xFFFFU);
        }
        if (store_slot(fs, &slots[i], entry) != 0) return -K_EIO;
    }
    /* The short entry publishes the file, after all long-name entries. */
    bytes_zero(entry, sizeof(entry));
    bytes_copy(entry, short_name, 11U);
    entry[11U] = attributes;
    /* Valid FAT epoch until a wall clock is supplied by the caller. */
    put_le16(entry + 16U, 0x21U);
    put_le16(entry + 18U, 0x21U);
    put_le16(entry + 24U, 0x21U);
    put_le16(entry + 20U, (uint16_t)(first_cluster >> 16));
    put_le16(entry + 26U, (uint16_t)first_cluster);
    put_le32(entry + 28U, size);
    location_from_entry(location, entry, name, slots[count].sector, slots[count].offset);
    location->lfn_count = count;
    for (i = 0; i < count; ++i) {
        location->lfn_sector[i] = slots[i].sector;
        location->lfn_offset[i] = slots[i].offset;
    }
    return store_slot(fs, &slots[count], entry);
}

static int update_entry(struct fat32_fs *fs, struct fat32_location *location,
                        uint32_t first_cluster, uint32_t size)
{
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];
    uint8_t *entry;
    if (read_sector(fs, location->sector, sector) != 0) return -K_EIO;
    entry = sector + location->offset;
    put_le16(entry + 20U, (uint16_t)(first_cluster >> 16));
    put_le16(entry + 26U, (uint16_t)first_cluster);
    put_le32(entry + 28U, size);
    return write_sector(fs, location->sector, sector);
}

int fat32_write_file(struct fat32_fs *fs, const char *path,
                     const void *data, uint32_t size)
{
    struct fat32_location location;
    char leaf[FAT32_MAX_NAME_UTF8];
    uint32_t parent, first = 0U, old = 0U, cluster_bytes, required;
    int found, rc;
    if (!fs_ready(fs) || (data == NULL && size != 0U) ||
        find_parent(fs, path, &parent, leaf) != 0 || !valid_leaf(leaf)) return -K_EIO;
    found = find_in_directory(fs, parent, leaf, &location);
    if (found < 0 || (found == 0 && (location.info.attributes &
        (FAT32_ATTR_DIRECTORY | FAT32_ATTR_READ_ONLY)) != 0U)) return -K_EIO;
    if (found == 0) old = location.info.first_cluster;
    cluster_bytes = (uint32_t)fs->sectors_per_cluster * fs->bytes_per_sector;
    required = size / cluster_bytes + (size % cluster_bytes != 0U);
    /* Keep the old contents until allocation and data writes succeed. */
    rc = resize_chain(fs, &first, required);
    if (rc || (size != 0U && write_chain_data(fs, first, data, size) != 0)) {
        if (first != 0U) (void)free_chain(fs, first);
        return rc ? rc : -K_EIO;
    }
    rc = found == 0 ? update_entry(fs, &location, first, size)
                   : create_entry(fs, parent, leaf, first, size, FAT32_ATTR_ARCHIVE, &location);
    /* A failed metadata write may have reached the medium: do not free a
       possibly published chain. FAT32 is not a crash-atomic filesystem. */
    if (rc != 0) return -K_EIO;
    if (old != 0U && free_chain(fs, old) != 0) return -K_EIO;
    return 0;
}

static int delete_location(struct fat32_fs *fs, const struct fat32_location *loc)
{
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];
    if (read_sector(fs, loc->sector, sector)) return -K_EIO;
    sector[loc->offset] = FAT32_DELETED;
    if (write_sector(fs, loc->sector, sector)) return -K_EIO;
    for (uint8_t i = 0; i < loc->lfn_count; ++i) {
        if (read_sector(fs, loc->lfn_sector[i], sector)) return -K_EIO;
        sector[loc->lfn_offset[i]] = FAT32_DELETED;
        if (write_sector(fs, loc->lfn_sector[i], sector)) return -K_EIO;
    }
    return 0;
}

int fat32_remove_file(struct fat32_fs *fs, const char *path)
{
    struct fat32_location location;
    int rc = lookup(fs, path, &location);
    if (rc) return rc == 1 ? -K_ENOENT : rc;
    if (location.info.attributes & FAT32_ATTR_DIRECTORY) return -K_EISDIR;
    if (location.info.attributes & FAT32_ATTR_READ_ONLY) return -K_EROFS;
    uint32_t count, last;
    if (chain_length(fs, location.info.first_cluster, &count, &last)) return -K_EIO;
    if (delete_location(fs, &location)) return -K_EIO;
    return free_chain(fs, location.info.first_cluster);
}

int fat32_list_dir(struct fat32_fs *fs, const char *path,
                   fat32_list_callback callback, void *user)
{
    struct fat32_location location;
    int rc;
    if (!callback || lookup(fs, path, &location) != 0 ||
        !(location.info.attributes & FAT32_ATTR_DIRECTORY)) return -K_EIO;
    rc = scan_directory(fs, location.info.first_cluster, NULL, &location, callback, user);
    return rc == 1 ? 0 : -K_EIO;
}

int fat32_mkdir(struct fat32_fs *fs, const char *path)
{
    char leaf[FAT32_MAX_NAME_UTF8];
    struct fat32_location location;
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];
    uint32_t parent, first, up;
    unsigned int i;
    if (!fs_ready(fs) || find_parent(fs, path, &parent, leaf) != 0 ||
        !valid_leaf(leaf) || find_in_directory(fs, parent, leaf, &location) != 1) return -K_EIO;
    if (allocate_cluster(fs, &first) != 0) return -K_EIO;
    bytes_zero(sector, sizeof(sector));
    for (i = 0U; i < 11U; ++i) sector[i] = sector[32U + i] = ' ';
    sector[0] = '.';
    sector[32U] = sector[33U] = '.';
    sector[11U] = sector[43U] = FAT32_ATTR_DIRECTORY;
    put_le16(sector + 20U, (uint16_t)(first >> 16));
    put_le16(sector + 26U, (uint16_t)first);
    up = parent == fs->root_cluster ? 0U : parent;
    put_le16(sector + 52U, (uint16_t)(up >> 16));
    put_le16(sector + 58U, (uint16_t)up);
    if (write_sector(fs, cluster_sector(fs, first), sector) != 0) {
        (void)free_chain(fs, first);
        return -K_EIO;
    }
    return create_entry(fs, parent, leaf, first, 0U, FAT32_ATTR_DIRECTORY, &location);
}

int fat32_touch(struct fat32_fs *fs, const char *path)
{
    struct fat32_file_info info;
    int rc = fat32_stat(fs, path, &info);
    if (rc == 0) return (info.attributes & FAT32_ATTR_DIRECTORY) ? -K_EIO : 0;
    return rc == -K_ENOENT ? fat32_write_file(fs, path, NULL, 0U) : rc;
}

static int grow_file(struct fat32_fs *fs, struct fat32_location *loc, uint32_t size,
                     uint32_t *first)
{
    uint32_t bytes = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint32_t have, last, extra = 0;
    uint32_t need = size / bytes + (size % bytes != 0);
    *first = loc->info.first_cluster;
    if (chain_length(fs, *first, &have, &last)) return -K_EIO;
    if (need <= have) return 0;
    /* Build growth off-chain so ENOSPC leaves the existing chain untouched. */
    int rc = resize_chain(fs, &extra, need - have);
    if (rc) {
        if (extra) (void)free_chain(fs, extra);
        return rc;
    }
    if (have) {
        if (fat_set(fs, last, extra)) return -K_EIO;
    } else *first = extra;
    return 0;
}
static void rollback_growth(struct fat32_fs *fs, struct fat32_location *loc, uint32_t first)
{
    if (fs->write_failed) return;
    uint32_t bytes = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint32_t keep = loc->info.size / bytes + (loc->info.size % bytes != 0);
    (void)resize_chain(fs, &first, keep);
}
int fat32_pwrite(struct fat32_fs *fs, const char *path, uint32_t offset,
                 const void *buffer, uint32_t size, uint32_t *done)
{
    struct fat32_location loc;
    uint32_t first, cleared;
    if (!done || (!buffer && size) || size > 0xFFFFFFFFU - offset) return -K_EIO;
    *done = 0;
    if (lookup(fs, path, &loc) || (loc.info.attributes &
        (FAT32_ATTR_DIRECTORY | FAT32_ATTR_READ_ONLY))) return -K_EIO;
    if (!size) return 0;
    uint32_t end = offset + size;
    uint32_t new_size = end > loc.info.size ? end : loc.info.size;
    int rc = grow_file(fs, &loc, new_size, &first);
    if (rc) return rc;
    if (offset > loc.info.size && transfer_range(fs, first, loc.info.size, NULL,
        offset - loc.info.size, true, true, &cleared)) {
        rollback_growth(fs, &loc, first); return -K_EIO;
    }
    if (transfer_range(fs, first, offset, (void *)buffer, size, true, false, done)) {
        rollback_growth(fs, &loc, first); return -K_EIO;
    }
    return update_entry(fs, &loc, first, new_size);
}
int fat32_truncate(struct fat32_fs *fs, const char *path, uint32_t size)
{
    struct fat32_location loc;
    uint32_t first, done;
    if (lookup(fs, path, &loc) || (loc.info.attributes &
        (FAT32_ATTR_DIRECTORY | FAT32_ATTR_READ_ONLY))) return -K_EIO;
    if (size == loc.info.size) return 0;
    uint32_t existing_count, existing_last;
    if (chain_length(fs, loc.info.first_cluster, &existing_count, &existing_last)) return -K_EIO;
    first = loc.info.first_cluster;
    if (size > loc.info.size) {
        int rc = grow_file(fs, &loc, size, &first);
        if (rc) return rc;
        if (transfer_range(fs, first, loc.info.size, NULL, size - loc.info.size, true, true, &done)) {
            rollback_growth(fs, &loc, first); return -K_EIO;
        }
        return update_entry(fs, &loc, first, size);
    }
    uint32_t cluster_bytes = fs->bytes_per_sector * fs->sectors_per_cluster;
    /* Publish the shorter size before freeing anything it used to reference. */
    if (update_entry(fs, &loc, size ? first : 0, size)) return -K_EIO;
    return resize_chain(fs, &first, size / cluster_bytes + (size % cluster_bytes != 0));
}
static int count_entry(void *user, const struct fat32_file_info *info)
{ (void)info; *(bool *)user = true; return 1; }
int fat32_rmdir(struct fat32_fs *fs, const char *path)
{
    struct fat32_location loc;
    char leaf[FAT32_MAX_NAME_UTF8]; uint32_t parent;
    bool nonempty = false;
    if (!fs_ready(fs) || find_parent(fs, path, &parent, leaf) || !valid_leaf(leaf) ||
        lookup(fs, path, &loc) || !(loc.info.attributes & FAT32_ATTR_DIRECTORY) ||
        loc.info.first_cluster == fs->root_cluster || (loc.info.attributes & FAT32_ATTR_READ_ONLY)) return -K_EIO;
    int rc = fat32_list_dir(fs, path, count_entry, &nonempty);
    if (nonempty) return -K_ENOTEMPTY;
    if (rc || delete_location(fs, &loc)) return -K_EIO;
    return free_chain(fs, loc.info.first_cluster);
}
int fat32_rename(struct fat32_fs *fs, const char *old_path, const char *new_path)
{
    struct fat32_location old, dest, up;
    char leaf[FAT32_MAX_NAME_UTF8]; uint32_t parent, old_parent;
    uint8_t metadata[32], sector[FAT32_MAX_SECTOR_SIZE];
    uint32_t replaced_cluster = 0;
    bool replace = false;
    if (!fs_ready(fs) || find_parent(fs, old_path, &old_parent, leaf) || !valid_leaf(leaf) ||
        lookup(fs, old_path, &old) || (old.info.attributes & FAT32_ATTR_READ_ONLY) ||
        find_parent(fs, new_path, &parent, leaf) || !valid_leaf(leaf)) return -K_EINVAL;
    int found = find_in_directory(fs, parent, leaf, &dest);
    if (found < 0) return found;
    if (!found && dest.sector == old.sector && dest.offset == old.offset) {
        /* Same entry and spelling is a no-op; otherwise publish the new casing. */
        if (old_parent == parent && !strcmp(old.info.name, leaf)) return 0;
    } else if (!found) {
        if (dest.info.attributes & FAT32_ATTR_READ_ONLY) return -K_EROFS;
        bool old_dir = (old.info.attributes & FAT32_ATTR_DIRECTORY) != 0;
        bool new_dir = (dest.info.attributes & FAT32_ATTR_DIRECTORY) != 0;
        if (old_dir != new_dir) return old_dir ? -K_ENOTDIR : -K_EISDIR;
        if (new_dir) {
            bool nonempty = false;
            int rc = fat32_list_dir(fs, new_path, count_entry, &nonempty);
            if (nonempty) return -K_ENOTEMPTY;
            if (rc) return rc;
        }
        replaced_cluster = dest.info.first_cluster;
        uint32_t count, last;
        if (replaced_cluster && replaced_cluster == old.info.first_cluster) return -K_EIO;
        if (chain_length(fs, replaced_cluster, &count, &last)) return -K_EIO;
        replace = true;
    }
    if (old.info.attributes & FAT32_ATTR_DIRECTORY) {
        uint32_t ancestor = parent, guard = 0;
        while (ancestor != fs->root_cluster) {
            if (ancestor == old.info.first_cluster || guard++ >= fs->cluster_count ||
                find_in_directory(fs, ancestor, "..", &up)) return -K_EINVAL;
            ancestor = up.info.first_cluster;
            if (!ancestor) ancestor = fs->root_cluster;
            if (!cluster_valid(fs, ancestor)) return -K_EIO;
        }
    }
    if (read_sector(fs, old.sector, sector)) return -K_EIO;
    bytes_copy(metadata, sector + old.offset, 32);
    if (!replace && create_entry(fs, parent, leaf, old.info.first_cluster, old.info.size,
                                  old.info.attributes, &dest)) return -K_EIO;
    if (read_sector(fs, dest.sector, sector)) return -K_EIO;
    /* Keep the target name/NT casing, replace the file metadata and data chain. */
    sector[dest.offset + 11] = metadata[11];
    bytes_copy(sector + dest.offset + 13, metadata + 13, 19);
    if (write_sector(fs, dest.sector, sector)) return -K_EIO;
    if ((old.info.attributes & FAT32_ATTR_DIRECTORY) && parent != old_parent) {
        if (find_in_directory(fs, old.info.first_cluster, "..", &up) ||
            update_entry(fs, &up, parent == fs->root_cluster ? 0 : parent, 0)) return -K_EIO;
    }
    /* A power loss may leave both names. Never free a possibly linked chain. */
    if (delete_location(fs, &old)) return -K_EIO;
    return free_chain(fs, replaced_cluster);
}
int fat32_set_attributes(struct fat32_fs *fs, const char *path, uint8_t attributes)
{
    struct fat32_location loc;
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];
    if (attributes & ~(FAT32_ATTR_READ_ONLY | FAT32_ATTR_HIDDEN | FAT32_ATTR_SYSTEM | FAT32_ATTR_ARCHIVE)) return -K_EIO;
    if (lookup(fs, path, &loc) || !loc.sector || read_sector(fs, loc.sector, sector)) return -K_EIO;
    sector[loc.offset + 11] = attributes | (loc.info.attributes & FAT32_ATTR_DIRECTORY);
    return write_sector(fs, loc.sector, sector);
}
static bool valid_date(uint16_t date)
{
    unsigned int month = (date >> 5) & 15U, day = date & 31U;
    unsigned int year = 1980U + (date >> 9);
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (!month || month > 12 || !day) return false;
    unsigned int limit = days[month - 1];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ++limit;
    return day <= limit;
}
static bool valid_time(uint16_t time)
{ return (time >> 11) < 24 && ((time >> 5) & 63) < 60 && (time & 31) < 30; }
int fat32_set_times(struct fat32_fs *fs, const char *path, const struct fat32_file_info *info)
{
    struct fat32_location loc;
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];
    if (!info || !valid_date(info->create_date) || !valid_date(info->modify_date) ||
        !valid_date(info->access_date) || !valid_time(info->create_time) ||
        !valid_time(info->modify_time) || info->create_tenths > 199 ||
        lookup(fs, path, &loc) || !loc.sector || (loc.info.attributes & FAT32_ATTR_READ_ONLY) ||
        read_sector(fs, loc.sector, sector)) return -K_EIO;
    uint8_t *entry = sector + loc.offset;
    entry[13] = info->create_tenths;
    put_le16(entry + 14, info->create_time); put_le16(entry + 16, info->create_date);
    put_le16(entry + 18, info->access_date); put_le16(entry + 22, info->modify_time);
    put_le16(entry + 24, info->modify_date);
    return write_sector(fs, loc.sector, sector);
}
int fat32_free_space(struct fat32_fs *fs, uint64_t *bytes)
{
    uint8_t sector[FAT32_MAX_SECTOR_SIZE];
    if (!fs_ready(fs) || !bytes) return -K_EIO;
    uint64_t count = 0;
    uint32_t entries = fs->bytes_per_sector / 4U;
    for (uint32_t cluster = 2; cluster < fs->cluster_count + 2; ++cluster) {
        if (cluster == 2 || cluster % entries == 0) {
            if (read_sector(fs, fs->first_fat_sector + fs->active_fat * fs->sectors_per_fat +
                            cluster / entries, sector)) return -K_EIO;
        }
        if (!(get_le32(sector + (cluster % entries) * 4U) & 0x0FFFFFFFU)) ++count;
    }
    *bytes = count * fs->bytes_per_sector * fs->sectors_per_cluster;
    return 0;
}
