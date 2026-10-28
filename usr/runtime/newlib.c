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
#include <stdarg.h>
#define CHECK_ERRNO(name) _Static_assert(name == KITOS_##name, "Newlib errno ABI: " #name)
CHECK_ERRNO(ENOENT); CHECK_ERRNO(ESRCH); CHECK_ERRNO(EIO); CHECK_ERRNO(E2BIG);
CHECK_ERRNO(ENOEXEC); CHECK_ERRNO(EBADF); CHECK_ERRNO(ECHILD); CHECK_ERRNO(EAGAIN);
CHECK_ERRNO(ENOMEM); CHECK_ERRNO(EFAULT); CHECK_ERRNO(EBUSY); CHECK_ERRNO(EEXIST);
CHECK_ERRNO(EXDEV); CHECK_ERRNO(ENODEV); CHECK_ERRNO(ENOTDIR); CHECK_ERRNO(EISDIR);
CHECK_ERRNO(EINVAL); CHECK_ERRNO(ENFILE); CHECK_ERRNO(EMFILE); CHECK_ERRNO(ENOTTY);
CHECK_ERRNO(ENOSPC); CHECK_ERRNO(ESPIPE); CHECK_ERRNO(EROFS); CHECK_ERRNO(ENOSYS);
CHECK_ERRNO(ENOTEMPTY); CHECK_ERRNO(ENAMETOOLONG); CHECK_ERRNO(ETIMEDOUT);
CHECK_ERRNO(ENOTSUP); CHECK_ERRNO(EOVERFLOW); CHECK_ERRNO(ECANCELED);
CHECK_ERRNO(EMSGSIZE);CHECK_ERRNO(EACCES);CHECK_ERRNO(EPIPE);
#undef CHECK_ERRNO
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
    out->st_ino = wire.inode; out->st_dev = wire.device; out->st_size = wire.size; out->st_nlink = 1;
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
    /* brk(0) queries the current break; it cannot shrink the heap to zero. */
    if (!next) { errno = ENOMEM; return (void *)-1; }
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
int kitos_input_read(struct kitos_input_event *events, unsigned int capacity)
{ return (int)result(kitos_raw_syscall(KITOS_SYS_INPUT_READ, (long)events, capacity, 0)); }
int kitos_readdir(const char *path, unsigned long long index, struct kitos_dirent *entry)
{ return (int)result(kitos_raw_syscall(KITOS_SYS_READDIR, (long)path, index, (long)entry)); }
long kitos_control(unsigned int op, unsigned long a, unsigned long b)
{ return result(kitos_raw_syscall(KITOS_SYS_CONTROL, op, a, b)); }
int chdir(const char *path)
{ return (int)result(kitos_raw_syscall(KITOS_SYS_CHDIR, (long)path, 0, 0)); }
char *getcwd(char *out, size_t size)
{
    if (!out) { errno = EINVAL; return NULL; }
    return result(kitos_raw_syscall(KITOS_SYS_GETCWD, (long)out, size, 0)) < 0 ? NULL : out;
}
int mkdir(const char *path, mode_t mode)
{ (void)mode; return (int)result(kitos_raw_syscall(KITOS_SYS_MKDIR, (long)path, 0, 0)); }
int rmdir(const char *path)
{ return (int)result(kitos_raw_syscall(KITOS_SYS_RMDIR, (long)path, 0, 0)); }
int rename(const char *old, const char *new)
{ return (int)result(kitos_raw_syscall(KITOS_SYS_RENAME, (long)old, (long)new, 0)); }
int ftruncate(int fd, off_t size)
{ return (int)result(kitos_raw_syscall(KITOS_SYS_FTRUNCATE, fd, size, 0)); }
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

