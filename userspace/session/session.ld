/* Static ET_EXEC at the ZEROOS user base; separate R-X and RW segments
 * (W^X). Must stay below the user stack page at base + 0x1ff000. */
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
    ASSERT(. < 0x00007f00001ff000, "session image overlaps the user stack page")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
