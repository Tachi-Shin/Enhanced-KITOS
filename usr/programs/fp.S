#include <uapi/syscall.h>
.text
.global fp_probe
.type fp_probe,%function
fp_probe:
    sub sp, sp, #192
    stp x19, x20, [sp]
    stp x29, x30, [sp, #16]
    stp q8, q9, [sp, #32]
    stp q10, q11, [sp, #64]
    stp q12, q13, [sp, #96]
    stp q14, q15, [sp, #128]
    mrs x9, fpcr
    mrs x10, fpsr
    stp x9, x10, [sp, #160]
    mov x19, x0
    and x12, x19, #3
    lsl x12, x12, #22
    and x13, x19, #1
    msr fpcr, x12
    msr fpsr, x13
    .irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
        dup v\reg\().2d, x19
    .endr
    mov x20, #5
1:  ldr x10, =4000000
2:  subs x10, x10, #1
    b.ne 2b
    mov x8, #KITOS_SYS_YIELD
    svc #0
    cbnz x0, fail
    .irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
        umov x11, v\reg\().d[0]
        cmp x11, x19
        b.ne fail
        umov x11, v\reg\().d[1]
        cmp x11, x19
        b.ne fail
    .endr
    mrs x11, fpcr
    cmp x11, x12
    b.ne fail
    mrs x11, fpsr
    cmp x11, x13
    b.ne fail
    subs x20, x20, #1
    b.ne 1b
    mov x0, #1
    b done
fail:
    mov x0, #0
done:
    ldp x9, x10, [sp, #160]
    msr fpcr, x9
    msr fpsr, x10
    ldp q8, q9, [sp, #32]
    ldp q10, q11, [sp, #64]
    ldp q12, q13, [sp, #96]
    ldp q14, q15, [sp, #128]
    ldp x19, x20, [sp]
    ldp x29, x30, [sp, #16]
    add sp, sp, #192
    ret
.size fp_probe,.-fp_probe
