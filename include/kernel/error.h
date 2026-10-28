#ifndef KERNEL_ERROR_H
#define KERNEL_ERROR_H
/* Internal kernel status values; these are not an EL0 ABI. */
enum kernel_error {
    K_EIO = 5, K_ENOENT = 2, K_EINVAL = 22, K_ENODEV = 19,
    K_ENOSPC = 28, K_EBUSY = 16, K_EEXIST = 17, K_ENOTDIR = 20,
    K_EISDIR = 21, K_EROFS = 30, K_ENOTEMPTY = 39, K_EXDEV = 18,
    K_ENAMETOOLONG = 36, K_ETIMEDOUT = 110, K_ENOTSUP = 95,
    K_EOVERFLOW = 75, K_EBADF = 9, K_ENOMEM = 12, K_EFAULT = 14,
    K_ENOSYS = 38, K_ECANCELED = 125, K_ENOEXEC = 8, K_ECHILD = 10, K_EPIPE = 32,
    K_ENFILE = 23, K_EMFILE = 24, K_ESRCH = 3, K_EAGAIN = 11, K_E2BIG = 7, K_ESPIPE = 29, K_ENOTTY = 25,
    K_EACCES = 13, K_EMSGSIZE = 90
};
#endif
