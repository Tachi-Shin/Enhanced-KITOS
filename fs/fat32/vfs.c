#include <fs/fat32.h>
#include <fs/vfs.h>
#include <kernel/error.h>
#include <lib/string.h>
static struct { struct fat32_fs fs; bool used; } instances[VFS_MAX_MOUNTS];
static int status(int rc) { return rc == 0 ? 0 : rc == 1 ? -K_ENOENT : rc == -1 ? -K_EIO : rc; }
static struct fat32_fs *filesystem(struct super_block *sb) { return sb->private_data; }
static int fat_lookup(struct super_block *sb, const char *path, struct inode *node)
{
    struct fat32_file_info info;
    int rc = status(fat32_stat(filesystem(sb), path, &info));
    if (rc) return rc;
    /* node may be refreshed in place, with path == node->path. */
    if (path != node->path) strcpy(node->path, path);
    node->sb = sb; node->number = info.number;
    node->size = info.size; node->attributes = info.attributes;
    node->kind = info.attributes & FAT32_ATTR_DIRECTORY ? VFS_DIRECTORY : VFS_REGULAR;
    return 0;
}
static int fat_read(struct inode *node, uint64_t offset, void *data, uint32_t size, uint32_t *done)
{
    if (offset > 0xFFFFFFFFU) { *done = 0; return 0; }
    return status(fat32_pread(filesystem(node->sb), node->path, (uint32_t)offset, data, size, done));
}
static int fat_write(struct inode *node, uint64_t offset, const void *data, uint32_t size, uint32_t *done)
{
    if (offset > 0xFFFFFFFFU || size > 0xFFFFFFFFU - offset) return -K_EOVERFLOW;
    if (node->attributes & FAT32_ATTR_READ_ONLY) return -K_EROFS;
    return status(fat32_pwrite(filesystem(node->sb), node->path, (uint32_t)offset, data, size, done));
}
static int fat_truncate(struct inode *node, uint64_t size)
{
    if (size > 0xFFFFFFFFU) return -K_EOVERFLOW;
    return status(fat32_truncate(filesystem(node->sb), node->path, (uint32_t)size));
}
static int fat_create(struct super_block *sb, const char *path, bool dir)
{
    struct fat32_file_info info;
    int rc = fat32_stat(filesystem(sb), path, &info);
    if (!rc) return -K_EEXIST;
    if (rc != -K_ENOENT) return status(rc);
    return status(dir ? fat32_mkdir(filesystem(sb), path) : fat32_touch(filesystem(sb), path));
}
static int fat_remove(struct super_block *sb, const char *path, bool dir)
{
    struct fat32_file_info info;
    int rc = status(fat32_stat(filesystem(sb), path, &info));
    if (rc) return rc;
    if (info.attributes & FAT32_ATTR_READ_ONLY) return -K_EROFS;
    if (dir != ((info.attributes & FAT32_ATTR_DIRECTORY) != 0)) return dir ? -K_ENOTDIR : -K_EISDIR;
    return status(dir ? fat32_rmdir(filesystem(sb), path) : fat32_remove_file(filesystem(sb), path));
}
static int fat_rename(struct super_block *sb, const char *old, const char *new)
{ return status(fat32_rename(filesystem(sb), old, new)); }
struct listing { vfs_dir_callback callback; void *user; int result; };
static int list_entry(void *argument, const struct fat32_file_info *info)
{
    struct listing *list = argument;
    struct vfs_dirent entry;
    strcpy(entry.name, info->name); entry.size = info->size;
    entry.kind = info->attributes & FAT32_ATTR_DIRECTORY ? VFS_DIRECTORY : VFS_REGULAR;
    list->result = list->callback(list->user, &entry);
    return list->result;
}
static int fat_list(struct super_block *sb, const char *path, vfs_dir_callback callback, void *user)
{
    struct listing list = {callback, user, 0};
    int rc = status(fat32_list_dir(filesystem(sb), path, list_entry, &list));
    return list.result ? list.result : rc;
}
static void fat_release(struct super_block *sb)
{
    for (unsigned int i = 0; i < VFS_MAX_MOUNTS; ++i)
        if (&instances[i].fs == sb->private_data) { instances[i].used = false; instances[i].fs.mounted = false; }
    sb->private_data = NULL;
}
static const struct super_operations super_ops = {
    fat_lookup, fat_create, fat_remove, fat_rename, fat_list, fat_release
};
static const struct file_operations file_ops = {fat_read, fat_write, fat_truncate};
static int fat_mount(struct super_block *sb, struct block_device *device, const void *data)
{
    const struct fat32_mount_options *options = data;
    for (unsigned int i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (instances[i].used) continue;
        struct fat32_fs *fs = &instances[i].fs;
        int rc = options && options->explicit_lba ? fat32_mount_at(fs, device, options->lba) : fat32_mount(fs, device);
        if (rc) return status(rc);
        instances[i].used = true; sb->private_data = fs;
        sb->ops = &super_ops; sb->file_ops = &file_ops;
        return 0;
    }
    return -K_ENOSPC;
}
const struct filesystem_type fat32_type = {"fat32", fat_mount};
