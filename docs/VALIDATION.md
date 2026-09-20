# Stage 1 validation record

Stage 1 is **not certified**. Source-level implementation and serial milestone
strings are not substitutes for hardware-exercised tests.

## Heap-boundary failure, 2026-09-20

CI run [35503459613](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/35503459613)
provides the decisive pre-fix trace:

- `CR3=0x1000`; the CPU and VMM root agree. Earlier claims of CR3 corruption
  came from incorrectly decoded debug-port output, not CPU evidence.
- `EFER=0x500`: long mode is active, but NXE (bit 11) is clear.
- PDE1 has bit 63 set. The first store at `CR2=0x200000` raises #PF with
  error `0xA` (write plus reserved-bit violation).
- The early IDT used 12-byte C records; the hardware indexes 16-byte gates.
  Delivery raises #GP, then #DF, then an explicit `Triple fault` in QEMU.

Fixes: check extended CPUID NX capability, enable/read back EFER.NXE before
installing NX mappings, fail closed without NX, use 16-byte early IDT records
with a compile-time assertion, and correct early exception RIP extraction.
The early IDT now precedes physical and virtual allocator initialization.
Temporary heap probes have been removed; the full-region payload test remains.

## Regression commands

- `make build/zeroos.elf`: warning-clean freestanding build (`-Werror`).
- `python3 tests/boot_regressions.py --static`: check ELF early IDT size,
  hardware-error-code stubs, and fatal C-entry frame offsets/alignment.
- `make && python3 tests/boot_regressions.py`: QEMU #UD and #PF injection
  images, asserting exact vector, error code, fault-instruction RIP and CR2;
  CPU-without-NX rejection; all guest runs bounded to 12 seconds.
- CI also boots the normal image for at most 60 seconds and checks the
  integration markers. A later subsystem failure still fails the job.

## Post-fix evidence

Run [35503604202](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/35503604202)
at commit `d12880b` passed the early exception/CPU-feature regression step and
normal-image heap, per-address-space VMM, and synchronization self-tests.
The next failure is #GP at LTR with error `0x28`: GDT initialization had loaded
the GDT address into IDTR using LIDT, leaving the bootstrap GDT active.
EFER is now `0xD00`, confirming NXE activation. This is progress, not a full
integration pass.

## GDT/TSS/IST follow-up

The hardware-boundary invariants are in [X86_BOUNDARY.md](X86_BOUNDARY.md).
Corrections cover LGDT/readback, user data/code descriptors, complete 104-byte
TSS including the I/O-map offset, the descriptor's upper base word, separate
DF/NMI stacks, and the real IST gate/frame format. Fault images exercise the
full #UD handler, software invocation of the NMI gate on IST2, and a real
#DF escalation on IST1. These tests assert frame placement inside the named
stack, not merely a printed vector. Guest results for this follow-up are
pending; compile-time assertions and ELF checks pass locally.

Full user/kernel W^X, syscall entry/return validation and per-thread extended
register ownership still require audit and tests. Stage 1 remains uncertified.
