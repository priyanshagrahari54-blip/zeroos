# Stage 1 validation record

## STAGE 1 CERTIFIED — scoped engineering acceptance

Certified code: **`cc38f8f7635ffbb7d3e0e0dd3016bbbb9a125460`**, 2026-09-20.
Both final workflows completed successfully:

- Push: [35509533259](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/35509533259)
- PR: [35509535607](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/35509535607)

All six recorded findings are closed for the scope below. Five native suites,
ELF boundary checks, nine fault/feature guests, the 60-second integration run,
and **seven CPU/RAM cases with 31 required checks each** passed. Both positive
PCID modes executed under KVM and were mandatory for a green final workflow.
This is ZEROOS's internal Stage-1 acceptance, not external certification,
production readiness or a claim of universal security. Documentation-only
follow-up commits do not change the certified executable code.

### Stage acceptance record

| Stage | Boundary | Implemented / tested result |
|---|---|---|
| 1.0 | Audit and first-principles design | Actual failures and ownership/transition rules recorded; no documentation-only feature credited as implemented |
| 1.1 | Boot/CPU/GDT/IDT/exceptions | Validated Multiboot2 input, NX requirement, exact frames/descriptors, distinct emergency stacks; fault guests passed |
| 1.2 | Physical memory | Reserved/allocated/claimed state, malformed/overlapping maps, rounding, exhaustion and failure boundaries passed |
| 1.3 | Virtual memory | Effective permissions, claims, protected kernel leaves and context activation/reuse passed |
| 1.4 | Kernel heap | Exact allocation identity, corruption bounds, initialization rollback, exhaustion and stress passed |
| 1.5 | Interrupts/timer | IRQ ownership, PIT ticks, bootstrap IRQ regression and normalized delivery passed |
| 1.6 | Existing scheduler | Cooperative/IRQ-exit preemption, register preservation, wait/sleep, guards and lifetime/reuse passed |
| 1.7 | Process/thread ownership | Distinct IDs/objects, parent link, atomic publication, rollback and terminal-thread reap passed |
| 1.8 | Isolated spaces | Same VA/different PA under live CR3, user data isolation and displaced-frame reuse passed |
| 1.9 | CPL3 | Actual user selectors/flags, TSS stack entry and fault containment passed |
| 1.10 | Syscall ABI | Five documented calls, register/RSP preservation, binary write bounds and invalid calls passed |
| 1.11 | User pointers | Whole-range canonicality, overflow, mapping and all-level permissions; no partial rejected copy |
| 1.12 | Security policy | Runtime W^X including aliases, integer-only state, safe returns, kernel/I/O/unsupported-entry denial passed |
| 1.13 | Integrated boot | Full required marker set and failure rejection passed in normal and matrix runs |
| 1.14 | Regression/CI/documentation | Bounded native, fault, stress and CPU/RAM tests; strict PCID coverage; truthful scope/evidence recorded |

## Closure evidence, 2026-09-20

- `8e876f2` implemented kernel/user/physical-alias W^X and integer-only state
  containment. Its run passed hardware fault regressions but exposed delayed
  terminal-thread reclamation during the extended user-fault sequence.
- `a04a15a` fixed terminal-thread reclamation and added CPU/RAM integration,
  explicit SYSENTER denial and PMM native tests. Push
  [35508727922](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/35508727922)
  and PR [35508729878](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/35508729878)
  passed the complete workflow and five TCG configurations.
- `dee4d13` fixed heap allocation identity. Against the old code, a second free
  of a coalesced block returned success and underflowed used-byte accounting to
  `18446744073709551584`. New native tests reject that stale header and forged
  payload headers and fail safely on corrupted chain sizes/canaries. Push
  [35508946160](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/35508946160)
  and PR [35508948944](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/35508948944)
  passed all five native suites, static checks, nine fault/feature guests,
  normal integration and the five-case TCG matrix.
