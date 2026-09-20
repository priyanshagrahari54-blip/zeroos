ENTRY(_start)

SECTIONS
{
    . = 1M;
    __kernel_start = .;

    .multiboot2 : ALIGN(8)
    {
        KEEP(*(.multiboot2))
    }

    .text : ALIGN(16) { *(.text*) }
    .rodata : ALIGN(16) { *(.rodata*) }
    /* The ring-3 user program: one contiguous executable blob copied into
     * each process's user code page. Kept apart from kernel sections so
     * the [user_init_code, user_init_end) range contains nothing else. */
    .userinit : ALIGN(16) { *(.userinit*) }
    .data : ALIGN(16) { *(.data*) }
    .bss : ALIGN(16) { *(COMMON) *(.bss*) }

    __kernel_end = .;

    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.comment*)
        *(.note*)
    }
}
