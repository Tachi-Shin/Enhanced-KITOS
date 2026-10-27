OUTPUT_ARCH(aarch64)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); stack PT_GNU_STACK FLAGS(6); }
SECTIONS {
    . = 0x400000;
    .text : { KEEP(*(.text.start)) *(.text*) *(.init) *(.fini) } :text
    . = ALIGN(4096);
    .rodata : { *(.rodata*) *(.eh_frame*) *(.gcc_except_table*) } :rodata
    . = ALIGN(4096);
    .data : {
        *(.data*)
        . = ALIGN(8); __preinit_array_start = .; KEEP(*(.preinit_array*)) __preinit_array_end = .;
        __init_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .;
        __fini_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*))) KEEP(*(.fini_array)) __fini_array_end = .;
    } :data
    .bss (NOLOAD) : { *(.bss* COMMON) } :data
    /DISCARD/ : { *(.comment) *(.note*) }
}
