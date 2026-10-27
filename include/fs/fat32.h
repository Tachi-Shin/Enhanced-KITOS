#ifndef DOS_FS_FAT32_H
#define DOS_FS_FAT32_H

#include <dos/type.h>

struct block_device;

#define FAT32_MAX_NAME_UTF8 768U

#define FAT32_ATTR_READ_ONLY 0x01U
#define FAT32_ATTR_HIDDEN    0x02U
#define FAT32_ATTR_SYSTEM    0x04U
#define FAT32_ATTR_VOLUME_ID 0x08U
#define FAT32_ATTR_DIRECTORY 0x10U
#define FAT32_ATTR_ARCHIVE   0x20U

struct fat32_file_info {
    char name[FAT32_MAX_NAME_UTF8];
    uint64_t number; /* Stable directory-entry identity while linked. */
    uint8_t attributes;
    uint32_t first_cluster;
    uint32_t size;
    uint16_t create_date, create_time, access_date, modify_date, modify_time;
    uint8_t create_tenths;
};

struct fat32_fs {
    struct block_device *device;
    uint64_t volume_lba;
    uint32_t total_sectors;
    uint32_t sectors_per_fat;
    uint32_t first_fat_sector;
    uint32_t first_data_sector;
    uint32_t root_cluster;
    uint32_t fsinfo_sector;
    uint32_t backup_boot_sector;
    uint32_t cluster_count;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint8_t fat_count;
    uint8_t active_fat;
    bool mirror_fats;
    bool mounted;
    bool write_failed;
    uint32_t blocks_per_sector;
};

/* Public operations: 0 success, negative kernel/error.h status. */
int fat32_mount(struct fat32_fs *fs, struct block_device *device);
int fat32_mount_at(struct fat32_fs *fs, struct block_device *device,
                   uint64_t volume_lba);
int fat32_stat(struct fat32_fs *fs, const char *path,
               struct fat32_file_info *info);
int fat32_read_file(struct fat32_fs *fs, const char *path,
                    void *buffer, uint32_t capacity, uint32_t *bytes_read);
int fat32_write_file(struct fat32_fs *fs, const char *path,
                     const void *data, uint32_t size);
int fat32_remove_file(struct fat32_fs *fs, const char *path);

/* Callback receives one non-dot, non-volume entry; return 0 to continue.
   Operations require external serialization. No callback may mutate this fs. */
typedef int (*fat32_list_callback)(void *user, const struct fat32_file_info *info);
int fat32_list_dir(struct fat32_fs *fs, const char *path,
                   fat32_list_callback callback, void *user);
int fat32_mkdir(struct fat32_fs *fs, const char *path);
/* Existing regular files are preserved. No RTC timestamp update is provided. */
int fat32_touch(struct fat32_fs *fs, const char *path);

/* Offset I/O uses bytes, supports zero-filled extension, and never allocates a
 * whole file in RAM. All calls on one instance require external serialization. */
int fat32_pread(struct fat32_fs *, const char *, uint32_t offset, void *, uint32_t capacity, uint32_t *done);
int fat32_pwrite(struct fat32_fs *, const char *, uint32_t offset, const void *, uint32_t size, uint32_t *done);
int fat32_truncate(struct fat32_fs *, const char *, uint32_t size);
int fat32_rmdir(struct fat32_fs *, const char *);
/* Replace a same-kind destination (directories must be empty). Reject cycles. */
int fat32_rename(struct fat32_fs *, const char *old_path, const char *new_path);
int fat32_set_attributes(struct fat32_fs *, const char *, uint8_t attributes);
int fat32_set_times(struct fat32_fs *, const char *, const struct fat32_file_info *);
int fat32_free_space(struct fat32_fs *, uint64_t *bytes);
struct filesystem_type;
extern const struct filesystem_type fat32_type;
struct fat32_mount_options { bool explicit_lba; uint64_t lba; };
#endif
