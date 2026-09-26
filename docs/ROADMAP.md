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
13. Forge AI bridge: sandboxed local kernel/user API and external-service boundary.
    Note: the ZEROOS AI platform broker (permission grants, backend
    selection with remote downgrade, wipe-on-drain, dormant-until-
    submit) is separate from Forge AI and already host-tested; this
    bridge item remains the future external boundary only.
14. SMP/per-CPU scheduling, high-resolution timers, advanced memory management.
15. Release engineering: reproducible builds, compatibility matrix, recovery media, long-duration stress and performance certification.

## Definition of done

Every stage follows:
AUDIT -> DESIGN -> IMPLEMENT -> TEST -> STRESS -> MEASURE -> DOCUMENT -> INTEGRATE

No roadmap item is considered implemented merely because its architecture is documented.
