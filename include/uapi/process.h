#ifndef UAPI_PROCESS_H
#define UAPI_PROCESS_H
/* Wire flags and layouts are independent of Newlib's C structs and flags. */
#define KITOS_OPEN_READ 1
#define KITOS_OPEN_WRITE 2
#define KITOS_OPEN_CREATE 4
#define KITOS_OPEN_EXCL 8
#define KITOS_OPEN_TRUNC 16
#define KITOS_OPEN_APPEND 32
#define KITOS_KIND_REGULAR 1
#define KITOS_KIND_DIRECTORY 2
#define KITOS_KIND_CHAR 3
#ifndef __ASSEMBLER__
struct kitos_stat { unsigned long long inode, size; unsigned int kind, reserved; };
_Static_assert(sizeof(struct kitos_stat) == 24, "stat wire layout");
#endif
#endif
