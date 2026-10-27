#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H
#include <dos/type.h>
int64_t syscall_dispatch(uint64_t number, const uint64_t arguments[6]);
#endif
