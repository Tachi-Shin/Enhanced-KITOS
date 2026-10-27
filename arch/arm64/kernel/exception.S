#include <arch/arm64/kernel/exception.h>
.section .vector_table,"ax"
.global vector_table
.balign 2048
vector_table:
.macro vector target
    b \target
    .balign 128
.endm
    vector fatal_entry              // Current EL, SP0
    vector fatal_entry
    vector fatal_entry
    vector fatal_entry
    vector sync_entry               // Current EL, SPx
    vector irq_entry
    vector fatal_entry
    vector fatal_entry
    vector sync_entry               // Lower EL, AArch64
    vector irq_entry
    vector fatal_entry
    vector fatal_entry
    vector fatal_entry              // AArch32 unsupported
    vector fatal_entry
    vector fatal_entry
    vector fatal_entry
.size vector_table, . - vector_table

.section .text.exception,"ax"
.balign 4
.macro save_context
    sub sp, sp, #EXCEPTION_FRAME_SIZE
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    mrs x0, sp_el0
    mrs x1, elr_el1
    stp x0, x1, [sp, #FRAME_SP]
    mrs x0, spsr_el1
    mrs x1, esr_el1
    stp x0, x1, [sp, #FRAME_PSTATE]
    mrs x0, far_el1
    str x0, [sp, #FRAME_FAR]
    stp q0, q1, [sp, #288]
    stp q2, q3, [sp, #320]
    stp q4, q5, [sp, #352]
    stp q6, q7, [sp, #384]
    stp q8, q9, [sp, #416]
    stp q10, q11, [sp, #448]
    stp q12, q13, [sp, #480]
    stp q14, q15, [sp, #512]
    stp q16, q17, [sp, #544]
    stp q18, q19, [sp, #576]
    stp q20, q21, [sp, #608]
    stp q22, q23, [sp, #640]
    stp q24, q25, [sp, #672]
    stp q26, q27, [sp, #704]
    stp q28, q29, [sp, #736]
    stp q30, q31, [sp, #768]
    mrs x0, fpcr
    mrs x1, fpsr
    str x0, [sp, #FRAME_FPCR]
    str x1, [sp, #FRAME_FPSR]
.endm

irq_entry:
    save_context
    bl irq_dispatch
    b exception_return
sync_entry:
    save_context
    mov x0, sp
    bl sync_dispatch
    b exception_return
fatal_entry:
    save_context
    mov x0, sp
    bl exception_fatal
1:  wfe
    b 1b

exception_return:
    // IRQs must remain masked while restoring the exception return registers.
    ldp q0, q1, [sp, #288]
    ldp q2, q3, [sp, #320]
    ldp q4, q5, [sp, #352]
    ldp q6, q7, [sp, #384]
    ldp q8, q9, [sp, #416]
    ldp q10, q11, [sp, #448]
    ldp q12, q13, [sp, #480]
    ldp q14, q15, [sp, #512]
    ldp q16, q17, [sp, #544]
    ldp q18, q19, [sp, #576]
    ldp q20, q21, [sp, #608]
    ldp q22, q23, [sp, #640]
    ldp q24, q25, [sp, #672]
    ldp q26, q27, [sp, #704]
    ldp q28, q29, [sp, #736]
    ldp q30, q31, [sp, #768]
    ldr x0, [sp, #FRAME_FPCR]
    ldr x1, [sp, #FRAME_FPSR]
    msr fpcr, x0
    msr fpsr, x1
    ldp x0, x1, [sp, #FRAME_SP]
    msr sp_el0, x0
    msr elr_el1, x1
    ldr x0, [sp, #FRAME_PSTATE]
    msr spsr_el1, x0
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #EXCEPTION_FRAME_SIZE
    eret

.global arch_enter_user
.type arch_enter_user, %function
arch_enter_user:
    msr daifset, #2
    sub sp, sp, #EXCEPTION_FRAME_SIZE
    mov x9, sp
    mov x10, #(EXCEPTION_FRAME_SIZE / 16)
2:  stp xzr, xzr, [x9], #16
    subs x10, x10, #1
    b.ne 2b
    str x2, [sp]                    // x0 argument; all other GPRs are zero
    str x1, [sp, #FRAME_SP]
    str x0, [sp, #FRAME_PC]
    mov x9, #0x340                  // EL0t; IRQ enabled; D/A/F masked
    str x9, [sp, #FRAME_PSTATE]
    b exception_return
.size arch_enter_user, . - arch_enter_user
