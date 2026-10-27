#ifndef UAPI_ERROR_H
#define UAPI_ERROR_H
/* Public syscall errors follow Newlib's generic sys/errno.h, not Linux errno
 * or kernel/error.h. Only the errors currently exposed by the ABI are listed. */
#define KITOS_EBADF 9
#define KITOS_EFAULT 14
#define KITOS_EINVAL 22
#define KITOS_ENOSYS 88
#endif
