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
#define KITOS_SYS_INPUT_READ 18
#define KITOS_SYS_READDIR 19
#define KITOS_SYS_MKDIR 20
#define KITOS_SYS_RMDIR 21
#define KITOS_SYS_RENAME 22
#define KITOS_SYS_FTRUNCATE 23
#define KITOS_SYS_CHDIR 24
#define KITOS_SYS_GETCWD 25
#define KITOS_SYS_CONTROL 26
#define KITOS_SYS_TTY_INFO 27
#define KITOS_SYS_PIPE 28
#define KITOS_SYS_CHANNEL 29
#define KITOS_SYS_DUP 30
#define KITOS_SYS_FCNTL 31
#define KITOS_SYS_SPAWN_EX 32
#define KITOS_SYS_POLL 33
#define KITOS_SYS_LISTEN 34
#define KITOS_SYS_CONNECT 35
#define KITOS_SYS_ACCEPT 36
#define KITOS_SYS_SHM_OPEN 37
#define KITOS_SYS_SHM_MAP 38
#define KITOS_SYS_SHM_UNMAP 39
#define KITOS_SYS_EVENT 40
#define KITOS_SYS_SHM_UNLINK 41
#define KITOS_SYS_DISPLAY 42
#define KITOS_SYSCALL_WRITE_MAX 4096
#endif
