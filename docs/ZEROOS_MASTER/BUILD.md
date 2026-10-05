# ZEROOS Build Guide

The first milestone uses GCC/binutils, GRUB Multiboot2 tooling, xorriso, and QEMU.

Build with:

    make

When the host does not have GRUB/xorriso installed, build and inspect only
the linked kernel ELF with:

    make elf

Run with:

    make run

The normal image is a persistent desktop/session build: after the kernel
certification gates, the Ring-3 session remains alive and blocks on the native
input wait path instead of exiting. This is the production boot path.

Run the full finite certification suite with:

    make check

make check adds ZEROOS_BOOT_CERTIFICATION, so the embedded session exits
after its display/compositor/input probes and the CI harness can reap it.
This keeps certification deterministic without making the normal OS session
ephemeral.

Clean with:

    make clean

Run the host-testable kernel helpers, desktop cores, and compatibility tests
under AddressSanitizer and UndefinedBehaviorSanitizer with:

    make sanitize-check

This is an additional host memory-safety gate; it does not replace the
freestanding kernel build, QEMU certification, or real-hardware validation.

A feature is complete only when it has an appropriate build and verification path.
