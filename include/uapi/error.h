#ifndef UAPI_ERROR_H
#define UAPI_ERROR_H
/* Public syscall errors follow Newlib's generic sys/errno.h, not Linux errno
 * or kernel/error.h. Only the errors currently exposed by the ABI are listed. */
#define KITOS_ENOENT 2
#define KITOS_ESRCH 3
#define KITOS_EIO 5
#define KITOS_E2BIG 7
#define KITOS_ENOEXEC 8
#define KITOS_EBADF 9
#define KITOS_ECHILD 10
#define KITOS_EAGAIN 11
#define KITOS_ENOMEM 12
#define KITOS_EACCES 13
#define KITOS_EPIPE 32
#define KITOS_EFAULT 14
#define KITOS_EBUSY 16
#define KITOS_EEXIST 17
#define KITOS_EXDEV 18
#define KITOS_ENODEV 19
#define KITOS_ENOTDIR 20
#define KITOS_EISDIR 21
#define KITOS_EINVAL 22
#define KITOS_ENFILE 23
#define KITOS_EMFILE 24
#define KITOS_ENOTTY 25
#define KITOS_ENOSPC 28
#define KITOS_ESPIPE 29
#define KITOS_EROFS 30
#define KITOS_ENOSYS 88
#define KITOS_ENOTEMPTY 90
#define KITOS_ENAMETOOLONG 91
#define KITOS_ETIMEDOUT 116
#define KITOS_ENOTSUP 134
#define KITOS_EOVERFLOW 139
#define KITOS_ECANCELED 140
#define KITOS_EMSGSIZE 122
#endif
