OUTPUT_ARCH(aarch64)
ENTRY(_start)
KERNEL_BASE = 0xffff000000000000;
PHDRS {
    boot PT_LOAD FLAGS(5); bootdata PT_LOAD FLAGS(6);
    text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0x80000;
    __kernel_physical_start = .;
    .boot : { KEEP(*(.reset)) } :boot
    . = ALIGN(4096);
    .boot_tables (NOLOAD) : {
        __boot_tables_start = .; . += 8K; __boot_tables_end = .;
    } :bootdata
    . += KERNEL_BASE;
    __kernel_start = KERNEL_BASE + __kernel_physical_start;
    __text_start = .;
    .text : AT(ADDR(.text) - KERNEL_BASE) {
        KEEP(*(.text.boot_high)) KEEP(*(.vector_table)) *(.text*)
    } :text
    . = ALIGN(4096); __text_end = .; __rodata_start = .;
    .rodata : AT(ADDR(.rodata) - KERNEL_BASE) { *(.rodata*) } :rodata
    . = ALIGN(4096); __rodata_end = .;
    .data : AT(ADDR(.data) - KERNEL_BASE) { *(.data*) } :data
    .bss (NOLOAD) : AT(ADDR(.bss) - KERNEL_BASE) {
        . = ALIGN(16); _bss_start = .;
        *(.bss* COMMON)
        . = ALIGN(16); _bss_end = .;
    } :data
    .stack (NOLOAD) : AT(ADDR(.stack) - KERNEL_BASE) {
        . = ALIGN(4096); __boot_stack_guard = .; . += 4K;
        _stack_start = .; . += 64K; _stack_end = .;
    } :data
    . = ALIGN(4096); __kernel_end = .;
    __kernel_physical_end = . - KERNEL_BASE;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
    ASSERT(__kernel_physical_end < 0x08000000, "kernel exceeds early boot RAM")
}
