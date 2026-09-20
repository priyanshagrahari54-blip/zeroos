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
stack, not merely a printed vector. Run [35503752278](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/35503752278)
at `cc715c4` passed the complete exception/CPU-feature regression step. The
normal image passed GDT/TSS loading and reached CPL3; it then failed with
#DB because user RFLAGS was 0x102 (TF), not 0x202 (IF).

## Syscall boundary follow-up

Correct user initial IF/TF, the syscall frame/save/restore sequence, SFMASK
bits, return-state validation, binary write length, and full-range parent
permissions. Host tests and ELF entry-shape checks pass locally. The user
program now asserts CPL3, IF, no TF, preserved GPRs/RSP and call results.
Run [35503986373](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/35503986373)
at `96bb637` passed host and exception regressions and the CPL3/syscall ABI
marker. It then exposed an orchestration bug: waiting for any zombie lets
process A satisfy process B's wait before B has executed. The test now waits
for its own PID while retaining A for the isolation comparison.

A separate host regression caught and fixes removal of the wrong ownership
list node (the old code freed the successor instead of the removed node).
The final integration marker has been renamed to a self-test pass, not a
claim of Stage 1 certification. Temporary PMM/heap progress probes are gone.

Full user/kernel W^X, syscall entry/return validation and per-thread extended
register ownership still require audit and tests. Stage 1 remains uncertified.

## First complete green integration run

Run [35504197236](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/35504197236)
at commit `46b032a` completed successfully. Its steps passed:

- Three bounded host suites: user-range permissions/boundaries/stress,
  syscall dispatch/binary writes/invalid return containment, ownership-list
  removal/failure handling.
- ELF checks: early IDT size, error-code stubs and fatal-frame ABI;
  GDT/TSS/emergency-stack sizes and instructions; syscall save/restore
  shape; general-register-only kernel image.
- Six bounded guest cases: early #UD, early #PF, full-IDT #UD, software
  invocation of the NMI gate on IST2, actual #DF on IST1, no-NX rejection.
- Normal boot integration: heap/VMM, GDT/TSS/IDT, timer-only preemption,
  zombie/reuse stress, CPL3/syscall register ABI, per-process data isolation,
  contained #PF/#GP, and negative user-pointer checks.

The full workflow took 3m4s; that is CI duration, not a kernel performance
measurement. The green result applies to this specific QEMU configuration,
not a hardware matrix or proof of security.

### Open certification findings

1. `process_spawn` reserves a PCID before `vmm_space_create` clears the
   field; destruction then clears PCID state before the process layer can
   release it. Lifetime/reuse and PCID-capable CPU tests are still needed.
2. Partial process allocation/map failures can leak pages not yet transferred
   to the address space. Fault-injected rollback accounting is missing.
3. Ownership lists do not yet reject the same page owned by a different
   space at the VMM API boundary; process-side checks alone are insufficient.
4. Kernel C no longer clobbers SIMD registers, but arbitrary user extended
   state is not isolated or explicitly disabled per thread.
5. User leaf W^X checks exist, but the first kernel 2 MiB and writable
   physical aliases of executable user pages prevent a complete W^X claim.
6. More adverse user-transition, process-lifecycle and CPU/RAM configuration
   coverage is required before certifying the complete Stage 1 scope.

The boot message deliberately says `ring-3 integration self-test passed`,
not `Stage 1 certified`. These findings must be fixed and tested, not waived
because the existing integration suite is green.
