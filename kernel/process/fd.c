#include "internal.h"
#include <kernel/task.h>
#include <kernel/error.h>
#include <kernel/console.h>
#include <uapi/syscall.h>
#include <uapi/process.h>
#include <lib/string.h>
#define RETRY_BUSY(expression) do { rc = (expression); if (rc == -K_EBUSY) task_sleep_ms(1); } while (rc == -K_EBUSY)

int process_user_string(uint64_t address, char *out, size_t capacity)
{
    if (!capacity) return -K_EINVAL;
    struct vm_space *space = task_current_space();
    for (size_t i = 0; i < capacity; ++i) {
        if (address > ~0ULL - i || copy_from_user(space, out + i, address + i, 1)) return -K_EFAULT;
        if (!out[i]) return 0;
    }
    return -K_ENAMETOOLONG;
}
int process_user_path(uint64_t address, char *out, size_t capacity)
{
    int rc = process_user_string(address, out, capacity);
    if (rc || out[0] == '/') return rc;
    size_t n = strlen(out);
    if (!n) return -K_ENOENT;
    if (n + 2 > capacity) return -K_ENAMETOOLONG;
    for (size_t i = n + 1; i; --i) out[i] = out[i - 1];
    out[0] = '/';
    return 0;
}
static int64_t transfer(struct process_fd *fd, uint64_t address, uint64_t length, bool write)
{
    if (length > 1024U * 1024U) return -K_EINVAL;
    if ((write && fd->kind == FD_INPUT) || (!write && fd->kind == FD_OUTPUT)) return -K_EBADF;
    if (fd->kind == FD_FILE && !(fd->flags & (write ? VFS_WRITE : VFS_READ))) return -K_EBADF;
    struct vm_space *space = task_current_space();
    if (length && vm_user_check(space, address, (size_t)length, write ? VM_READ : VM_WRITE)) return -K_EFAULT;
    char buffer[512]; uint64_t total = 0;
    while (total < length) {
        uint32_t chunk = length - total > sizeof(buffer) ? sizeof(buffer) : (uint32_t)(length - total), done = 0;
        int rc = 0;
        if (write) {
            if (copy_from_user(space, buffer, address + total, chunk)) return total ? (int64_t)total : -K_EFAULT;
            if (fd->kind == FD_OUTPUT) {
                for (unsigned int i = 0; i < chunk; ++i) console_putc(buffer[i]);
                done = chunk;
            } else RETRY_BUSY(vfs_write(fd->file, buffer, chunk, &done));
        } else {
            if (fd->kind == FD_INPUT) {
                do {
                    rc = console_try_getc(buffer);
                    if (!rc) { process_unpin(); task_sleep_ms(1); process_pin(); }
                } while (!rc);
                if (rc > 0) { done = 1; rc = 0; if (buffer[0] == '\r') buffer[0] = '\n'; }
                else rc = -K_EIO;
            } else RETRY_BUSY(vfs_read(fd->file, buffer, chunk, &done));
            if (done && copy_to_user(space, address + total, buffer, done)) return total ? (int64_t)total : -K_EFAULT;
        }
        total += done;
        if (rc) return total ? (int64_t)total : rc;
        if (done < chunk) break;
    }
    return (int64_t)total;
}
static void wire_stat(struct kitos_stat *out, const struct vfs_stat *in)
{ *out = (struct kitos_stat){in->number, in->size, in->kind, 0}; }
int64_t process_fd_call(struct process *p, uint64_t number, const uint64_t a[6])
{
    int rc = 0;
    if (number == KITOS_SYS_OPEN || number == KITOS_SYS_STAT || number == KITOS_SYS_UNLINK) {
        char path[VFS_PATH_MAX];
        rc = process_user_path(a[0], path, sizeof(path));
        if (rc) return rc;
        if (path[0] != '/') return -K_EINVAL;
        if (number == KITOS_SYS_UNLINK) { RETRY_BUSY(vfs_unlink(path)); return rc; }
        if (number == KITOS_SYS_STAT) {
            if (vm_user_check(task_current_space(), a[1], sizeof(struct kitos_stat), VM_WRITE)) return -K_EFAULT;
            struct vfs_stat stat; RETRY_BUSY(vfs_stat(path, &stat));
            if (rc) return rc;
            struct kitos_stat wire; wire_stat(&wire, &stat);
            return copy_to_user(task_current_space(), a[1], &wire, sizeof(wire));
        }
        if (a[1] > 63) return -K_EINVAL;
        unsigned int index;
        for (index = 0; index < PROCESS_FD_MAX; ++index) if (!p->fd[index].kind) break;
        if (index == PROCESS_FD_MAX) return -K_EMFILE;
        struct file *file;
        RETRY_BUSY(vfs_open(path, (unsigned int)a[1], &file));
        if (rc) return rc;
        p->fd[index] = (struct process_fd){FD_FILE, (unsigned int)a[1], file};
        return index;
    }
    if (a[0] >= PROCESS_FD_MAX || !p->fd[a[0]].kind) return -K_EBADF;
    struct process_fd *fd = &p->fd[a[0]];
    switch (number) {
    case KITOS_SYS_READ: return transfer(fd, a[1], a[2], false);
    case KITOS_SYS_WRITE: return transfer(fd, a[1], a[2], true);
    case KITOS_SYS_CLOSE:
        if (fd->kind == FD_FILE) { RETRY_BUSY(vfs_close(fd->file)); if (rc) return rc; }
        *fd = (struct process_fd){0}; return 0;
    case KITOS_SYS_LSEEK: {
        if (fd->kind != FD_FILE) return -K_ESPIPE;
        if (a[2] > 2) return -K_EINVAL;
        uint64_t position;
        RETRY_BUSY(vfs_seek(fd->file, (int64_t)a[1], (unsigned int)a[2], &position));
        return rc ? rc : (int64_t)position;
    }
    case KITOS_SYS_ISATTY: return fd->kind == FD_FILE ? -K_ENOTTY : 1;
    case KITOS_SYS_FSTAT: {
        if (vm_user_check(task_current_space(), a[1], sizeof(struct kitos_stat), VM_WRITE)) return -K_EFAULT;
        struct kitos_stat wire = {0, 0, KITOS_KIND_CHAR, 0};
        if (fd->kind == FD_FILE) {
            struct vfs_stat stat; RETRY_BUSY(vfs_fstat(fd->file, &stat));
            if (rc) return rc;
            wire_stat(&wire, &stat);
        }
        return copy_to_user(task_current_space(), a[1], &wire, sizeof(wire));
    }
    default: return -K_ENOSYS;
    }
}
