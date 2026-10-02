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
14. SMP/per-CPU scheduler reliability and support hardening, APIC clock-event evolution, advanced memory management. Per-CPU scheduling, remote TLB invalidation, and bounded CPU hot-offline are implemented and QEMU-tested; intermittent oversubscribed-q35 boot failures, broader device IRQ routing, hardware validation, and long-duration stress remain open.
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

**BLOCKED — production release evidence incomplete.** Host `make check` passes. Exact-SHA run [36976530176](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36976530176) on `1936a2bd` passed build, host release gate, image verification, Boot/SMP and q35 AHCI/NVMe two-boot persistence, with the credential success marker explicitly required across six guest boots. Three recent full-workflow passes do not establish a cause-specific fix or stable reliability; earlier q35/boot failures remain open. No physical G560 profile, real HDD/network/graphics/media/thermal measurement, 2 GB stress certification, signed update/recovery hardware test, or qualifying long-duration soak is available. The detailed gate/evidence matrix is in `STAGE_10_REPORT.md`; do not label Stage 10 COMPLETE.


## Stage 1–6 execution status (2026-10-02)

Stages 1–5 remain under hardening and evidence closure. Per the user's latest direction, Stage 6 kernel security implementation has begun in parallel; this does not waive earlier gates or authorize a Stage 6 production claim. Stage 7–10 work remains blocked until Stage 6 exits. Recent Stage 1 increments include atomic contiguous-page release, FPU/SSE context switching, VMM unmap ordering/reclamation, shared-MMIO lifetime, failed-protect permission isolation, and scheduler diagnostics. Runs 36858051968, 36860017977, 36861689464 and 36873757374 failed boot/storage gates; full-workflow runs 36876632155, 36975661257, 36976530176, 36980289437, 36980993553, 36981780463, 36982624279, 36983484109, 36983529324 and 36986917406 passed afterward. The FPU probe now covers all 16 XMM registers, all eight x87 stack entries, and MXCSR, with a CPU-1-pinned worker on AP systems and forced BSP timer-preemption coverage; exact-SHA run 36986917406 passed the earlier single-x87-value probe, and run 36999951281 passed the expanded all-eight-x87 probe through repeated 2-vCPU, 4-vCPU, and three q35 storage boots. Run 36981780463 and subsequent runs passed the expanded three-boot q35 AHCI/NVMe persistence gate; run 36983529324 additionally required the FPU completion marker on each q35 boot. This improves repeatability evidence but does not explain or close the earlier q35 reliability failures. The latest security increment adds a kernel `SET_CREDENTIALS` capability with locked credential transitions and irreversible self-drop; exact-SHA run 36976530176 passed with explicit marker checks across six guest boots. These are implementation increments, not Stage 1–5 completion or Stage 6 support/production claims. See `VALIDATION.md`, `STORAGE.md`, `SCHEDULER.md`, and `RELEASE_CERTIFICATION.md` for evidence.
