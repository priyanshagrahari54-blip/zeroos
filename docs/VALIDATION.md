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

Local build and static checks passed for these fixes. QEMU is not installed in
the sandbox, and package downloads are unavailable; guest tests run in CI.
Their post-fix results must be recorded before claiming a boot milestone.

## Remaining audit findings

The later GDT/TSS and interrupt code still needs correction and execution
validation: GDT loading uses LIDT, user descriptors have wrong access bytes,
TSS layout is incorrect, and IST handling assumes a nonexistent pushed index.
These findings are separate from the now-identified heap-boundary failure.
Full user/kernel W^X and Stage 1 security certification remain outstanding.
