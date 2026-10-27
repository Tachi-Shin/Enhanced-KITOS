#ifndef ARCH_ARM64_EXCEPTION_H
#define ARCH_ARM64_EXCEPTION_H
#define EXCEPTION_FRAME_SIZE 816
#define FRAME_FP 288
#define FRAME_FPCR 800
#define FRAME_FPSR 808
#define FRAME_SP 248
#define FRAME_PC 256
#define FRAME_PSTATE 264
#define FRAME_ESR 272
#define FRAME_FAR 280
#ifndef __ASSEMBLER__
#include <dos/type.h>
struct exception_frame {
    uint64_t x[31];
    uint64_t sp, pc, pstate, esr, far;
    uint64_t q[32][2], fpcr, fpsr;
};
_Static_assert(sizeof(struct exception_frame) == EXCEPTION_FRAME_SIZE, "exception frame size");
_Static_assert(__builtin_offsetof(struct exception_frame, sp) == FRAME_SP, "exception frame SP");
_Static_assert(__builtin_offsetof(struct exception_frame, far) == FRAME_FAR, "exception frame FAR");
extern char vector_table[];
void arch_exception_init(void);
void irq_dispatch(void);
void sync_dispatch(struct exception_frame *frame);
void exception_fatal(struct exception_frame *frame) __attribute__((noreturn));
#endif
#endif
