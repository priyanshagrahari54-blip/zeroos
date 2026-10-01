# ZEROOS MASTER PROMPT — 10-STAGE EXECUTION

You are an engineering agent working on the existing ZEROOS repository:
https://github.com/priyanshagrahari54-blip/zeroos

Treat the repository code and its living documents as the source of truth for implementation status. Do not rebuild ZEROOS from scratch and do not replace existing architecture merely for convenience.

## Mission

Build ZEROOS into a real, native x86-64, production-oriented desktop operating system that is:
- lightweight and responsive on old hardware;
- measurable rather than marketing-driven;
- secure by architecture;
- fast under HDD, low-RAM and thermal constraints;
- capable of a modern original desktop;
- able to provide Windows and Android compatibility as isolated user-space layers;
- able to support modern graphics/media/gaming only where the hardware and tested runtime permit;
- deeply integrated with ZERO AI without making AI a kernel dependency.

## Non-negotiable product requirements

1. Native x86-64 OS. No Linux skin, simulator, mock UI or toy implementation.
2. Correctness before optimization; every performance claim needs a benchmark.
3. Event-driven execution over polling.
4. Installed != loaded != running != active.
5. Dormant services/runtimes consume no continuous work when unused.
6. Minimize CPU wakeups, RAM residency, disk seeks, network chatter and unnecessary GPU rendering.
7. Literal zero resource usage for active work is impossible; target minimum useful work and near-zero unnecessary idle/background overhead.
8. HDD-first optimization: sequential I/O, request merging, batching, read-ahead, metadata locality, bounded queues and cache-aware behavior.
9. Native G560 reference profile: 1366x768 display and old Intel/NVIDIA-class hardware; use the actual detected configuration for certification.
10. 1080p content should be decoded/scaled efficiently when the real hardware supports it; do not pretend native panel resolution is 1080p.
11. Animated wallpaper must stop rendering when hidden/covered/fullscreen/locked and adapt under resource/thermal pressure.
12. Wi-Fi/Ethernet targets are measured against the physical adapter/link; never promise 100 Mbps independent of hardware.
13. Security is first-class: user/kernel isolation, capabilities, permissions, sandboxing, NX/W^X/ASLR/stack protection, signed packages/updates, secure boot/TPM where available, encrypted storage/secrets, firewall and recovery.
14. Windows/Android compatibility layers are isolated, resource-controlled user-space components, never kernel dependencies.
15. ZERO AI is only the ZEROOS-specific personal AI service. Forge AI is separate and must not be made a ZEROOS dependency.
16. ZERO AI uses a permission broker and action executor; it never receives unrestricted kernel access.
17. Controller/gamepad support is out of scope unless explicitly re-authorized.
18. No unsupported capability may be presented as implemented.
19. Host-tested core != live hardware support.
20. Detection != operational support.

## Engineering workflow

For every task:
AUDIT -> DESIGN -> IMPLEMENT -> TEST -> STRESS -> MEASURE -> DOCUMENT -> INTEGRATE

Before changing code:
- inspect current HEAD and relevant files;
- inspect current tests/CI;
- identify existing contracts and invariants;
- preserve working architecture;
- identify exact dependency blockers.

After implementation:
- run the narrowest relevant tests;
- run integration/QEMU tests;
- run stress/fault tests;
- collect resource/performance evidence;
- update the authoritative docs;
- record unsupported paths explicitly.

## 10 stages

### Stage 0 — Reproducible engineering
CI, deterministic builds, warnings/errors, test harnesses, release artifacts, evidence discipline.

### Stage 1 — Kernel execution
Scheduler/context lifecycle, interrupt-frame ownership, preemption, wait/sleep/wake, reaping, SMP/per-CPU foundations, stress certification.

### Stage 2 — Userspace execution
Process/thread split, address spaces, ring-3, syscall ABI, ELF/init, safe user copies, IPC, handles, credentials, resource limits.

### Stage 3 — Persistent storage
VFS, block layer, filesystem, journaling/crash consistency, page cache, HDD scheduling, async/direct/buffered I/O, checksums, snapshots, encryption and recovery.

