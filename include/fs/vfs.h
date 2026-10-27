#ifndef FS_VFS_H
#define FS_VFS_H
#include <dos/type.h>
#define VFS_PATH_MAX 2048U
#define VFS_NAME_MAX 768U
#define VFS_MAX_MOUNTS 4U
#define VFS_MAX_FILES 16U
#define VFS_REGULAR 1U
#define VFS_DIRECTORY 2U
#define VFS_READ 1U
#define VFS_WRITE 2U
#define VFS_CREATE 4U
#define VFS_EXCL 8U
#define VFS_TRUNC 16U
#define VFS_APPEND 32U
struct block_device;
struct super_block;
struct file;
struct inode {
    struct super_block *sb;
    uint64_t number, size;
    unsigned int kind;
    uint8_t attributes;
    char path[VFS_PATH_MAX]; /* Backend-relative, canonical path; no global cwd. */
};
struct vfs_dirent { char name[VFS_NAME_MAX]; uint64_t size; unsigned int kind; };
typedef int (*vfs_dir_callback)(void *, const struct vfs_dirent *);
struct file_operations {
    int (*read)(struct inode *, uint64_t offset, void *, uint32_t, uint32_t *);
    int (*write)(struct inode *, uint64_t offset, const void *, uint32_t, uint32_t *);
    int (*truncate)(struct inode *, uint64_t size);
};
struct super_operations {
    int (*lookup)(struct super_block *, const char *, struct inode *);
    int (*create)(struct super_block *, const char *, bool directory);
    int (*remove)(struct super_block *, const char *, bool directory);
    int (*rename)(struct super_block *, const char *, const char *);
    int (*list)(struct super_block *, const char *, vfs_dir_callback, void *);
    void (*release)(struct super_block *);
};
struct filesystem_type {
    const char *name;
    int (*mount)(struct super_block *, struct block_device *, const void *options);
};
struct super_block {
    const struct filesystem_type *type;
    const struct super_operations *ops;
    const struct file_operations *file_ops;
    struct block_device *device;
    void *private_data;
};
struct mount {
    char path[VFS_PATH_MAX];
    struct super_block sb;
    unsigned int open_files;
    bool used;
};
struct vfs_stat { uint64_t number, size; unsigned int kind; uint8_t attributes; };
/* Fixed object pools, no heap. Single nonblocking serialization gate: a
 * concurrent or callback-reentrant call returns -K_EBUSY, never spins in IRQ.
 * Task context only. Callbacks may not call VFS. Do not kill a task in a VFS call.
 * Open handles live until close; unmount/namespace mutation rejects busy mounts.
 * These kernel APIs deliberately are not file descriptor / syscall APIs. */
int vfs_register(const struct filesystem_type *type);
int vfs_mount(const char *type, const char *path, struct block_device *, const void *options);
int vfs_unmount(const char *path);
int vfs_stat(const char *path, struct vfs_stat *out);
int vfs_open(const char *path, unsigned int flags, struct file **out);
int vfs_close(struct file *file);
int vfs_fstat(struct file *, struct vfs_stat *out);
int vfs_read(struct file *, void *, uint32_t, uint32_t *done);
int vfs_write(struct file *, const void *, uint32_t, uint32_t *done);
int vfs_seek(struct file *, int64_t offset, unsigned int whence, uint64_t *position);
int vfs_ftruncate(struct file *, uint64_t size);
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);
int vfs_rmdir(const char *path);
int vfs_rename(const char *old_path, const char *new_path);
int vfs_list(const char *, vfs_dir_callback, void *);
#endif
