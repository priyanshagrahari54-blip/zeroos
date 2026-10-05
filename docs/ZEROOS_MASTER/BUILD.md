# ZEROOS Build Guide

The first milestone uses GCC/binutils, GRUB Multiboot2 tooling, xorriso, and QEMU.

Build with:

    make

When the host does not have GRUB/xorriso installed, build and inspect only
the linked kernel ELF with:

    make elf

Run with:

    make run

Clean with:

    make clean

Run the host-testable kernel helpers, desktop cores, and compatibility tests
under AddressSanitizer and UndefinedBehaviorSanitizer with:

    make sanitize-check

This is an additional host memory-safety gate; it does not replace the
freestanding kernel build, QEMU certification, or real-hardware validation.

A feature is complete only when it has an appropriate build and verification path.
