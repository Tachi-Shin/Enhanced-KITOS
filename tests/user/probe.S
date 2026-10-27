/* Position-independent EL0 fixture, copied into a private RX page. This is not
 * a loader or a C library. All OS interaction uses the documented SVC ABI. */
#include <uapi/syscall.h>
#include "layout.h"
.section .rodata.user_probe,"a",@progbits
.balign 16
.global user_probe_start, user_probe_end
user_probe_start:
    mov x19, x0
    and w0, w0, #255
    cbz w0, normal
    cmp w0, #7
    b.eq busy
    cmp w0, #5
    b.eq privileged
    cmp w0, #3
    b.eq nx
    // Kernel supplies the chosen fault address in the private data page.
    ldr x1, =USER_TEST_DATA
    ldr x1, [x1, #32]
    cmp w0, #2
    b.eq fault_write
    cmp w0, #4
    b.eq fault_write
    cmp w0, #10
    b.eq fault_exec
    ldr x2, [x1]
    b fail
fault_write:
    str xzr, [x1]
    b fail
fault_exec:
    br x1
privileged:
    mrs x1, sctlr_el1
    b fail
nx:
    ldr x1, =USER_TEST_DATA
    ldr w2, =0xd65f03c0             // ret; execution must be denied
    str w2, [x1]
    br x1
busy:
    lsr x19, x19, #32
    ldr x1, =USER_TEST_DATA
    str x19, [x1]
1:  ldr x2, [x1, #8]
    add x2, x2, #1
    str x2, [x1, #8]
    ldr x3, [x1]
    cmp x3, x19
    b.ne fail
    b 1b

.macro expect_error value
    cmn x0, #\value
    b.ne fail
.endm
.macro write_args address, length
    mov x0, #1
    ldr x1, =\address
    mov x2, #\length
    mov x8, #KITOS_SYS_WRITE
    svc #0
.endm
normal:
    // A new task starts with a clean register file, including callee-saved GPRs.
    // x19 was used to decode the argument above; x1..x18/x20..x30 are still zero.
    orr x1, x1, x2
    orr x1, x1, x3
    orr x1, x1, x18
    orr x1, x1, x20
    orr x1, x1, x30
    cbnz x1, fail
    mov x8, #KITOS_SYS_GETPID
    svc #0
    cmp x0, #0
    b.le fail
    mov x8, #255
    svc #0
    expect_error KITOS_ENOSYS
    mov x8, #KITOS_SYS_GETPID
    svc #1
    expect_error KITOS_ENOSYS
    mov x0, #99
    mov x1, #0
    mov x2, #1
    mov x8, #KITOS_SYS_WRITE
    svc #0
    expect_error KITOS_EBADF
    write_args 0, 1
    expect_error KITOS_EFAULT
    write_args 0xffff000000084000, 1
    expect_error KITOS_EFAULT
    write_args 0xffffffffffffffff, 8
    expect_error KITOS_EFAULT
    write_args USER_TEST_DATA + 8192 - 4, 8
    expect_error KITOS_EFAULT                 // Entire range checked before output
    write_args USER_TEST_DATA, 4097
    expect_error KITOS_EINVAL
    write_args 0, 0                 // Empty write does not dereference a pointer
    cbnz x0, fail
    write_args USER_TEST_DATA + 4092, 9
    cmp x0, #9
    b.ne fail
    mov x8, #KITOS_SYS_CLOCK_MS
    ldr x0, =USER_TEST_CODE          // Copyout must refuse RX code
    svc #0
    expect_error KITOS_EFAULT
    ldr x0, =USER_TEST_DATA + 8192 - 4
    svc #0
    expect_error KITOS_EFAULT
    ldr x0, =USER_TEST_DATA + 4092   // Valid unaligned copyout across two pages
    svc #0
    cbnz x0, fail
    ldr x0, =USER_TEST_DATA
    svc #0
    cbnz x0, fail
    ldr x1, =USER_TEST_DATA
    ldr x19, [x1]
    mov x8, #KITOS_SYS_SLEEP_MS
    mov x0, #25
    svc #0
    cbnz x0, fail
    mov x8, #KITOS_SYS_CLOCK_MS
    ldr x0, =USER_TEST_DATA
    svc #0
    cbnz x0, fail
    ldr x1, =USER_TEST_DATA
    ldr x1, [x1]
    sub x1, x1, x19
    cmp x1, #25
    b.lo fail
    mov x8, #KITOS_SYS_SLEEP_MS
    mov x0, #0x100000000
    svc #0
    expect_error KITOS_EINVAL
    // Keep every non-result GPR live over timer preemption and an SVC yield.
    .irp reg,1,2,3,4,5,6,7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
        mov x\reg, #\reg
    .endr
    mov x8, #KITOS_SYS_YIELD
    ldr x0, =20000000
2:  subs x0, x0, #1
    b.ne 2b
    svc #0
    cbnz x0, fail
    .irp reg,1,2,3,4,5,6,7,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
        cmp x\reg, #\reg
        b.ne fail
    .endr
    cmp x8, #KITOS_SYS_YIELD
    b.ne fail
    // SP_EL0 also survives IRQs and task switches.
    mov x1, sp
    ldr x2, =USER_TEST_STACK_TOP
    cmp x1, x2
    b.ne fail
    mov x0, #1
    adr x1, success_text
    mov x2, #(success_end - success_text)
    mov x8, #KITOS_SYS_WRITE
    svc #0
    cmp x0, #(success_end - success_text)
    b.ne fail
    mov x0, #0
    b finish
fail:
    mov x0, #101
finish:
    mov x8, #KITOS_SYS_EXIT
    svc #0
3:  b 3b
success_text:
    .ascii "[EL0] syscall/register/sleep PASS\n"
success_end:
.balign 8
.ltorg
user_probe_end:
