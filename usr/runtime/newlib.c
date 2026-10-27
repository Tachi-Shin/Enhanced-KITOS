#include "arch_syscall.h"
#include "kitos.h"
#include <uapi/syscall.h>
#include <uapi/process.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
_Static_assert(ENOSYS == KITOS_ENOSYS && EFAULT == KITOS_EFAULT && EINVAL == KITOS_EINVAL, "Newlib errno ABI");
char **environ;
static long result(long value)
{ if (value < 0) { errno = (int)-value; return -1; } return value; }
void _exit(int status)
{ kitos_raw_syscall(KITOS_SYS_EXIT, status, 0, 0); for (;;) {} }
int _write(int fd, const void *data, size_t length)
{ return (int)result(kitos_raw_syscall(KITOS_SYS_WRITE, fd, (long)data, (long)length)); }
int _read(int fd, void *data, size_t length)
{ return (int)result(kitos_raw_syscall(KITOS_SYS_READ, fd, (long)data, (long)length)); }
int _open(const char *path, int flags, ...)
{
    int mode = flags & O_ACCMODE, wire = mode == O_RDONLY ? KITOS_OPEN_READ :
        mode == O_WRONLY ? KITOS_OPEN_WRITE : mode == O_RDWR ? KITOS_OPEN_READ | KITOS_OPEN_WRITE : 0;
    int allowed = O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND;
#ifdef O_BINARY
    allowed |= O_BINARY;
#endif
    if (!wire || (flags & ~allowed)) { errno = EINVAL; return -1; }
    if (flags & O_CREAT) wire |= KITOS_OPEN_CREATE;
    if (flags & O_EXCL) wire |= KITOS_OPEN_EXCL;
    if (flags & O_TRUNC) wire |= KITOS_OPEN_TRUNC;
    if (flags & O_APPEND) wire |= KITOS_OPEN_APPEND;
    return (int)result(kitos_raw_syscall(KITOS_SYS_OPEN, (long)path, wire, 0));
}
int _close(int fd) { return (int)result(kitos_raw_syscall(KITOS_SYS_CLOSE, fd, 0, 0)); }
off_t _lseek(int fd, off_t offset, int whence)
{ return (off_t)result(kitos_raw_syscall(KITOS_SYS_LSEEK, fd, offset, whence)); }
static int stat_call(long number, long target, struct stat *out)
{
    if (!out) { errno = EFAULT; return -1; }
    struct kitos_stat wire;
    if (result(kitos_raw_syscall(number, target, (long)&wire, 0)) < 0) return -1;
    memset(out, 0, sizeof(*out));
    out->st_ino = wire.inode; out->st_size = wire.size; out->st_nlink = 1;
    out->st_mode = (wire.kind == KITOS_KIND_CHAR ? S_IFCHR : wire.kind == KITOS_KIND_DIRECTORY ? S_IFDIR : S_IFREG) | 0666;
    out->st_blksize = 512; out->st_blocks = (wire.size + 511) / 512;
    return 0;
}
int _fstat(int fd, struct stat *out) { return stat_call(KITOS_SYS_FSTAT, fd, out); }
int _stat(const char *path, struct stat *out) { return stat_call(KITOS_SYS_STAT, (long)path, out); }
int _isatty(int fd)
{ long rc = result(kitos_raw_syscall(KITOS_SYS_ISATTY, fd, 0, 0)); return rc < 0 ? 0 : (int)rc; }
int _unlink(const char *path) { return (int)result(kitos_raw_syscall(KITOS_SYS_UNLINK, (long)path, 0, 0)); }
void *_sbrk(ptrdiff_t increment)
{
    long previous = kitos_raw_syscall(KITOS_SYS_BRK, 0, 0, 0);
    if (previous < 0) { result(previous); return (void *)-1; }
    if ((increment > 0 && previous > LONG_MAX - increment) ||
        (increment < 0 && (unsigned long)(0UL - (unsigned long)increment) > (unsigned long)previous)) {
        errno = ENOMEM; return (void *)-1;
    }
    long next = previous + increment;
    if (result(kitos_raw_syscall(KITOS_SYS_BRK, next, 0, 0)) < 0) return (void *)-1;
    return (void *)previous;
}
int _getpid(void) { return (int)result(kitos_raw_syscall(KITOS_SYS_GETPID, 0, 0, 0)); }
int _kill(int pid, int signal) { return (int)result(kitos_raw_syscall(KITOS_SYS_KILL, pid, signal, 0)); }
int _wait(int *status) { return kitos_waitpid(-1, status, 0); }
int _fork(void) { errno = ENOSYS; return -1; }
int _execve(const char *p, char *const a[], char *const e[])
{ (void)p; (void)a; (void)e; errno = ENOSYS; return -1; }
int _link(const char *a, const char *b) { (void)a; (void)b; errno = ENOSYS; return -1; }
int _gettimeofday(struct timeval *tv, void *tz)
{ (void)tv; (void)tz; errno = ENOSYS; return -1; }
clock_t _times(struct tms *out) { (void)out; errno = ENOSYS; return (clock_t)-1; }
int kitos_yield(void) { return (int)result(kitos_raw_syscall(KITOS_SYS_YIELD, 0, 0, 0)); }
int kitos_sleep_ms(uint32_t ms) { return (int)result(kitos_raw_syscall(KITOS_SYS_SLEEP_MS, ms, 0, 0)); }
uint64_t kitos_clock_ms(void)
{ uint64_t value = 0; result(kitos_raw_syscall(KITOS_SYS_CLOCK_MS, (long)&value, 0, 0)); return value; }
pid_t kitos_spawn(const char *path, char *const argv[], char *const envp[])
{ return (pid_t)result(kitos_raw_syscall(KITOS_SYS_SPAWN, (long)path, (long)argv, (long)envp)); }
pid_t kitos_waitpid(pid_t pid, int *status, int options)
{ return (pid_t)result(kitos_raw_syscall(KITOS_SYS_WAITPID, pid, (long)status, options)); }
/* Supply reentrant hooks directly. Newlib's generic covers use a separate
 * legacy global errno, while KITOS keeps errno in each process's _reent. */
#include <reent.h>
#define REENT_ERROR(r, value) do { if ((value) == (__typeof__(value))-1) _REENT_ERRNO(r) = errno; } while (0)
_ssize_t _write_r(struct _reent *r, int fd, const void *data, size_t length)
{ _ssize_t value = _write(fd, data, length); REENT_ERROR(r, value); return value; }
_ssize_t _read_r(struct _reent *r, int fd, void *data, size_t length)
{ _ssize_t value = _read(fd, data, length); REENT_ERROR(r, value); return value; }
int _open_r(struct _reent *r, const char *path, int flags, int mode)
{ int value = _open(path, flags, mode); REENT_ERROR(r, value); return value; }
int _close_r(struct _reent *r, int fd)
{ int value = _close(fd); REENT_ERROR(r, value); return value; }
_off_t _lseek_r(struct _reent *r, int fd, _off_t offset, int whence)
{ _off_t value = _lseek(fd, offset, whence); REENT_ERROR(r, value); return value; }
int _fstat_r(struct _reent *r, int fd, struct stat *out)
{ int value = _fstat(fd, out); REENT_ERROR(r, value); return value; }
int _stat_r(struct _reent *r, const char *path, struct stat *out)
{ int value = _stat(path, out); REENT_ERROR(r, value); return value; }
int _unlink_r(struct _reent *r, const char *path)
{ int value = _unlink(path); REENT_ERROR(r, value); return value; }
void *_sbrk_r(struct _reent *r, ptrdiff_t increment)
{ void *value = _sbrk(increment); if (value == (void *)-1) _REENT_ERRNO(r) = errno; return value; }
int _isatty_r(struct _reent *r, int fd)
{ int value = _isatty(fd); if (!value) _REENT_ERRNO(r) = errno; return value; }
int _getpid_r(struct _reent *r)
{ int value = _getpid(); REENT_ERROR(r, value); return value; }
int _kill_r(struct _reent *r, int pid, int signal)
{ int value = _kill(pid, signal); REENT_ERROR(r, value); return value; }
int _wait_r(struct _reent *r, int *status)
{ int value = _wait(status); REENT_ERROR(r, value); return value; }
int _gettimeofday_r(struct _reent *r, struct timeval *tv, void *tz)
{ int value = _gettimeofday(tv, tz); REENT_ERROR(r, value); return value; }
clock_t _times_r(struct _reent *r, struct tms *out)
{ clock_t value = _times(out); REENT_ERROR(r, value); return value; }
int _link_r(struct _reent *r, const char *a, const char *b)
{ int value = _link(a, b); REENT_ERROR(r, value); return value; }
int _fork_r(struct _reent *r)
{ int value = _fork(); REENT_ERROR(r, value); return value; }
int _execve_r(struct _reent *r, const char *p, char *const a[], char *const e[])
{ int value = _execve(p, a, e); REENT_ERROR(r, value); return value; }
