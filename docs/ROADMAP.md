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
    batch 1–3; the embedded session process (`userspace/session/`) now
    wires display service + compositor + input router to the live
    syscalls with boot certification; ZERO Bar and launcher cores
    (layout/click/focus, registry/query/launch), browser tab
    lifecycle, navigation controller and notification/search/settings
    surfaces are host-tested in Stage 5 batches 4–7; shell chrome
    rendering and app UI remain.  Batches 8–11 added the clipboard
    service, downloads manager, built-in search providers
    (commands/diagnostics), the measured performance centre, the
    terminal core (streaming escape parser, scrollback ring), the
    file manager (source-injected listings, path history,
    permission-gated ops) and the shell overview/expose grid model —
    all host-tested.
11. System services: package/update/recovery/diagnostics/observability/power management.
    Status: RFC 8439 ChaCha20-Poly1305 primitives (block, cipher, MAC,
    key generation, AEAD) landed in `kernel/crypto.c`, linked into the
    kernel and verified against the published RFC vectors plus
    tamper/wrong-key negatives (`make hardware-core-test`); AEAD vault,
    transactional update state machine (section-20 pipeline with
    rollback), snapshot manager, firewall policy engine and sandbox
    profiles are host-tested in Stage 5 batches 4–7.  Batches 8–10
    added AEAD update payload verification (version-as-AAD, tamper/
    replay/wrong-key negatives), the update<->snapshot production
    binding with the previously-unwired stage_apply/capture hooks
    restored fail-closed, and the privacy centre aggregation
    (sandbox/firewall/media/eco/clipboard counters -> documented
    risk ladder).  Kernel packet-path binding and enforcement hooks
    follow.
12. Study Center and app framework.
    Status: flashcard/spaced-repetition scheduler and focus sessions
    host-tested (Stage 5); the PDF subset contract (page tree, Tj/TJ
    extraction, explicit -95 for filters/encryption) landed in batch
    9; the formula engine (bounded parser/evaluator), the
    OCR capability contract (argument validation, honest -95 with no
    engine linked), the Study notes notebook and the dictionary
    (injected source, case-insensitive lookup/suggestions) landed in
    batch 11; only the app framework UI follows.
14. Desktop app/ecosystem policy cores: media lawful-source gates,
    gaming profiles/cooperative yield, cloud-device offline-first sync,
    availability+lifecycle contract for terminal/file-manager.
    Status: media (default-deny origins, DRM refusal with no bypass
    path), gaming (profiles, remap, yield matrix), and cloud/device
    (pairing grants nothing, offline queue, per-bit permissions)
    host-tested in Stage 5 batch 10; transports and hardware bindings
    follow.
13. AI remains optional and isolated; no external development-agent bridge is part of ZEROOS.
    Note: the ZEROOS AI platform broker (permission grants, backend
    selection with remote downgrade, wipe-on-drain, dormant-until-
    submit) is already host-tested and follows this rule.
14. SMP/per-CPU scheduling, high-resolution timers, advanced memory management.
15. Release engineering: reproducible builds, compatibility matrix, recovery media, long-duration stress and performance certification.

## Definition of done

Every stage follows:
AUDIT -> DESIGN -> IMPLEMENT -> TEST -> STRESS -> MEASURE -> DOCUMENT -> INTEGRATE

No roadmap item is considered implemented merely because its architecture is documented.


## 10-stage execution bridge

Stages 0-5 describe the existing implementation and test foundations; they are not a blanket production certification. The next five stages are:

6. Security enforcement, packages, updates and recovery.
7. GPU acceleration, media, browser and native app foundation.
8. Windows compatibility, Android runtime foundation and gaming.
9. ZERO AI, automation, service lifecycle and performance engineering.
10. G560/hardware certification, long-duration stability and release.

The detailed authoritative definitions are in `ZEROOS_MASTER_ROADMAP.md`. The reusable agent prompt is `ZEROOS_MASTER_PROMPT_10_STAGE.md`.

All stage claims remain evidence-gated: detection != operational support, host-tested != hardware-supported, roadmap intent != implementation.


## Stage 10 status (2026-10-01)

**BLOCKED — production release evidence incomplete.** Host `make check` and exact-SHA GitHub QEMU/CI pass (latest VMM table-lifetime run 36857163730), but there is no physical G560 profile, real HDD/network/graphics/media/thermal measurement, 2 GB stress certification, signed update/recovery hardware test, or qualifying long-duration soak. The detailed gate/evidence matrix is in `STAGE_10_REPORT.md`; do not label Stage 10 COMPLETE based on the CI pass.


## Stage 1–5 execution status (2026-10-01)

Execution remains in Stages 1–5; later stages must not start until the applicable earlier-stage implementation and evidence gates are closed. Recent Stage 1 increments include atomic contiguous-page release, FPU/SSE context switching, VMM unmap ordering/reclamation, shared-MMIO lifetime, and failed-protect permission isolation. Recent failed-protect and unlink-before-free fixes have host-build and exact-SHA QEMU evidence (runs 36855983261 and 36857163730); these are increments, not a Stage 1 completion claim. Remaining hardware, concurrency, and stage-specific acceptance gates are tracked in `VALIDATION.md`, `RELEASE_CERTIFICATION.md`, and the detailed roadmap.
