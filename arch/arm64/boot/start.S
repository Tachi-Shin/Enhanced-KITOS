/* Firmware loads at physical 0x80000 with MMU and D-cache off. Only this
 * trampoline has a low VMA. Secondary cores remain parked. */
#include <arch/arm64/mmu.h>
.section .reset,"ax",@progbits
.global _start
_start:
    mov x20, x0                     // Preserve DTB (or QEMU ATAG) address
    mrs x0, mpidr_el1
    and x0, x0, #0xff
    cbnz x0, boot_halt
    msr daifset, #0xf
    mrs x0, CurrentEL
    cmp x0, #8
    b.eq boot_el2
    cmp x0, #4
    b.eq boot_el1
boot_halt:
    wfe
    b boot_halt
boot_el2:
    mov x0, #(1 << 31)              // HCR_EL2.RW
    msr hcr_el2, x0
    mov x0, #3
    msr cnthctl_el2, x0
    msr cntvoff_el2, xzr
    msr cptr_el2, xzr
    adr x0, boot_el1
    msr elr_el2, x0
    mov x0, #0x3c5                  // EL1h, DAIF masked
    msr spsr_el2, x0
    eret
boot_el1:
    msr spsel, #1
    mrs x0, sctlr_el1
    tbnz x0, #0, boot_halt          // Reject unsupported MMU-on handoff
    tbnz x0, #2, boot_halt          // D-cache must be clean/off
    adr x0, boot_vectors
    msr vbar_el1, x0
    mov x0, #(3 << 20)             // FP/SIMD saved in the exception frame
    msr cpacr_el1, x0
    isb
    msr cntkctl_el1, xzr            // User counter/timer access via syscall
    ldr x1, =__boot_tables_start
    ldr x2, =__boot_tables_end
1:  stp xzr, xzr, [x1], #16
    cmp x1, x2
    b.lo 1b
    ldr x1, =__boot_tables_start    // L0 -> L1
    add x2, x1, #4096
    orr x3, x2, #3
    str x3, [x1]
    // Temporary Normal RAM 0..3GiB, Device nGnRnE 3..4GiB; caches off.
    mov x3, #0x701                 // AF, inner-shareable, L1 block
    str x3, [x2]
    mov x4, #0x40000000
    orr x3, x3, x4
    str x3, [x2, #8]
    mov x3, #0x80000000
    orr x3, x3, #0x700
    orr x3, x3, #1
    str x3, [x2, #16]
    ldr x3, =0x00600000c0000405     // PXN, UXN, AF, AttrIdx=1
    str x3, [x2, #24]
    dsb sy
    mov x0, #0xff                  // MAIR[0]=Normal WBWA, [1]=Device nGnRnE
    msr mair_el1, x0
    // 48-bit VA, 4KiB TG0/TG1, WBWA inner-shareable walks, 40-bit PA.
    ldr x0, =0x00000002b5103510
    msr tcr_el1, x0
    msr ttbr0_el1, x1              // Temporary identity map
    msr ttbr1_el1, x1              // KERNEL_VA_BASE + PA
    isb
    tlbi vmalle1
    dsb sy
    isb
    ic iallu
    dsb sy
    isb
    mrs x0, sctlr_el1
    bic x0, x0, #(1 << 1)
    bic x0, x0, #(1 << 12)         // I-cache off until final tables
    bic x0, x0, #(1 << 19)         // WXN set with final permissions
    bic x0, x0, #(1 << 9)          // Trap EL0 DAIF access (UMA=0)
    bic x0, x0, #(1 << 14)         // Trap EL0 DC ZVA
    bic x0, x0, #(1 << 15)         // Trap EL0 CTR access
    bic x0, x0, #(1 << 16)         // Trap EL0 WFI
    bic x0, x0, #(1 << 18)         // Trap EL0 WFE
    bic x0, x0, #(1 << 26)         // Trap EL0 cache-maintenance instructions
    orr x0, x0, #1
    orr x0, x0, #(1 << 3)          // Aligned kernel/user stack pointers
    orr x0, x0, #(1 << 4)
    msr sctlr_el1, x0
    isb
    ldr x0, =high_entry
    br x0
.balign 2048
boot_vectors:
.rept 16
    b boot_halt
    .balign 128
.endr
.section .text.boot_high,"ax",@progbits
high_entry:
    ldr x0, =_stack_end
    mov sp, x0
    ldr x0, =vector_table
    msr vbar_el1, x0
    isb
    mov x0, x20
    bl main
2:  wfe
    b 2b
