# ZEROOS External Reference Audit — 2026-10-26

## Scope and evidence

This audit covers the PDF references surfaced in the 2026-10-26 review. The Library contains duplicate copies of the **100 Low-Level System Optimizations Catalog**, the **120 Low-Level System, Network & Browser Optimization Catalog**, the **Operating Systems Ultimate Encyclopedia**, and the **Operating Systems Absolute Omega Vault**. The same-day repository upload commit also added **Extreme Hardware Optimization & Low-Level OS Engineering Manual.pdf** and **Master 100+ Hardware, Kernel & Virtual VRAM Handbook.pdf**. The latter two are identified from the repository upload commit by filename, but their PDF body text was not available through the text extraction used in this pass, so their contents are not represented as fully audited. The distinct text-audited reference families are:

1. *Operating Systems Ultimate Encyclopedia* — a broad directory of systems projects, references, and engineering topics.
2. *Operating Systems Absolute Omega Vault* — a broad OS engineering/reference catalog.
3. *100 Low-Level System Optimizations Catalog* — storage, memory, scheduling, CPU, network, IPC, and hardware tuning ideas.
4. *120 Low-Level System, Network & Browser Optimization Catalog* — overlapping low-level tuning plus network and browser optimization ideas.

The catalogs are idea inventories, not verified patches or authoritative tuning recipes. A listed feature is **not** evidence that ZEROOS implements it, that its source code is mature, or that it is safe on every machine. This audit separates reuse candidates, concepts to adapt, unsafe defaults, and items requiring measurement. No external project code is imported by this document.

## Executive decision

Use a **selective reuse + ZEROOS-native policy** model:

- Reuse mature, compatible, license-approved implementations where their interfaces and dependency footprint fit ZEROOS.
- Adapt proven algorithms and design patterns behind ZEROOS APIs instead of copying Linux sysctl values, systemd units, Linux kernel modules, or browser extensions directly.
- Build ZEROOS-specific lifecycle/resource policy, capability security, diagnostics, UI/compositor policy, and AI permission brokerage.
- Do not import entire Linux kernel subsystems or claim feature support from a catalog entry alone.
- Every adopted item needs provenance/license review, a build integration plan, a feature gate, regression tests, fault tests, and before/after measurements.

## Classification

### A. Adopt or prototype early (after checking the current implementation)

| Area | Candidate | ZEROOS treatment | Acceptance evidence |
|---|---|---|---|
| HDD I/O | BFQ-like latency/budget scheduling, bounded read-ahead, foreground I/O priority, write coalescing | Adapt policies for rotational HDDs; retain a simple deadline/fairness fallback | Interactive latency, throughput, starvation, queue-depth and writeback measurements |
| Memory pressure | LZ4-class fast compression, zram-style compressed backing, reclaim priorities, discardable caches | Implement as bounded memory-pressure policy; never assume compression gives a fixed multiplier | Resident RAM, compression CPU cost, reclaim latency, OOM and thrash stress |
| Storage cache | Demand-loaded file cache, bounded dirty-page/writeback queues, metadata locality | Keep journal/order guarantees; make cache limits hardware-profile-aware | Crash/power-cut tests, data integrity, dirty-page ceiling, HDD I/O trace |
| Scheduler | Fair eligibility/deadline concepts, priority inheritance where needed, foreground/background weights | Adapt concepts to ZEROOS scheduler; do not transplant Linux EEVDF implementation blindly | Latency, fairness, starvation, context-switch and SMP stress |
| Networking | Bounded socket buffers, TCP congestion-control research, packet batching/offload when hardware supports it | Implement per-interface capability detection; use safe protocol defaults | Loss/latency/throughput tests across loopback and emulated NICs |
| Browser lifecycle | Lazy image/iframe loading, bounded JS heaps, background throttling, bfcache-like state retention, tab freeze/discard | Apply in a future browser engine/process model; preserve site/process isolation | Memory per tab, resume correctness, web compatibility and security tests |
| Video | Hardware decode, frame pacing, bounded 2–4-frame queue where appropriate, direct scanout/overlay when supported | Capability-detect codec/profile/driver; prefer supported H.264 path on legacy hardware | Sustained playback, dropped frames, A/V sync, decoder/CPU/thermal metrics |
| Boot | Compressed read-only system image, lazy optional services, parallel independent init | Keep core boot path deterministic and recovery-capable | Reproducible ISO, boot milestones, cold-boot and failure-injection tests |
| IPC | Shared-memory fast paths and bounded rings where they reduce copies safely | Preserve capability checks, quotas, cancellation and backpressure | Copy/latency benchmarks, malformed-message and resource-exhaustion tests |
| Observability | Pressure/stall accounting, bounded logs, per-subsystem metrics | Build low-overhead event-driven metrics with opt-in sampling | Idle wakeups and CPU overhead measured; log size bounded |

