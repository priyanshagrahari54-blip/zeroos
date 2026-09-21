ENTRY(_start)

SECTIONS
{
    . = 1M;
    __kernel_start = .;
    .multiboot2 : ALIGN(8) { KEEP(*(.multiboot2)) }

    . = ALIGN(4096);
    __kernel_text_start = .;
    .text : { *(.text*) }
    . = ALIGN(4096);
    __kernel_text_end = .;
    __kernel_ro_start = .;
    .rodata : { *(.rodata*) }
    /* Copied into private user pages, never executed through this alias. */
    .userinit : ALIGN(16) { *(.userinit*) }
    . = ALIGN(4096);
    __kernel_ro_end = .;
    .data : { *(.data*) }
    .bss : ALIGN(4096) { *(COMMON) *(.bss*) }
    . = ALIGN(4096);
    __kernel_end = .;

    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
