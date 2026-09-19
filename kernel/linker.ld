ENTRY(_start)

SECTIONS
{
    . = 1M;

    .multiboot2 : ALIGN(8)
    {
        KEEP(*(.multiboot2))
    }

    .text : ALIGN(16) { *(.text*) }
    .rodata : ALIGN(16) { *(.rodata*) }
    .data : ALIGN(16) { *(.data*) }
    .bss : ALIGN(16) { *(COMMON) *(.bss*) }

    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.comment*)
        *(.note*)
    }
}