### B. Adapt only with explicit hardware/security gates

- **zram / compressed swap:** strong candidate for 2 GB RAM, but only after allocator, page ownership, swap backing, reclaim, and OOM paths are sound. Compression can consume CPU and must not cause swap storms.
- **tmpfs:** only for small, explicitly bounded temporary data. Do not put the full browser profile/cache in RAM by default on a 2 GB machine; use a bounded disk cache with hot-item caching.
- **Read-ahead and cache sizing:** adaptive to sequential access, RAM pressure, and rotational storage. Never use fixed 4 MiB read-ahead or large caches universally.
- **TCP BBR / Fast Open / QUIC / HTTP/3:** implement only with a complete protocol implementation and interoperability tests. Do not force protocols; negotiate and fall back.
- **Hardware offloads (TSO/GSO/GRO, DMA):** use only when the driver advertises support and tests verify descriptor, alignment, ownership, and cache-coherency rules.
- **KSM/page dedup:** optional later; scanning burns CPU and introduces timing/security considerations. Prefer shared read-only pages and copy-on-write first.
- **Huge pages / THP:** opt-in for workloads that benefit; small-memory desktops and fragmentation can lose more than they gain.
- **CPU affinity, IRQ pinning, core isolation, tickless mode, real-time scheduling:** workload-specific advanced controls, not defaults for older dual-core G560 hardware. Measure interrupt latency, fairness, thermals, and power.
- **CPU frequency/turbo/C-states/PCIe ASPM/SATA ALPM:** let a capability-aware power governor choose balanced policy. Disabling power saving globally can increase heat, fan noise, battery drain, and sometimes reduce sustained performance.
- **Compiler LTO/PGO/vectorization:** use reproducible release builds and per-target feature detection. Never emit AVX-512 instructions for a generic legacy x86-64 baseline; the G560 cannot be assumed to support modern vector extensions.
- **Browser process model:** memory reduction must not weaken site isolation. Prefer lifecycle freezing and bounded caches before merging unrelated sites into one process.
- **DNS encryption:** DoH/DoT is a privacy/security choice with fallback and policy controls, not an optimization to disable globally.
- **WebRTC:** user-controlled privacy permission and per-app policy; do not disable globally because some calls and conferencing depend on it.
- **Telemetry/logging:** no unbounded background polling, but keep security audit records and actionable crash diagnostics within bounded storage.

### C. Reject as unsafe, misleading, or not portable as a default

1. data=writeback and long journal commit intervals as blanket speed tweaks: they can widen the data-loss window. ZEROOS must prioritize filesystem integrity and document any durability mode explicitly.
2. Unswappable, unbounded ramfs/RAM-only caches: they can exhaust scarce RAM and cause system-wide failure.
3. “0 ms reload”, fixed 3:1/4:1 compression, “CPU 0–5%”, “double/triple RAM”, or “zero software overhead” as guarantees: these are workload- and hardware-dependent marketing claims, not acceptance criteria.
4. Forcing HTTP/3/QUIC, TCP Fast Open, BBR, or a single codec: can break compatibility or fail to improve the actual path. Negotiate and measure.
5. Disabling KPTI/speculation mitigations, watchdogs, firewall protections, TLS checks, or privacy protections solely for speed: not acceptable as default policy.
6. Blindly disabling PCIe/SATA power management or forcing maximum CPU frequency: not acceptable as default; it may worsen thermals, power and sustained performance.
7. systemd, udev, sysctl, Linux kernel modules, cgroups v2, io_uring, and Linux-specific queue knobs as direct drop-ins: ZEROOS is not Linux. Reuse source only after API/ABI, license, architecture, and dependency audit.
8. AV1/VP9 hardware decode claims on the G560 without confirming the exact GPU/driver/codec profile. The baseline certification target remains hardware-supported H.264/AVC 1080p30, measured on the actual device.
9. Disabling the watchdog or removing diagnostics to improve benchmark numbers.
10. Treating the encyclopedia's broad project list or placeholder-style repeated catalog entries as validated source-quality evidence.

## Hardware-aware ZEROOS policy

### Lenovo G560 / 2 GB profile

