.section .text.start,"ax"
.global _start
.type _start,%function
_start:
    mov x29, xzr
    mov x30, xzr
    ldr x19, [sp]                   // argc
    add x20, sp, #8                 // argv
    add x21, x20, x19, lsl #3
    add x21, x21, #8                // envp
    adrp x0, environ
    add x0, x0, :lo12:environ
    str x21, [x0]
    bl __libc_init_array
    mov x0, x19
    mov x1, x20
    mov x2, x21
    bl main
    bl exit
1:  wfe
    b 1b
.size _start,.-_start
.global _init, _fini
_init:
    ret
_fini:
    ret
