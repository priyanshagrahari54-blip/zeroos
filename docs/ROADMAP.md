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
    Status: framebuffer display primitive (kernel) + `DISPLAY_PRESENT`
    pixel-mapping syscall + PS/2 keyboard/mouse input stack
    (IRQ1/IRQ12, scancode + mouse-packet decoders, INPUT_POLL/WAIT) +
    desktop platform core (compositor/window/input-router/search/
    settings/notify/a11y/i18n/watchdog modules) landed in Stage 5
    batch 1–3; display service, shell processes and app UI remain.
11. System services: package/update/recovery/diagnostics/observability/power management.
12. Study Center and app framework.
13. Forge AI bridge: sandboxed local kernel/user API and external-service boundary.
14. SMP/per-CPU scheduling, high-resolution timers, advanced memory management.
15. Release engineering: reproducible builds, compatibility matrix, recovery media, long-duration stress and performance certification.

## Definition of done

Every stage follows:
AUDIT -> DESIGN -> IMPLEMENT -> TEST -> STRESS -> MEASURE -> DOCUMENT -> INTEGRATE

No roadmap item is considered implemented merely because its architecture is documented.
