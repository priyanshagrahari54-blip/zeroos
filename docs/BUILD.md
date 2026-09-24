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

A feature is complete only when it has an appropriate build and verification path.
