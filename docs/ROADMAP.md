# ZEROOS Execution Roadmap

This file is the short execution bridge referenced by BLUEPRINT_VERIFICATION.md.
The detailed product plan remains in ZEROOS_MASTER_ROADMAP.md.

## Execution order

1. Scheduler certification: timer-only preemption, mixed cooperative/preemptive transitions, callee-saved register preservation, wait/sleep interaction, lifecycle/reaping, idle fallback.
2. Kernel memory hardening: allocator invariants, page metadata, mapping ownership, fault diagnostics, guard pages where practical.
3. Process/thread split: kernel task abstraction, process container, per-process address spaces, kernel/user stack separation.
4. User-mode transition: GDT/TSS design, ring-3 entry/return, syscall entry ABI, validation and fault containment.
5. ELF loader and init: executable validation, segment mapping, user stack, argc/argv, first user process.
6. Syscall layer: handles, memory, process/thread, time, synchronization, IPC, diagnostics.
7. VFS and storage: block-device contract, buffer/cache policy, filesystem abstraction, initial filesystem, mount lifecycle.
8. Driver model: device ownership, IRQ registration, DMA-safe interfaces, input, display, storage.
9. Networking: packet buffers, NIC abstraction, Ethernet/ARP/IP/UDP/TCP foundations, sockets.
10. Graphics/UI: framebuffer, compositor, windows, input routing, terminal, settings, file manager.
11. System services: package/update/recovery/diagnostics/observability/power management.
12. Study Center and app framework.
13. Forge AI bridge: sandboxed local kernel/user API and external-service boundary.
14. SMP/per-CPU scheduling, high-resolution timers, advanced memory management.
15. Release engineering: reproducible builds, compatibility matrix, recovery media, long-duration stress and performance certification.

## Execution status (Stage 1)

**STAGE 1 CERTIFIED** for the bounded single-CPU, integer-only foundation at
`cc38f8f`, with both strict CI workflows green. This supersedes earlier
unsupported "Done" labels with executed evidence. Exact runs, acceptance for
1.0–1.14, closed findings and limitations are in VALIDATION.md.

| Item | State | Evidence / remaining work |
|---|---|---|
| Boot/CPU/GDT/IDT/exceptions | Implemented and exercised | Exact early/full fault frames, independent IST stacks, real #DF, no-NX rejection; complete boot-map hardening passed strict CI |
| PMM/VMM/heap | Implemented and exercised | Claims, W^X aliases, live CR3/displaced-frame reuse, rollback, forged/coalesced frees; native sanitizer runs |
| Scheduler | Expanded suite passed | Cooperative/IRQ-exit switching, timer-only preemption, wait/sleep, guards, bootstrap IRQ, lifetime/slot reuse |
| Process/thread split | Expanded suite passed | Separate identity/ownership, publication/rollback, optional PCID ownership, immediate terminal-thread reclamation |
| CPL3/syscall ABI | Expanded suite passed | Stable five-call ABI, register/flags checks, full-range pointers, bad RSP, integer-only policy and forbidden-access/entry tests |
| CPU/RAM matrix | Seven configurations passed at cc38f8f | TCG 32/128/512/768 MiB; actual KVM PCID with and without INVPCID |
| Full Stage 1 | Certified within documented scope | Five native suites, nine fault guests, normal integration, seven matrix cases with 31 checks each; both PCID modes required |
| Later stages | Deferred | No loader/VFS/driver/application/SMP implementation as part of this task |

## Definition of done

Every stage follows:
AUDIT -> DESIGN -> IMPLEMENT -> TEST -> STRESS -> MEASURE -> DOCUMENT -> INTEGRATE

No roadmap item is considered implemented merely because its architecture is documented.
