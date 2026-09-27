/* Static ET_EXEC at the ZEROOS user base; separate R-X and RW segments
 * (W^X). Each process has its own address space, so the child shares the
 * session's base without conflicting with it. */
ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0x00007f0000000000;
    .text : { *(.text.start) *(.text .text.*) *(.rodata .rodata.*) } :text
    . = ALIGN(4096);
    .data : { *(.data .data.*) } :data
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    ASSERT(. < 0x00007f00001ff000, "child image overlaps the user stack page")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
