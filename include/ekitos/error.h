#ifndef EKITOS_DRIVER_ERROR_H
#define EKITOS_DRIVER_ERROR_H
/* Driver ABI v1 status numbers. API methods return zero or their negative.
 * These are independent of the Newlib/EL0 errno namespace. */
enum ek_driver_error {
    EK_EIO=5, EK_ENOENT=2, EK_EINVAL=22, EK_ENODEV=19, EK_ENOSPC=28,
    EK_EBUSY=16, EK_EEXIST=17, EK_ETIMEDOUT=110, EK_ENOTSUP=95,
    EK_EOVERFLOW=75, EK_ENOMEM=12, EK_ENOEXEC=8, EK_EAGAIN=11, EK_EROFS=30, EK_EPIPE=32
};
#endif
