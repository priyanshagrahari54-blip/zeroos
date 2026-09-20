# ZEROOS

ZEROOS is an original x86-64 operating system. It is built from first
principles as its own architecture: its subsystem boundaries, internal
APIs, object models, and process/thread model are ZEROOS-defined.
Established standards are used only at interoperability boundaries
(Multiboot2, the x86-64 instruction set, GRUB, and the SYSCALL/SYSRET
hardware mechanism). Everything ZEROOS controls is designed and
implemented by ZEROOS.

## Current stage: Stage 1 — ring-3 foundation

ZEROOS boots from Multiboot2 into a 64-bit protected kernel and provides:

- A physical page allocator over the first 512 MiB of RAM.
- A 4-level page table manager with a compact 2 MiB identity map for the
  kernel, 4 KiB fine-grained mapping, and 2 MiB huge-page splitting.
- A small kernel object heap with invariants, canary checks, and a boot
  self-test.
- **Per-process address spaces**: each process owns an independent PML4
  root, a PCID, and an exclusive set of physical pages. User pages are
  writable **or** executable, never both (W^X). Two processes may map the
  same virtual address to different physical pages, and the boot
  certification verifies isolation in both directions.
- **Genuine ring-3 user execution**: user code/data GDT segments, a TSS,
  per-task TSS RSP0, and IST-based double-fault/NMI delivery. User
  threads enter through an `iretq` at CPL 3 and are timer-preemptible.
- **A SYSCALL/SYSRET syscall ABI** (`docs/SYSCALL_ABI.md`) with a small,
  numbered, documented call set (exit, yield, write, getpid, gettid) and
  whole-range user-pointer validation before any user byte is copied.
- **User fault containment**: a page fault or general protection taken in
  user mode kills only the current process and the kernel keeps running.
  Kernel-mode exceptions remain fatal.

The kernel certifies itself at boot and prints a milestone line for each
verified subsystem over a serial console.

## Build

```sh
make            # builds build/zeroos.elf and a bootable build/zeroos.iso
make clean      # removes build artifacts
```

The build is a freestanding, no-stdlib `-Werror` compile with no external
dependencies beyond `gcc`, `ld`, and `grub-mkrescue` (for the ISO).

## Run

```sh
make run        # boots the ISO in QEMU with the serial console on stdio
```

A successful boot prints, among others:

```
ZEROOS: heap self-test passed.
ZEROOS: SYSCALL/SYSRET syscall entry initialized.
ZEROOS: ring-3 user program executed.
ZEROOS: per-process address-space isolation verified.
ZEROOS: ring-3 page-fault containment verified.
ZEROOS: ring-3 general-protection containment verified.
ZEROOS: user pointer validation verified.
ZEROOS: stage-1 ring-3 foundation certified.
```

The build also runs a bounded, deterministic boot self-test sequence
(positive, negative, boundary, and fault cases) in QEMU; see
`.github/workflows/build.yml`.

## Layout

- `boot/` — Multiboot2 entry, GDT/IDT setup, paging bootstrap.
- `kernel/` — PMM, VMM, heap, GDT/TSS, interrupts, scheduler, process,
  and syscall layers.
- `user/` — the built-in ring-3 user program used for the Stage-1
  certification.
- `docs/` — architecture and subsystem documentation.

## Documentation

- `docs/ARCHITECTURE.md` — subsystem map and current status.
- `docs/SYSCALL_ABI.md` — the system call ABI and user-pointer rules.
- `docs/VIRTUAL_MEMORY.md` — paging, per-process address spaces, PCID.
- `docs/INTERRUPTS.md` — IDT, IST, ring-3 containment, TSS RSP0.
- `docs/SCHEDULER.md` — task model, preemption, user-mode tasks.
- `docs/ROADMAP.md` — execution order and per-stage status.
