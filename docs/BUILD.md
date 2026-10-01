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

Intermittent SMP/storage failures (repeated-boot stress):

    LIMINE_DIR=/path/to/limine-v8-binary python3 tools/stress/mkiso-limine.py build/zeroos.elf build/zeroos-limine.iso
    tools/stress/boot-loop.sh smp4|smp2|storage build/zeroos-limine.iso 100

The loop boots sequentially and stops QEMU as soon as a boot completes or
panics. Plain boots are checked against every Boot-test serial pattern in
`.github/workflows/build.yml`; storage boots use fresh ZJFS disks and the CI
persistence markers. Failing serial logs are kept under `build/stress-*`.
Requires `pycdlib` and the Limine v8 binary release (BIOS CD files). The
default `qemu-system-x86_64` can be overridden with `QEMU=`.

A feature is complete only when it has an appropriate build and verification path.
