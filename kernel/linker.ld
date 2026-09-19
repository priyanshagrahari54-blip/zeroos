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
