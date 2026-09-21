# ZEROOS

ZEROOS is an original x86-64 operating system. It is built from first
principles as its own architecture: its subsystem boundaries, internal
APIs, object models, and process/thread model are ZEROOS-defined.
Established standards are used only at interoperability boundaries
(Multiboot2, the x86-64 instruction set, GRUB, and the SYSCALL/SYSRET
hardware mechanism). Everything ZEROOS controls is designed and
implemented by ZEROOS.

## Current stage: STAGE 1 CERTIFIED

**Stage 1 passed its scoped engineering acceptance** at code commit `cc38f8f`.
See [the validation record](docs/VALIDATION.md) for exact CI runs, closed
findings and limits. This is a single-CPU, integer-only foundation—not a
production-readiness or universal security claim.

Implemented subsystems include the physical allocator, VMM, kernel heap,
interrupt/timer and scheduler code, distinct process/thread objects,
per-process page-table roots, ring-3 entry, and the initial syscall set.
Implementation presence does not imply that every path is tested or secure.

The expanded suite now tests kernel/user/physical-alias W^X, exclusive page
ownership, failure-atomic process creation, terminal-thread reclamation,
integer-only state containment, invalid transitions and whole-range pointer
checks. Five native suites, nine fatal/feature guests, and seven CPU/RAM
configurations include actual KVM **PCID with and without INVPCID**. Both
[push](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/35509533259)
and [PR](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/35509535607)
workflows passed, including all 31 matrix checks per configuration.

Boot milestone messages report individual self-tests, not certification of
the entire kernel. No later-stage loader, driver, or application work should
be inferred from the existing foundation.

## Build

```sh
make            # builds build/zeroos.elf and a bootable build/zeroos.iso
make clean      # removes build artifacts
make host-test  # five bounded native regression suites
```

The build is a freestanding, no-stdlib `-Werror` compile with no external
dependencies beyond `gcc`, `ld`, and `grub-mkrescue` (for the ISO).

## Run

```sh
make run        # boots the ISO in QEMU with the serial console on stdio
```

Integration requires these messages among the complete checked set:

```
ZEROOS: heap self-test passed.
ZEROOS: live-CR3 isolation, ownership and W^X passed.
ZEROOS: address-space reuse with displaced frames passed.
ZEROOS: process rollback and lifetime stress passed.
ZEROOS: process execution/reap stress passed.
ZEROOS: SYSCALL/SYSRET syscall entry initialized.
ZEROOS: ring-3 user program executed.
ZEROOS: per-process address-space isolation verified.
ZEROOS: ring-3 page-fault containment verified.
ZEROOS: ring-3 general-protection containment verified.
ZEROOS: user pointer validation verified.
ZEROOS: ring-3 integration self-test passed.
```

CI also runs a bounded, deterministic boot self-test sequence
(positive, negative, boundary, and fault cases) in QEMU; see
`.github/workflows/build.yml` and [the build guide](docs/BUILD.md).

## Layout

- `boot/` — Multiboot2 entry, GDT/IDT setup, paging bootstrap.
- `kernel/` — PMM, VMM, heap, GDT/TSS, interrupts, scheduler, process,
  and syscall layers.
- `user/` — the built-in ring-3 user program used for the Stage-1
  integration tests.
- `docs/` — architecture and subsystem documentation.

## Documentation

- `docs/ARCHITECTURE.md` — subsystem map and current status.
- `docs/SYSCALL_ABI.md` — the system call ABI and user-pointer rules.
- `docs/VIRTUAL_MEMORY.md` — paging, per-process address spaces, PCID.
- `docs/INTERRUPTS.md` — IDT, IST, ring-3 containment, TSS RSP0.
- `docs/SCHEDULER.md` — task model, preemption, user-mode tasks.
- `docs/ROADMAP.md` — execution order and per-stage status.