int kitos_tty_info(struct kitos_terminal_info *out)
{ return (int)result(kitos_raw_syscall(KITOS_SYS_TTY_INFO,(long)out,0,0)); }
pid_t kitos_spawn_with_fds(const char *path,char *const argv[],char *const envp[],const struct kitos_fd_map *maps,unsigned int count)
{
    struct kitos_spawn_spec spec={(uintptr_t)path,(uintptr_t)argv,(uintptr_t)(envp?envp:environ),(uintptr_t)maps,count,0};
    return (pid_t)result(kitos_raw_syscall(KITOS_SYS_SPAWN_EX,(long)&spec,0,0));
}
int kitos_pipe(int fds[2],unsigned int flags){return (int)result(kitos_raw_syscall(KITOS_SYS_PIPE,(long)fds,flags,0));}
int kitos_channel(int fds[2],unsigned int flags){return (int)result(kitos_raw_syscall(KITOS_SYS_CHANNEL,(long)fds,flags,0));}
int pipe(int fds[2]){return kitos_pipe(fds,0);}
int pipe2(int fds[2],int flags)
{
    int allowed=O_NONBLOCK;
#ifdef O_CLOEXEC
    allowed|=O_CLOEXEC;
#endif
    if(flags&~allowed){errno=EINVAL;return -1;}
    unsigned int wire=flags&O_NONBLOCK?KITOS_IPC_NONBLOCK:0;
#ifdef O_CLOEXEC
    if(flags&O_CLOEXEC)wire|=KITOS_IPC_CLOEXEC;
#endif
    return kitos_pipe(fds,wire);
}
int dup(int fd){return (int)result(kitos_raw_syscall(KITOS_SYS_DUP,fd,-1,0));}
int dup2(int fd,int target){if(target<0){errno=EBADF;return -1;}return (int)result(kitos_raw_syscall(KITOS_SYS_DUP,fd,target,0));}
int fcntl(int fd,int command,...)
{
    unsigned int wire;long value=0;
    if(command==F_GETFL)wire=KITOS_F_GETFL;else if(command==F_GETFD)wire=KITOS_F_GETFD;
    else if(command==F_SETFL||command==F_SETFD){
        va_list args;va_start(args,command);int n=va_arg(args,int);va_end(args);
        wire=command==F_SETFL?KITOS_F_SETFL:KITOS_F_SETFD;
        if(command==F_SETFL){if(n&~(O_NONBLOCK|O_ACCMODE)){errno=EINVAL;return -1;}value=n&O_NONBLOCK?KITOS_IPC_NONBLOCK:0;}
        else{if(n&~FD_CLOEXEC){errno=EINVAL;return -1;}value=n&FD_CLOEXEC?KITOS_IPC_CLOEXEC:0;}
    }else{errno=ENOTSUP;return -1;}
    long rc=result(kitos_raw_syscall(KITOS_SYS_FCNTL,fd,wire,value));
    if(rc>=0&&command==F_GETFL)rc=(rc&3)|((rc&KITOS_IPC_NONBLOCK)?O_NONBLOCK:0);
    if(rc>=0&&command==F_GETFD)rc=rc&KITOS_IPC_CLOEXEC?FD_CLOEXEC:0;
    return (int)rc;
}
int kitos_poll(struct kitos_pollfd *fds,unsigned int count,int timeout){return (int)result(kitos_raw_syscall(KITOS_SYS_POLL,(long)fds,count,timeout));}
int kitos_listen(const char *name,unsigned int backlog,unsigned int flags){return (int)result(kitos_raw_syscall(KITOS_SYS_LISTEN,(long)name,backlog,flags));}
int kitos_connect(const char *name,unsigned int flags){return (int)result(kitos_raw_syscall(KITOS_SYS_CONNECT,(long)name,flags,0));}
int kitos_accept(int fd,unsigned int flags){return (int)result(kitos_raw_syscall(KITOS_SYS_ACCEPT,fd,flags,0));}
int kitos_event_create(uint64_t initial,unsigned int flags){return (int)result(kitos_raw_syscall(KITOS_SYS_EVENT,(long)initial,flags,0));}
int kitos_shm_open(const char *name,size_t size,unsigned int flags){return (int)result(kitos_raw_syscall(KITOS_SYS_SHM_OPEN,(long)name,(long)size,flags));}
void *kitos_shm_map(int fd,unsigned int flags){long rc=result(kitos_raw_syscall(KITOS_SYS_SHM_MAP,fd,flags,0));return rc<0?(void *)-1:(void *)rc;}
int kitos_shm_unmap(void *p){return (int)result(kitos_raw_syscall(KITOS_SYS_SHM_UNMAP,(long)p,0,0));}
int kitos_shm_unlink(const char *name){return (int)result(kitos_raw_syscall(KITOS_SYS_SHM_UNLINK,(long)name,0,0));}

int kitos_display_acquire(void){return (int)result(kitos_raw_syscall(KITOS_SYS_DISPLAY,KITOS_DISPLAY_ACQUIRE,0,0));}
int kitos_display_present(int fd,unsigned int width,unsigned int height,unsigned int pitch){return (int)result(kitos_raw_syscall6(KITOS_SYS_DISPLAY,KITOS_DISPLAY_PRESENT,fd,width,height,pitch,0));}
int kitos_display_release(void){return (int)result(kitos_raw_syscall(KITOS_SYS_DISPLAY,KITOS_DISPLAY_RELEASE,0,0));}
