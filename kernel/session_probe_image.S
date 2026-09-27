/* Embedded Ring-3 session/shell process ELF (userspace/session/session.c).
 * elf_load_image validates the image and maps copies of its segments
 * into the session process. */
    .section .rodata
    .balign 16
    .globl session_probe_image_start
    .globl session_probe_image_end
session_probe_image_start:
    .incbin PROBE_PATH
session_probe_image_end:
    .section .note.GNU-stack, "", @progbits
