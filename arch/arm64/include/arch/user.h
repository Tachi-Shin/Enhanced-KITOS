#ifndef ARCH_USER_H
#define ARCH_USER_H
#include <dos/type.h>
#define ARCH_ELF_MACHINE 183
/* Enter AArch64 EL0 with clean integer registers, x0=argument and IRQ enabled.
 * No return. The current task already owns a kernel stack and TTBR0. */
void arch_enter_user(uint64_t entry, uint64_t stack, uint64_t argument) __attribute__((noreturn));
#endif