- Keep the minimum boot and desktop path resident; make browser engines, AI models, compatibility runtimes, media stacks, indexers and update engines demand-loaded.
- Prefer small bounded caches, compressed reclaim only after pressure, sequential HDD reads, write coalescing with durable flush ordering, and foreground I/O priority.
- Avoid background prefetch/indexing when the HDD is busy or RAM is tight.
- Keep animations and compositor work event-driven; do not add polling loops for metrics, Dynamic Capsule, widgets or idle effects.
- Treat 1080p H.264/AVC at 30 FPS as a **hardware certification target**, not a proven result until sustained real-device tests pass.
- Treat 100 Mbps Ethernet and 802.11n Wi-Fi figures as physical link-rate ceilings/nominal PHY rates, not guaranteed application throughput. ZEROOS cannot software-optimize past the NIC/radio, access point, channel, or ISP limit.
- Detect the exact GPU, NIC, Wi-Fi, SATA controller, audio, Bluetooth and thermal interfaces; unsupported hardware must degrade safely.

### Resource governor

Use event-driven states such as NORMAL, LOW_MEMORY, HDD_BUSY, THERMAL_LIMIT, LOW_BATTERY, and FOREGROUND_MEDIA. Each state changes budgets rather than applying global magic values:

- reduce optional caches and freeze background apps under memory pressure;
- pause indexing/updates/AI work when foreground I/O or media is active;
- raise foreground scheduling/I/O priority without starving kernel and system tasks;
- reduce motion/effects before input responsiveness or data integrity;
- record every policy transition and reason, with bounded logs;
- always provide a safe fallback and recovery path.

## Import and licensing workflow

Before any external source enters the repository:

1. Identify the exact upstream project, pinned version/commit, supported architecture, maturity, and maintenance status.
2. Record license and notice obligations; preserve copyright and required source changes.
3. Create a third_party entry plus provenance, patch series, license inventory/SBOM, and local build instructions.
4. Write a ZEROOS adapter boundary and list which upstream APIs are exposed to kernel/user space.
5. Build it in isolation; disable unsupported optional features rather than faking them.
6. Add unit, integration, malformed-input, resource-exhaustion, fault-injection, and QEMU tests.
7. Compare baseline versus candidate for RAM, idle wakeups, CPU, I/O, boot time, latency, and correctness.
8. Only then enable it in a capability-gated profile. Keep rollback and a known-good fallback.

## Repository-specific implementation order

The current roadmap says the project is not yet feature-complete; full dynamic linking, a standalone installer, production browser/GPU/media, Windows/Android runtimes, complete ZERO AI tool execution, physical G560 support, recovery UX, and performance/soak certification remain open. Therefore the correct order is:

1. **Release gate first:** run make -j check, tools/release.sh, post-merge CI, and QEMU logs on the exact current main head. Fix failures before tuning.
2. **Kernel/user lifecycle:** prove process/exec cleanup, scheduler/IPC race behavior, pointer validation, and resource-exhaustion recovery with fault injection.
3. **Storage integrity:** repeated-boot persistence, abrupt power-loss simulation, journal replay, bounded writeback, disk-full and I/O error tests.
4. **Minimum usable native session:** boot to stable shell, input, text/font, launcher, Files, Terminal, Settings, restart/recovery paths.
5. **Hardware baseline:** QEMU models first; then actual G560 CPU/RAM/HDD/GPU/network/audio/Bluetooth/thermal tests.
6. **Reuse integration:** libc/runtime and crypto/compression first where ABI-compatible; then networking/browser/media. Compatibility runtimes and Android/AOSP are isolated later-stage projects, not small kernel patches.
7. **AI, update and recovery:** scoped tool broker, signed packages/updates, independent recovery boot, verified snapshot/restore and install flow.
8. **Performance optimization:** add each optimization only with before/after measurements and regression thresholds.
9. **Feature completion:** track every user-facing feature as SPECIFIED, PROTOTYPE, IMPLEMENTED, INTEGRATED, TESTED, HARDWARE-VERIFIED; never count documentation-only work as implementation.

## Required evidence for every claim

For each adopted optimization, store: upstream reference + version/license, files changed, configuration and hardware gates, test command, test/CI URL or log artifact, baseline and result, regressions, and rollback. No percentage increase or production-ready label is justified by adding a document or passing a string-marker script alone.

## Audit status

- Text-audited PDF families: 4 distinct works; duplicate Library copies were not counted as new ideas. Two additional same-day repository-uploaded PDFs were identified by filename but still require full-text extraction and a second-pass content audit.
- Reference concepts classified: adopt/prototype, hardware/security-gated, reject-as-default.
- External code imported by this audit: none.
- Runtime implementation changes made by this audit: none.
- Next required action: execute the current-main release gate and use test evidence to select the first implementation fix.
