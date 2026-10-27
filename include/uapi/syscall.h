#ifndef UAPI_SYSCALL_H
#define UAPI_SYSCALL_H
#include <uapi/error.h>
/* KITOS ABI v1: AArch64 svc #0, x8 number, x0..x5 arguments, x0 result.
 * Negative uapi/error.h values on failure. Newlib wrappers set errno and return -1.
 * Integer and FP/SIMD registers are preserved across SVC and preemption. */
#define KITOS_SYS_EXIT 0
#define KITOS_SYS_WRITE 1
#define KITOS_SYS_YIELD 2
#define KITOS_SYS_SLEEP_MS 3
#define KITOS_SYS_GETPID 4
#define KITOS_SYS_CLOCK_MS 5
#define KITOS_SYS_READ 6
#define KITOS_SYS_OPEN 7
#define KITOS_SYS_CLOSE 8
#define KITOS_SYS_LSEEK 9
#define KITOS_SYS_FSTAT 10
#define KITOS_SYS_STAT 11
#define KITOS_SYS_ISATTY 12
#define KITOS_SYS_BRK 13
#define KITOS_SYS_SPAWN 14
#define KITOS_SYS_WAITPID 15
#define KITOS_SYS_UNLINK 16
#define KITOS_SYS_KILL 17
#define KITOS_SYSCALL_WRITE_MAX 4096
#endif
