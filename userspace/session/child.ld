/* Static ET_EXEC at the ZEROOS user base; separate R-X and RW segments
 * (W^X). Same layout as session.ld: the data segment must follow the text
 * segment through the location counter, because ADDR(.text) would resolve
 * to 0 for the renamed .text.child section and produce a PT_LOAD at
 * virtual address zero, which the kernel's ELF validator rejects.
 * Must stay below the user stack page at base + 0x1ff000. */
ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0x00007f0000000000;
    .text : { *(.text.start) *(.text .text.*) *(.rodata .rodata.*) } :text
    /* Explicit address: with an empty .bss the linker would otherwise emit
     * the data PT_LOAD at virtual address zero. */
    . = 0x00007f0000002000;
    .data : { *(.data .data.*) } :data
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    ASSERT(. < 0x00007f0000010000, "child image exceeds its 64 KiB window")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