- `f9b91d4` added displaced-frame identifier-reuse tests and actual KVM paths.
  Push [35509279217](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/35509279217)
  and PR [35509281487](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/35509281487)
  passed. Both **PCID with INVPCID** and **PCID without INVPCID** executed and
  passed under KVM. This closes the feature-coverage gap left by TCG.
- `cc38f8f` validates the complete boot memory map before releasing frames and
  gives reserved records precedence over overlapping available records.
  The new overlap fixture fails against the preceding implementation and
  passes after the fix. Malformed tags/end markers, overflow, rounding and
  physical-cap tests pass locally. PMM and heap suites also pass local
  ASan/UBSan builds. Both final workflows above passed this candidate. CI
  requires positive PCID coverage in both modes and explicitly checks
  synchronization, wait and sleep markers.

### Original finding disposition

| Finding | Resolution and evidence |
|---|---|
| PCID lifetime/reuse | Space-owned acquire/release; native exhaustion/reuse; actual KVM with/without INVPCID; displaced retired frames prevent false passes from physical-page reuse |
| Partial spawn rollback | Builder-vs-space transfer tracking; injected page/heap failures compare page/heap/task/PCID baselines; 64 capacity/drain rounds |
| Cross-space ownership | PMM exclusive claims reject reserved/free/already-owned frames and free-while-mapped; native and guest negative cases |
| Extended register state | Explicit integer-only ABI, CR0.TS held set; actual x87/MMX/SSE #NM containment; initial GPR scrub; no FP/vector support claim |
| Complete runtime W^X | Kernel RX/RO-NX/RW-NX, sealed executable physical aliases, protected root APIs; exact kernel #PF probes and CPL3 forbidden accesses |
| Adverse transitions/lifecycle/matrix | Bad RSP, privileged I/O, kernel reads, NX/code-write faults, invalid gate/SYSENTER, bootstrap IRQ regression, 64 execution/reap cycles, seven CPU/RAM cases |

### Final CPU/RAM matrix at `cc38f8f`

Every row passed all 31 required integration checks; no panic/triple fault was
accepted. TCG's `max` CPU did **not** provide PCID. KVM was available on the
hosted runner and supplied the two positive feature paths.

| Accelerator | CPU | RAM MiB | PCID | INVPCID |
|---|---|---:|---|---|
| TCG | qemu64 | 32 | off | unavailable |
| TCG | max | 128 | off | unavailable |
| TCG | max,pcid=off,invpcid=off | 512 | off | unavailable |
| TCG | qemu64 | 768 | off | unavailable |
| TCG | qemu64, repeated baseline | 128 | off | unavailable |
| KVM | host | 128 | enabled | available |
| KVM | host,invpcid=off | 128 | enabled | unavailable |

### Scope, not a production-readiness claim

The target is the single-CPU, integer-only, built-in-image Stage-1 foundation:
32–768 MiB tested RAM with a 512 MiB managed aperture, fixed resource limits,
PIC/PIT and architectural user/kernel isolation. Tests are bounded: 5 seconds
per native suite, 12 per fault guest, 45 per matrix guest, and a 60-second
normal integration window. CI archives diagnostics and posts bounded reports
on success as well as failure. Local guest tools were unavailable; the guest
claims above come from CI, not invented local runs.

No bare-metal/UEFI certification, SMP, floating-point contexts, loader/VFS/
drivers, indefinite soak, fairness/latency benchmark, speculative-execution
mitigation or universal memory-safety proof is claimed. Those are outside this
stage. See BUILD.md for reproducible commands and STAGE1_CLOSURE_DESIGN.md for
ZEROOS's ownership and boundary rationale.

---

## Historical audit and failure record

The sections below describe earlier commits, not unresolved findings in the
certified code. They are retained so failures and corrections remain
traceable instead of being rewritten as an uninterrupted success.

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

At this historical point, full user/kernel W^X, return-state validation and
extended-register ownership remained open. Later closure evidence is above.

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

### Findings recorded at `46b032a` (historical)

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
not `Stage 1 certified`. The current disposition and expanded evidence are
recorded at the top of this document; the original green run alone did not
close these findings.
