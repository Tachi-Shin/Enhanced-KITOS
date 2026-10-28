#ifndef UAPI_SYSTEM_H
#define UAPI_SYSTEM_H
#define KITOS_NAME_MAX 768
/* Typed operations used by the /sbin utilities. This single-user system has
 * no UID/capability model yet; these calls have system administration access. */
enum kitos_control {
    KITOS_CTL_TIME, KITOS_CTL_PS, KITOS_CTL_MEM, KITOS_CTL_UART,
    KITOS_CTL_USB, KITOS_CTL_INPUT, KITOS_CTL_DISKS, KITOS_CTL_LSMOD,
    KITOS_CTL_INSMOD, KITOS_CTL_RMMOD, KITOS_CTL_MOUNT, KITOS_CTL_UMOUNT,
    KITOS_CTL_SELFTEST, KITOS_CTL_MEMTEST, KITOS_CTL_USERTEST, KITOS_CTL_OOMTEST,
    KITOS_CTL_PROCS, KITOS_CTL_GPIO, KITOS_CTL_STOP, KITOS_CTL_RESUME,
    KITOS_CTL_KILL, KITOS_CTL_TASK_SLEEP, KITOS_CTL_DISPLAY
};
struct kitos_dirent {
    unsigned long long size;
    unsigned int kind, reserved;
    char name[KITOS_NAME_MAX];
};
#endif
