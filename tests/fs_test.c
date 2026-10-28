#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fs/vfs.h>
#include <fs/fat32.h>
#include <kernel/error.h>
#include "support/file_block.h"
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static unsigned int checks;
static int listed;
static int listing(void *unused, const struct vfs_dirent *entry)
{
    (void)unused; (void)entry;
    ++listed;
    struct vfs_stat st;
    CHECK(vfs_stat("/", &st) == -K_EBUSY);
    return 0;
}
int main(int argc, char **argv)
{
    CHECK(argc == 3);
    struct block_device disk, other; struct file_context context, context2;
    CHECK(disk_open(argv[1], &disk, &context) == 0);
    CHECK(disk_open(argv[2], &other, &context2) == 0);
    CHECK(vfs_register(&fat32_type) == 0);
    CHECK(vfs_register(&fat32_type) == -K_EEXIST);
    CHECK(vfs_mount("fat32", "/", &disk, NULL) == 0);
    struct vfs_stat st;
    CHECK(vfs_stat("/", &st) == 0 && st.kind == VFS_DIRECTORY);
    char resolved[VFS_PATH_MAX]="unchanged";
    CHECK(!vfs_resolve_directory("/../../",resolved)&&!strcmp(resolved,"/"));
    CHECK(vfs_resolve_directory("/missing/..",resolved)==-K_ENOENT&&!strcmp(resolved,"/"));
    CHECK(vfs_resolve_directory("/HELLO.TXT/..",resolved)==-K_ENOTDIR&&!strcmp(resolved,"/"));
    CHECK(vfs_resolve_directory("/HELLO.TXT",resolved)==-K_ENOTDIR&&!strcmp(resolved,"/"));
    CHECK(vfs_resolve_directory("relative",resolved)==-K_EINVAL&&!strcmp(resolved,"/"));
    CHECK(vfs_resolve_directory("/",NULL)==-K_EINVAL);
    CHECK(vfs_resolve_directory(NULL,resolved)==-K_EINVAL&&!strcmp(resolved,"/"));
    CHECK(vfs_mkdir("/test") == 0);
    CHECK(vfs_mkdir("/test") == -K_EEXIST);
    CHECK(vfs_mkdir("/test/child") == 0);
    CHECK(!vfs_resolve_directory("//test/./child/..//",resolved)&&!strcmp(resolved,"/test"));
    strcpy(resolved,"/test/child/../..");
    CHECK(!vfs_resolve_directory(resolved,resolved)&&!strcmp(resolved,"/"));
    struct file *f, *second;
    CHECK(vfs_open("/test/日本語 😀 long name.txt", VFS_READ | VFS_WRITE | VFS_CREATE | VFS_EXCL, &f) == 0);
    CHECK(vfs_unmount("/") == -K_EBUSY);
    CHECK(vfs_unlink("/test/日本語 😀 long name.txt") == -K_EBUSY);
    CHECK(vfs_open("/test/日本語 😀 long name.txt", VFS_READ, &second) == 0);
    char data[8193], back[8193];
    for (unsigned int i = 0; i < sizeof(data); ++i) data[i] = (char)(i * 17U + 3U);
    uint32_t done; uint64_t pos;
    CHECK(vfs_write(f, data, sizeof(data), &done) == 0 && done == sizeof(data));
    CHECK(vfs_read(second, back, sizeof(back), &done) == 0 && done == sizeof(back) && !memcmp(data, back, done));
    CHECK(vfs_seek(f, 509, 0, &pos) == 0 && pos == 509);
    CHECK(vfs_write(f, "boundary", 8, &done) == 0 && done == 8);
    CHECK(vfs_seek(f, 508, 0, &pos) == 0);
    CHECK(vfs_read(f, back, 10, &done) == 0 && done == 10 && back[0] == data[508] && back[9] == data[517] && !memcmp(back + 1, "boundary", 8));
    CHECK(vfs_ftruncate(f, 1) == 0);
    CHECK(vfs_seek(second, 0, 0, &pos) == 0);
    CHECK(vfs_read(second, back, sizeof(back), &done) == 0 && done == 1);
    CHECK(vfs_ftruncate(f, 6000) == 0);
    CHECK(vfs_seek(f, 1, 0, &pos) == 0);
    CHECK(vfs_read(f, back, 5999, &done) == 0 && done == 5999);
    for (unsigned int i = 0; i < done; ++i) CHECK(back[i] == 0);
    CHECK(vfs_seek(f, 7000, 0, &pos) == 0);
    CHECK(vfs_write(f, "tail", 4, &done) == 0);
    CHECK(vfs_seek(f, 5999, 0, &pos) == 0);
    CHECK(vfs_read(f, back, 1005, &done) == 0 && done == 1005 && !memcmp(back + 1001, "tail", 4));
    for (unsigned int i = 0; i < 1001; ++i) CHECK(back[i] == 0);
    CHECK(vfs_seek(f, -1, 0, &pos) == -K_EOVERFLOW);
    CHECK(vfs_seek(f, 0x7FFFFFFFFFFFFFFFLL, 0, &pos) == 0);
    CHECK(vfs_seek(f, 1, 1, &pos) == -K_EOVERFLOW);
    CHECK(vfs_write(f, data, 1, &done) == -K_EOVERFLOW && done == 0);
    CHECK(vfs_close(second) == 0);
    CHECK(vfs_close(f) == 0);
    CHECK(vfs_close(f) == -K_EBADF);
    CHECK(vfs_open("/test/日本語 😀 long name.txt", VFS_CREATE | VFS_EXCL | VFS_WRITE, &f) == -K_EEXIST);
    CHECK(vfs_open("/test/日本語 😀 long name.txt", VFS_WRITE | VFS_APPEND, &f) == 0);
    CHECK(vfs_seek(f, 0, 0, &pos) == 0);
    CHECK(vfs_write(f, "!", 1, &done) == 0);
    CHECK(vfs_close(f) == 0);
    CHECK(vfs_stat("/test/日本語 😀 long name.txt", &st) == 0 && st.size == 7005);
    CHECK(vfs_stat("/missing/../test", &st) == -K_ENOENT);
    CHECK(vfs_stat("/test/日本語 😀 long name.txt/..", &st) == -K_ENOTDIR);
    CHECK(vfs_open("/test/new/", VFS_WRITE | VFS_CREATE, &f) == -K_ENOENT);
    CHECK(vfs_stat("//test/./child/../../test", &st) == 0 && st.kind == VFS_DIRECTORY);
    CHECK(vfs_rename("/test/日本語 😀 long name.txt", "/test/child/renamed.txt") == 0);
    CHECK(vfs_stat("/test/日本語 😀 long name.txt", &st) == -K_ENOENT);
    CHECK(vfs_rmdir("/test/child") == -K_ENOTEMPTY);
    CHECK(vfs_rename("/test", "/test/child/cycle") != 0);
    CHECK(vfs_mkdir("/destination") == 0);
    CHECK(vfs_rename("/test/child", "/destination/moved") == 0);
    CHECK(vfs_stat("/destination/moved/renamed.txt", &st) == 0 && st.size == 7005);
    CHECK(vfs_list("/destination/moved", listing, NULL) == 0 && listed == 1);
    CHECK(vfs_unlink("/destination/moved/renamed.txt") == 0);
    CHECK(vfs_rmdir("/destination/moved") == 0);
    CHECK(vfs_mkdir("/mnt") == 0);
    CHECK(vfs_mount("fat32", "/mnt", &other, NULL) == 0);
    CHECK(vfs_stat("/mnt/MBRFILE.TXT", &st) == 0);
    CHECK(!vfs_resolve_directory("/mnt/..",resolved)&&!strcmp(resolved,"/"));
    CHECK(vfs_rename("/HELLO.TXT", "/mnt/hello.txt") == -K_EXDEV);
    CHECK(vfs_unmount("/") == -K_EBUSY);
    CHECK(vfs_rmdir("/mnt") == -K_EBUSY);
    CHECK(vfs_unmount("/mnt") == 0);
    CHECK(vfs_rmdir("/mnt") == 0);
    struct file *handles[VFS_MAX_FILES];
    for (unsigned int i = 0; i < VFS_MAX_FILES; ++i) CHECK(vfs_open("/HELLO.TXT", VFS_READ, &handles[i]) == 0);
    CHECK(vfs_open("/HELLO.TXT", VFS_READ, &f) == -K_ENFILE);
    for (unsigned int i = 0; i < VFS_MAX_FILES; ++i) CHECK(vfs_close(handles[i]) == 0);
    CHECK(vfs_unmount("/") == 0);
    struct fat32_fs fs;
    struct fat32_fs malformed = {.mounted = true};
    struct fat32_file_info invalid_info;
    CHECK(fat32_stat(&malformed, "/x", &invalid_info) < 0);
    CHECK(fat32_mount(&fs, &disk) == 0);
    uint64_t free_before, free_after;
    CHECK(fat32_free_space(&fs, &free_before) == 0);
    CHECK(fat32_mkdir(&fs, "/many") == 0);
    char name[800];
    for (int i = 0; i < 50; ++i) {
        snprintf(name, sizeof(name), "/many/Long directory-spanning filename number %d.txt", i);
        CHECK(fat32_write_file(&fs, name, data, 1111) == 0);
    }
    for (int i = 0; i < 50; ++i) {
        snprintf(name, sizeof(name), "/many/Long directory-spanning filename number %d.txt", i);
        CHECK(fat32_read_file(&fs, name, back, sizeof(back), &done) == 0 && done == 1111 && !memcmp(data, back, done));
        CHECK(fat32_remove_file(&fs, name) == 0);
    }
    CHECK(fat32_rmdir(&fs, "/many") == 0);
    CHECK(fat32_free_space(&fs, &free_after) == 0 && free_after == free_before);
    memset(name, 'a', 256); name[0] = '/'; name[256] = 0;
    CHECK(fat32_touch(&fs, name) == 0); // 255 UTF-16 units
    CHECK(fat32_remove_file(&fs, name) == 0);
    name[256] = 'a'; name[257] = 0;
    CHECK(fat32_touch(&fs, name) != 0);
    CHECK(fat32_touch(&fs, "/bad\xC0\xAF.txt") != 0);
    CHECK(fat32_touch(&fs, "/bad?.txt") != 0);
    CHECK(fat32_set_attributes(&fs, "/HELLO.TXT", FAT32_ATTR_READ_ONLY | FAT32_ATTR_HIDDEN) == 0);
    CHECK(fat32_remove_file(&fs, "/HELLO.TXT") != 0);
    CHECK(fat32_write_file(&fs, "/HELLO.TXT", data, 1) != 0);
    CHECK(fat32_set_attributes(&fs, "/HELLO.TXT", FAT32_ATTR_ARCHIVE) == 0);
    struct fat32_file_info info;
    CHECK(fat32_stat(&fs, "/HELLO.TXT", &info) == 0);
    info.create_date = info.modify_date = info.access_date = (46U << 9) | (10U << 5) | 27U;
    info.create_time = info.modify_time = (12U << 11) | (34U << 5) | 28U; info.create_tenths = 123;
    CHECK(fat32_set_times(&fs, "/HELLO.TXT", &info) == 0);
    CHECK(fat32_stat(&fs, "/HELLO.TXT", &info) == 0 && info.create_tenths == 123);
    info.access_date = 0;
    CHECK(fat32_set_times(&fs, "/HELLO.TXT", &info) != 0);
    CHECK(fat32_touch(&fs, "/école-Δ.txt") == 0);
    CHECK(fat32_stat(&fs, "/ÉCOLE-δ.TXT", &info) == 0);
    CHECK(fat32_remove_file(&fs, "/ÉCOLE-δ.TXT") == 0);
    /* ENOSPC must leave an existing file intact; restore the test FAT after
     * marking otherwise-free clusters unavailable. No physical medium used. */
    size_t fat_bytes = (size_t)fs.sectors_per_fat * fs.bytes_per_sector;
    unsigned char *fat_saved = malloc(fat_bytes), *fat_full = malloc(fat_bytes);
    CHECK(fat_saved && fat_full);
    uint64_t fat_lba = fs.volume_lba + (uint64_t)fs.first_fat_sector * fs.blocks_per_sector;
    CHECK(block_read(&disk, fat_lba, fs.sectors_per_fat * fs.blocks_per_sector, fat_saved) == 0);
    memcpy(fat_full, fat_saved, fat_bytes);
    for (uint32_t i = 2; i < fs.cluster_count + 2; ++i) {
        unsigned char *p = fat_full + (size_t)i * 4;
        if (!(p[0] | p[1] | p[2] | (p[3] & 15))) {
            p[0] = p[1] = p[2] = 255; p[3] = 15;
        }
    }
    for (unsigned int i = 0; i < fs.fat_count; ++i)
        CHECK(block_write(&disk, fat_lba + (uint64_t)i * fs.sectors_per_fat * fs.blocks_per_sector,
                          fs.sectors_per_fat * fs.blocks_per_sector, fat_full) == 0);
    CHECK(fat32_write_file(&fs, "/HELLO.TXT", data, sizeof(data)) == -K_ENOSPC);
    CHECK(fat32_read_file(&fs, "/HELLO.TXT", back, sizeof(back), &done) == 0 && done == 13 && !memcmp(back, "initial-data\n", 13));
    for (unsigned int i = 0; i < fs.fat_count; ++i)
        CHECK(block_write(&disk, fat_lba + (uint64_t)i * fs.sectors_per_fat * fs.blocks_per_sector,
                          fs.sectors_per_fat * fs.blocks_per_sector, fat_saved) == 0);
    free(fat_saved); free(fat_full);
    CHECK(fat32_write_file(&fs, "/CaseName.txt", "source", 6) == 0);
    CHECK(fat32_rename(&fs, "/CaseName.txt", "/casename.TXT") == 0);
    CHECK(fat32_stat(&fs, "/CASENAME.TXT", &info) == 0 && !strcmp(info.name, "casename.TXT"));
    CHECK(fat32_write_file(&fs, "/replace.txt", "target", 6) == 0);
    CHECK(fat32_rename(&fs, "/casename.TXT", "/replace.txt") == 0);
    CHECK(fat32_read_file(&fs, "/replace.txt", back, sizeof(back), &done) == 0 && done == 6 && !memcmp(back, "source", 6));
    CHECK(fat32_remove_file(&fs, "/replace.txt") == 0);
    CHECK(fat32_mkdir(&fs, "/empty-a") == 0 && fat32_mkdir(&fs, "/empty-b") == 0);
    CHECK(fat32_rename(&fs, "/empty-a", "/empty-b") == 0);
    CHECK(fat32_rmdir(&fs, "/empty-b") == 0);
    /* Non-mirrored volumes must read/write only the selected FAT. */
    unsigned char ext_boot[512], original_boot[512];
    CHECK(block_read(&disk, 0, 1, original_boot) == 0);
    memcpy(ext_boot, original_boot, 512); ext_boot[40] = 0x81; ext_boot[41] = 0;
    CHECK(block_write(&disk, 0, 1, ext_boot) == 0);
    struct fat32_fs active;
    CHECK(fat32_mount(&active, &disk) == 0 && active.active_fat == 1 && !active.mirror_fats);
    unsigned char *first_fat = malloc(fat_bytes), *after_fat = malloc(fat_bytes);
    CHECK(first_fat && after_fat);
    CHECK(block_read(&disk, fat_lba, fs.sectors_per_fat * fs.blocks_per_sector, first_fat) == 0);
    CHECK(fat32_write_file(&active, "/ACTIVE.BIN", data, sizeof(data)) == 0);
    CHECK(fat32_read_file(&active, "/ACTIVE.BIN", back, sizeof(back), &done) == 0 && done == sizeof(back));
    CHECK(block_read(&disk, fat_lba, fs.sectors_per_fat * fs.blocks_per_sector, after_fat) == 0);
    CHECK(!memcmp(first_fat, after_fat, fat_bytes));
    CHECK(fat32_remove_file(&active, "/ACTIVE.BIN") == 0);
    CHECK(block_write(&disk, 0, 1, original_boot) == 0);
    /* A looped FAT chain is rejected before truncating or freeing clusters. */
    CHECK(fat32_write_file(&fs, "/CYCLE.BIN", data, sizeof(data)) == 0);
    CHECK(fat32_stat(&fs, "/CYCLE.BIN", &info) == 0);
    uint32_t cluster = info.first_cluster;
    uint64_t sector_lba = fat_lba + ((uint64_t)cluster * 4 / 512);
    unsigned char fat_entry[512], saved_entry[512];
    CHECK(block_read(&disk, sector_lba, 1, saved_entry) == 0);
    memcpy(fat_entry, saved_entry, 512);
    unsigned int entry_offset = (cluster * 4) % 512;
    for (unsigned int i = 0; i < 4; ++i) fat_entry[entry_offset + i] = (unsigned char)(cluster >> (8U * i));
    CHECK(block_write(&disk, sector_lba, 1, fat_entry) == 0);
    CHECK(fat32_truncate(&fs, "/CYCLE.BIN", 0) != 0);
    CHECK(fat32_stat(&fs, "/CYCLE.BIN", &info) == 0 && info.size == sizeof(data));
    CHECK(block_write(&disk, sector_lba, 1, saved_entry) == 0);
    CHECK(fat32_remove_file(&fs, "/CYCLE.BIN") == 0);
    free(first_fat); free(after_fat);
    /* Bad geometry and a too-short MBR extent must be rejected before write. */
    unsigned char boot[512], invalid[512];
    CHECK(block_read(&disk, 0, 1, boot) == 0);
    memcpy(invalid, boot, 512); invalid[13] = 3;
    CHECK(block_write(&disk, 0, 1, invalid) == 0);
    struct fat32_fs rejected;
    CHECK(fat32_mount(&rejected, &disk) != 0 && !rejected.mounted);
    CHECK(block_write(&disk, 0, 1, boot) == 0);
    CHECK(block_read(&other, 0, 1, boot) == 0);
    memcpy(invalid, boot, 512); memset(invalid + 458, 0, 4); invalid[458] = 1;
    CHECK(block_write(&other, 0, 1, invalid) == 0);
    CHECK(fat32_mount(&rejected, &other) != 0 && !rejected.mounted);
    CHECK(block_write(&other, 0, 1, boot) == 0);
    context.fail_read = 0;
    CHECK(fat32_read_file(&fs, "/HELLO.TXT", back, sizeof(back), &done) != 0);
    context.fail_read = -1;
    context.fail_write = 0;
    CHECK(fat32_write_file(&fs, "/fault.txt", data, sizeof(data)) != 0 && fs.write_failed);
    context.fail_write = -1;
    CHECK(fat32_touch(&fs, "/after-fault.txt") != 0);
    CHECK(fat32_read_file(&fs, "/HELLO.TXT", back, sizeof(back), &done) == 0 && done == 13);
    close(context.fd); close(context2.fd);
    printf("VFS/FAT32 extended: PASS (%u checks)\n", checks);
}
