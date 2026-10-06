# ZEROOS Build Guide

The first milestone uses GCC/binutils, GRUB Multiboot2 tooling, xorriso, and QEMU.

## Toolchain

A **bootable** ISO needs GRUB's El Torito boot image. Install it before
building a release image:

    sudo apt-get install grub-pc-bin grub-common xorriso mtools qemu-system-x86

Without it `make iso` **fails on purpose**. The previous fallbacks silently
produced an image that tooling accepted and firmware could not boot: the naive
`xorriso -as mkisofs` branch passed no boot options, and the old
`tools/build_iso.py` wrote no El Torito boot record at all and flattened
`boot/zeroos.elf` into `BOOT_ZEROOS.ELF;1` in the root directory, so even a
loaded GRUB could not have found `/boot/grub/grub.cfg`. Neither path exists
any more.

## Build

    make              # production image -> build/zeroos.iso
    make elf          # linked kernel ELF only (no ISO tooling required)
    make iso-test     # testing-mode image -> build-test/zeroos.iso
    make clean        # removes both trees

Every `iso` build finishes by running `verify-iso`, which re-reads the image
and checks the ECMA-119 structures (PVD, terminator, root directory, required
files) and the El Torito boot record/catalog. When `pycdlib` is installed it
also cross-checks the directory tree with that third-party parser, which is
how a one-byte PVD date offset and a pre-LBA path table were caught.

    make verify-iso                     # validate an existing image
    pip install pycdlib                 # enable the independent cross-check

On a host with no GRUB, `ZEROOS_ISO_ALLOW_UNBOOTABLE=1` builds a data-only
image for ISO-layout work. `verify-iso` then reports the layout as valid and
states plainly that the image will not boot:

    make ZEROOS_ISO_ALLOW_UNBOOTABLE=1 iso

## The two images

`build/zeroos.iso` is the production image. The embedded Ring-3 session starts
and blocks on the native input wait path, so the desktop stays alive.

`build-test/zeroos.iso` is the testing image: the same sources built with
`ZEROOS_BOOT_CERTIFICATION`, so the session runs its finite
display/compositor/input probes and exits instead of staying interactive. Boot
this one for automated or repeatable testing; boot the production image for a
persistent desktop.

The two flavours must never share object files. `make check` builds the
certification flavour into `build/`, and Make keys rebuilds on timestamps
alone, so without the compile-flag fingerprint in `$(BUILD)/.compile-flags` a
later `make iso` relinked the certification objects into an image labelled
production. Verified before the fix: `build/session_launch.o` still contained
the certification-only string `session did not finish` after a
certification-free `make iso`. Switching flavour now rebuilds once; repeating
the same flavour rebuilds nothing.

## Run

    make run          # production image in QEMU
    make run-test     # testing image in QEMU

## Real boot test

    make boot-test    # needs qemu-system-x86_64

`tools/boot_test.sh` boots the image in QEMU and asserts the guest serial log
against `tools/boot_milestones.txt`: 91 required milestones plus two negative
gates (`storage.*FAILED`, `ZEROOS PANIC:`). Three boots are required because
the interrupt-frame ownership defect this gates against was nondeterministic.

This is the only gate in the project that executes kernel code. Everything
else - the host suites, the stage certifications, the link-time checks - can
pass on a kernel that hangs during boot.

The milestone list is shared with CI's Boot test step, which now calls the same
script, so local runs and CI cannot drift. To check the assertion logic on a
host without an emulator, point it at a captured log:

    make ZEROOS_SERIAL_LOG=path/to/serial.log boot-test

A failed run lists every missing milestone rather than just the first, so the
output shows how far the kernel got before it stopped.

## Kernel header gate

Every kernel link runs two checks over the produced ELF:

    kernel-simd-check      no x87/MMX/SSE/AVX instruction in the image
    make verify-multiboot2 Multiboot2 header conforms to the specification

`verify-multiboot2` validates magic, architecture, the checksum arithmetic,
8-byte alignment, containment in a `PT_LOAD` segment inside the 32 KiB window a
bootloader searches, tag-list termination, and agreement between the
framebuffer tag and `grub.cfg`. A header defect passes every host test and then
fails at boot, so it is checked on every link.

## Full gate suite

    make check

`make check` adds `ZEROOS_BOOT_CERTIFICATION`, so the embedded session exits
after its probes and the CI harness can reap it. This keeps certification
deterministic without making the normal OS session ephemeral.

`make -j check` is safe: the stage certification gates declare the targets
that produce the artifacts they execute, so parallel make cannot start a gate
before its test binaries or the linked kernel exist.

The gates honour `BUILD=`, so `make BUILD=build-fault check` certifies the
tree you asked for rather than a stale `./build`.

### What the gates actually execute

Stages 1-5 used to be `grep` checks over source files: a marker string in a
comment or an unused branch was enough to pass. They now also verify that the
required symbols are present in the **linked kernel image** and run the real
host suites:

| Stage | Executable evidence |
|---|---|
| 1 scheduler | `scheduler_*`/`task_block` present in the linked ELF |
| 2 userspace | `abi_consistency.py`, ABI compile check, `elf_load_image`/`ipc_*` linked |
| 3 storage | `host_selftest.sh`: builds a GPT/ZJFS image, round-trips data, corrupts the superblock, confirms fsck recovery |
| 4 hardware | `display-core-test`, `input-core-test`, `usb-core-test`, `driver-core-test`, `dma-test` |
| 5 desktop | `desktop-tests` (the full compositor/UI/session suite) |

Each gate fails with a stated reason. Verified: removing a test binary fails
stage 4 with `test binary not built`, and pointing a gate at an ELF without
the scheduler symbols fails stage 1 listing every missing symbol.

### The release gate

`stage10-certification-release` runs `tools/g560_benchmark.py`, which compares
every declared figure against its numeric target and exits non-zero when one is
out of range. It previously printed `(target 100-200 MB: PASS)` from an f-string
without ever comparing anything, and stamped all ten subsystems `CERTIFIED`
unconditionally.

Supply real observations to get a certification rather than a consistency
check:

    ZEROOS_MEASUREMENTS=measurements.json make stage10-certification-release

Without it the report records `measurement_provenance: declared`, the
subsystems are stamped `GATE PASSED (host-side checks only, not measured on
hardware)`, and `overall_readiness` reads `TARGETS CONSISTENT, NOT CERTIFIED`.

## Installing alongside another OS (dual boot)

ZeroOS is a self-contained Multiboot2 kernel: the storage probe and the Ring-3
session are embedded in the ELF, so it needs no initrd, no root partition and
no bootloader of its own. The supported dual-boot route is therefore a GRUB
menu entry on the machine you already boot:

    make iso iso-test
    sudo tools/install/dualboot_install.sh --dry-run    # preview
    sudo tools/install/dualboot_install.sh              # install

The script writes `/boot/zeroos/zeroos.elf` (plus `zeroos-test.elf` for the
testing entry), installs `/etc/grub.d/40_zeroos` with `multiboot2` entries, and
runs `update-grub`. It requires GRUB >= 2.02, refuses to run without it, and
never touches a partition table. Roll back with:

    sudo tools/install/dualboot_install.sh --uninstall

A feature is complete only when it has an appropriate build and verification path.
