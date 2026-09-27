/* Embedded Ring-3 storage probe ELF (userspace/storage/probe.c). The
 * kernel copies nothing out of it directly: elf_load_image validates the
 * image and maps copies of its segments into the probe process. */
    .section .rodata
    .balign 16
    .globl storage_probe_image_start
    .globl storage_probe_image_end
storage_probe_image_start:
    .incbin PROBE_PATH
storage_probe_image_end:
    .section .note.GNU-stack, "", @progbits
