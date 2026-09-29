/* Embedded Ring-3 shell-child ELF (userspace/session/child.c).
 *
 * The shell reads this image through ZEROOS_SYS_CHILD_IMAGE and spawns it,
 * so the SPAWN/WAIT lifecycle is exercised by a real second process. The
 * image is copied to the caller unchanged; elf_load_image still validates
 * every header and enforces W^X before a single page is mapped. */
    .section .rodata
    .balign 16
    .globl shell_child_image_start
    .globl shell_child_image_end
shell_child_image_start:
    .incbin PROBE_PATH
shell_child_image_end:
    .section .note.GNU-stack, "", @progbits