### Stage 4 — Hardware and networking
Device/bus/driver model, PCI/ACPI, DMA/MMIO/IRQ, USB/HID where supported, Ethernet/Wi-Fi framework, IPv4/IPv6, ARP/ND, UDP/TCP/DNS/DHCP, firewall binding, audio, ACPI power/thermal.

### Stage 5 — Desktop platform
Framebuffer/accelerated display abstraction, compositor, surfaces, window manager, input routing, session, shell services, search, notifications, settings, diagnostics, performance center, accessibility.

### Stage 6 — Security/update/recovery
Real privilege/capability enforcement, sandboxing, firewall packet-path binding, package signing, transactional updates, snapshots/rollback, recovery environment, safe mode and security testing.

### Stage 7 — GPU/media/browser/native apps
GPU driver abstraction, accelerated backend/Vulkan where supported, damage/occlusion/frame pacing, hardware video decode, 1080p playback, browser engine integration, native app framework and core apps.

### Stage 8 — Compatibility/gaming
Windows PE/Win32/Win64 layers, graphics translation where tested, Android AOSP 14/API 34 isolated runtime, app/package permissions, gaming performance governor. Publish only tested compatibility matrices. No controller support.

### Stage 9 — ZERO AI/ecosystem/performance
Permission-brokered ZERO AI actions, automation, search/diagnostic context, dormant model lifecycle, service governor, observability, low-overhead background architecture, benchmark/regression system.

### Stage 10 — Certification/release
Lenovo G560-class hardware certification, actual 2 GB-class profile when applicable, 1366x768, HDD, 1080p media where supported, actual network link measurements, thermal/power, long-duration soak, recovery, security, compatibility and release gates.

## ZERO AI contract

request -> permission broker -> context provider -> backend selector -> inference -> action executor -> audit/result

Required:
- explicit permission grants;
- bounded queue;
- cancellation;
- dormant-until-submit;
- local backend when available;
- explicit remote egress permission;
- sensitive buffer lifecycle/wipe;
- destructive action confirmation;
- no kernel privilege;
- complete operation with ZERO AI disabled.

## Resource/thermal contract

Use:
- tickless/event-driven operation where beneficial;
- adaptive CPU scheduling;
- lazy page allocation/reclaim;
- shared read-only memory and COW;
- HDD-aware queues;
- bounded cache/writeback;
- damage tracking/occlusion;
- hardware decode;
- background suspension;
- thermal-aware governors;
- measured wakeup/I/O/GPU budgets.

Never trade correctness or security for a cosmetic performance number.

## Evidence standard

For each claimed feature record:
- implementation status;
- exact source path(s);
- test command;
- CI/QEMU result;
- hardware result if any;
- measured CPU/RAM/I/O/GPU/network/thermal data when relevant;
- unsupported conditions;
- recovery behavior.

Use ADOPT / BENCHMARK / PROTOTYPE / OPTIONAL / REJECT for researched technologies. The two external AI research outputs are evidence inputs, not authorities.

## Required documents

Keep synchronized:
- ZEROOS_MASTER_BLUEPRINT.md
- ZEROOS_MASTER_ROADMAP.md
- ROADMAP.md
- BLUEPRINT_VERIFICATION.md
- ARCHITECTURE.md
- HARDWARE.md
- VALIDATION.md
- this master prompt

Never let the roadmap claim more than the code and validation evidence support.

## Current execution rule

Execution remains in Stages 1–5 until each applicable earlier-stage implementation and evidence gate is closed to the required production-readiness standard. Do not begin Stage 6 before that gate is met. Once Stages 1–5 are closed, proceed through Stages 6–10 in order. If later-stage work exposes an earlier-stage dependency, stop and resolve that dependency first; never bypass scheduler, userspace, storage, driver, or security invariants to advance the stage number.

When a stage is completed, record:
1. what changed;
2. what was tested;
3. what was measured;
4. what remains unsupported;
5. which document sections were updated;
6. the commit SHA.

