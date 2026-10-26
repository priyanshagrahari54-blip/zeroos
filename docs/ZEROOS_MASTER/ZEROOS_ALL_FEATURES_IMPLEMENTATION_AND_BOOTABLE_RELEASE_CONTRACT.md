# ZEROOS — Complete Feature Implementation and Bootable Release Contract

Status: mandatory execution specification, not evidence of completion.
Scope: all existing ZEROOS requirements, all 10 stages, and both uploaded optimization catalogs (100 + 120 entries).

## 1. Mission and definition of done

Build a real native x86-64 OS that reproducibly builds, boots from a verified ISO in QEMU, enters a usable native session, and passes behavior-based tests. Do not substitute UI mockups, marker strings, fake drivers, stub success responses, or unsupported claims.

“100%” means every approved requirement is implemented or explicitly classified as hardware-dependent and tested, blocked with a reason, or not applicable with a technical justification. A source file, architecture document, marker test, host test, or single QEMU boot does not prove a complete end-user feature.

Release gates:
1. Clean reproducible build with toolchain prerequisites documented.
2. Valid GRUB/El Torito ISO, integrity checksum, and actual boot test.
3. QEMU cold/repeated boot, interactive session, shutdown/reboot, failure and recovery paths pass.
4. Run make check and tools/release.sh from a clean checkout; save logs and exact SHA. Verify main-branch CI separately.
5. Behavior-based tests cover kernel/userspace, storage, network, desktop, security, updates/recovery, browser/media, compatibility, AI and performance.
6. No known critical memory-safety, privilege-boundary, data-integrity, rollback or permission-bypass defect.
7. Hardware claims labelled verified, partial, unsupported or not tested for the exact device. QEMU does not prove physical Wi-Fi, GPU acceleration, fan control, Bluetooth or 1080p playback.
8. Publish release notes, SBOM/licences, limitations, installation/recovery instructions, checksums and rollback procedure.

## 2. Execution protocol

For every requirement: inventory → inspect current implementation → design invariants/API/ABI → implement → positive, negative and fault tests → targeted tests → affected stage gate → full check/release gate → measure → update status/docs. Work in reviewable commits. Do not claim tests passed without actual output. Prefer maintained, compatible, auditable upstream components where appropriate; keep ZEROOS-specific kernel, resource policy, lifecycle, permission broker, desktop policy and integration auditable. Every subsystem defines ownership/lifetime, locking/concurrency, interrupt context, errors, resource bounds, cancellation, security boundary, observability and recovery. Unsupported paths fail closed; no fake success.

## 3. All-stage completion plan

### Stage 0 — Build and engineering discipline
Reconcile every requirement into a traceable ledger: requirement → source → test → gate → status → evidence. Validate clean parallel builds, dependencies, toolchain, ISO structure, checksums and reproducibility. CI should run build, host tests, ABI checks, static analysis/sanitizers where possible, QEMU boot, repeated boot, artifact integrity and regression tests.

### Stage 1 — Boot, kernel, scheduler and SMP
Verify boot protocol, long mode, paging, allocator, IDT/ISR, PIC/APIC, timers, IRQs, SMP startup, locks/atomics, scheduler/context ownership, preemption, wait queues, timed waits, task exit/reaping and stack guards. Test races, starvation, interrupt nesting, timer wrap, allocator exhaustion, multi-vCPU stress and repeated boot. Preserve interrupt-frame/register-save ABI; kernel SIMD stays disabled unless full context save/restore is implemented and tested. Add panic capture and diagnostics.

### Stage 2 — Processes, userspace, IPC and runtime
Complete process/thread lifecycle, ELF validation/loading, user stack/auxv, address-space ownership, spawn/exec/wait/exit/reap, file handles, capability IPC, pipes, events, shared memory, timeouts, cancellation, partial writes, backpressure, revocation and resource limits. Dynamic linking requires a full design and tests for PT_INTERP, interpreter mapping, relocations, load bias, TLS, GOT/PLT, auxv, W^X, RELRO and malformed inputs. Until implemented, reject unsupported dynamic ELF explicitly. Fault-inject each creation step and prove mappings, pages, handles and process slots are released exactly once. Provide a documented native userspace runtime and syscall ABI.

### Stage 3 — Storage and integrity
Complete block I/O, supported AHCI/NVMe, partition discovery, VFS, files/directories, page cache, mount lifecycle, permissions, limits and persistent filesystem recovery. Define filesystem invariants, transaction/journal boundaries, checksums, allocation consistency, repair/fsck and versioning. Test power cuts, interrupted writes, full disks, device errors, repeated-boot persistence, backup/restore and data integrity. Never trade integrity for throughput.

### Stage 4 — Drivers, network and power
Complete PCI/resource ownership, DMA/interrupt contracts, USB/HID/input, display, audio, storage, Ethernet/Wi-Fi, Bluetooth, ACPI, thermal/fan and battery interfaces. Network coverage includes Ethernet, ARP, IPv4/IPv6, ICMP, UDP/TCP, routing, DHCP/DNS, sockets, firewall, conntrack, MTU/fragmentation, checksums, retransmission, congestion and timeouts. Fuzz malformed packets and test exhaustion, reconnect, link loss, DNS failure, firewall default-deny and IPv6 fail-closed. Never fabricate sensor or hardware capability data.

### Stage 5 — Desktop and first-party apps
Complete modesetting/framebuffer fallback, windows, compositor, focus, keyboard/mouse/touchpad/touch/stylus where supported, clipboard/history, notifications, workspaces, snapping, accessibility, Hindi/English, settings, files, terminal, search and session restoration. Implement themes (Aurora, Obsidian, Pearl, Solar, Ocean, Forest, Sunset, Mono, high contrast/custom), glass with Soft/Solid fallback, rounded geometry, Dynamic Capsule/Island, ZERO Bar, dock, tablet layouts and reduced motion. Idle UI must be event-driven: no unnecessary polling, animation timers or compositor redraw while unchanged. Include loading/empty/error/offline/permission/low-memory states.

### Stage 6 — Security, updates and recovery
Implement capability access, privilege separation, app permissions, isolation, secret handling, firewall, crypto, encryption where supported, signed packages/updates, anti-rollback and audit logs. Updates must verify before activation, use staged/transactional or A/B activation where feasible, preserve last-known-good and recover from power loss. Build an independently bootable Recovery ISO/environment for install, upgrade, keep-files/clean install, backup/restore, boot repair, filesystem repair, safe mode, reset, snapshots and verified rollback. Destructive operations require confirmation. Security/antivirus claims require real detection tests and provenance, not a UI shell.

### Stage 7 — GPU, media, browser and apps
Provide GPU/display interfaces, software fallback, buffer ownership, mode switching, multi-monitor capability and crash recovery. Media includes audio, codecs, subtitles/tracks, speed, playlists, A/V sync, screenshots, streaming and verified hardware decode. Integrate a real maintained browser engine; implement TLS/certificate checks, sandbox/site isolation, downloads, permissions, privacy, bounded cache, accessibility and updates. Lifecycle: ACTIVE → IDLE → FROZEN → DISCARDED with state restoration. G560 target: sustained 1080p H.264/AVC 30 FPS only when a real decode/render path is detected and measured; record dropped frames, sync, CPU, RAM, HDD I/O and thermals. 1080p60, AV1, VP9 and GPU acceleration remain hardware-dependent. Files, Music, Video, Study/PDF, notes, OCR/dictionary, downloads, screenshot and recording must be functional apps, not placeholder windows.

### Stage 8 — Windows, Android, VM and gaming
Windows support needs a real PE/Win32/Win64 compatibility runtime, API coverage, graphics/audio/input, isolation and a measured compatibility matrix. Android requires a maintained/licensed AOSP/ART-derived plan, package/signature validation, permissions, lifecycle and graphics/audio/network isolation. VM/containers require capability detection and honest fallback. Gaming includes controller support, profiles, foreground priority, FPS/latency metrics, overlays/recording when supported and resource reclaim. File recognition is not compatibility.

### Stage 9 — ZERO AI and ecosystem
Architecture: AI orchestrator → AI Broker/Permission Layer → ZEROOS APIs/tools → kernel interfaces. Provide tools for files, terminal, apps, browser, settings, Git, compiler, tests, packaging and diagnostics. Separate read/write/execute/network/admin capabilities. Support plan→inspect→act→test→report with cancellation, timeout, bounded retries, reversible operations, audit trail, provenance, secret isolation, sandbox and emergency STOP. Confirm destructive actions, privilege escalation, credential access, external publication and broad access. Add context/memory controls, offline behavior where available and explicit unavailable states. Broker/intent parser alone is not a complete agent. Implement package manager, SDK, automation, device link, cloud sync, P2P, phone and smart-home integration with permissions and tests.

### Stage 10 — Performance and release certification
Repeatable benchmarks for cold/warm boot, app launch/switch, files, search, browser, media, memory pressure, idle CPU/GPU, power, thermals, network and recovery. Publish hardware, firmware, build flags, data, repetitions, median/p95, variance and logs. Test G560 variants independently: CPU/chipset, 2 GB profile, SATA HDD/SSD, Intel/NVIDIA variants, Wi-Fi/Ethernet, audio, touchpad, Bluetooth, webcam, battery and thermals. Certify install/update/rollback/restore/repair/shutdown/power-loss and soak. Targets are not results.

## 4. Preserve and disposition every uploaded optimization

Source inventories: 100 Low-Level System Optimizations Catalog.pdf (100 entries) and 120 Low-Level System, Network & Browser Optimization Catalog.pdf (120 entries). Keep all 220 individually traceable in a matrix. Each entry must be marked IMPLEMENT, ADAPT, REUSE, HARDWARE_DEPENDENT, DEFER or REJECT_WITH_REASON. Cross-reference duplicates rather than implementing twice.

### Storage/I/O — items 1–25 in each catalog
Evaluate bcache/hybrid tiering, EnhanceIO/Flashcache, dm-cache, bcachefs/COW, tmpfs, ramfs, async I/O rings, BFQ, Kyber, mq-deadline, dirty-background thresholds/ceilings/expiry, noatime/nodiratime, journal/commit interval, data=writeback, read-ahead, EROFS, SquashFS+ZSTD, preallocation, TRIM/fstrim, drive spindown/acoustic mode, SATA NCQ and NVMe queue depth. These are reference implementations/ideas, not literal ZEROOS APIs. Detect HDD/SSD/NVMe, bound cache memory, avoid seek storms and protect journal ordering. Unsafe writeback must not be default.

### RAM/VM — items 26–50 in each catalog
Evaluate zram, zswap-like compression, ZSTD, LZ4, LZO-RLE, swappiness-like policy, VFS cache pressure, proactive reclaim, free-memory reserves, KSM/dedup, page-cluster, huge/opt-in pages, PSI-like stall telemetry, early OOM, cgroup/systemd-oomd analogues, memory locking, HugeTLB, contiguous allocation, KPTI/isolation cost, compaction, NUMA reclaim/binding, slab merging and overcommit controls. Build ZERO Adaptive Memory Fabric with hot/warm/cold/compressed/discardable/disk tiers, demand paging, shared read-only pages, COW and bounded caches. Compression ratio is data-dependent. Never pin broad memory on 2 GB hardware or kill critical tasks without policy and recovery. Security mitigations cannot be disabled solely for speed.

### CPU/scheduler/toolchain — items 51–75 in each catalog
Evaluate EEVDF-like scheduling, affinity, resource bands, frequency governors, scheduler APIs, IRQ balancing/pinning, isolated CPUs, tickless behavior, Intel/AMD P-state/EPP, turbo, C-states, migration costs/limits, NUMA, real-time policy, priority tools, microcode, speculative-execution mitigations, PGO, LTO and SIMD/vectorization. Adapt to topology and workload. Do not globally pin cores or disable deep idle without measured benefit. Bound RT priority. SIMD is allowed only with correct ABI/context save/restore; preserve current kernel SIMD restrictions otherwise. Keep security mitigations enabled by default.

### Network/IPC/services — items 76–100 in each catalog
Evaluate BBR-style congestion control, TCP Fast Open, backlog limits, TCP buffer autotuning, low-latency policy, XDP/eBPF-inspired hooks, RPS/RFS, TSO/GSO/GRO, window scaling, timestamp policy, SACK, receive backlog, DNS cache, Unix sockets, shared memory, memfd-like objects, service pruning, log caps, PCIe ASPM, SATA ALPM, watchdog policy and mature low-latency kernel ideas. Buffers scale with RAM/link speed. DNS cache honors TTL/privacy. Do not disable watchdogs, TCP safeguards or power management by default. eBPF/XDP require verifier and sandbox design and are optional.

### Browser — items 101–120 of the 120 catalog
Track parallel downloads, GPU rasterization, zero-copy rendering, tab discard, content filtering, JS JIT/heap caps, HTTP/3/QUIC, DoH policy, WebRTC/telemetry privacy, process-per-site, RAM/disk cache, back-forward cache, preloading/prerender, background suppression, smooth scrolling, hardware video decode, lazy images/iframes and tab suspension. Apply only to a real browser engine with feature detection and fallback. Do not universally disable secure DNS/WebRTC or force experimental protocols. Bound RAM cache on 2 GB. Preload must not harm privacy, bandwidth or foreground latency. Never assume AV1/VP9 decode.

For every optimization record item number, ZEROOS equivalent, dependency, hardware gate, expected benefit, CPU/RAM/I/O cost, integrity/security risk, fallback, benchmark, baseline/result, confidence and disposition. Treat “zero latency,” “0 ms,” fixed compression ratios and guaranteed RAM savings as unverified marketing claims, not acceptance criteria.

## 5. No-feature-drop product ledger

All existing requirements in the consolidated master spec, cross-cutting contract, stage plans, visual design, G560 profile, zero-render architecture and AI plans remain in scope. Explicitly trace: native boot/kernel/runtime; memory/scheduler/filesystem/packages/terminal/SDK; drivers and hardware detection; desktop/tablet themes, Dynamic Capsule, bar/dock/widgets, accessibility, shortcuts, clipboard, emoji, capture/recording, search/workspaces/notifications; Browser/Files/Music/Video/Study/PDF/OCR/downloads; Windows/Android/VM/containers/gaming; ZERO AI tools and safety; firewall/privacy/encryption/permissions/signed updates/recovery/install/backup/restore/snapshots/rollback; cloud/P2P/phone/device-link/automation/smart-home; power-loss safety, Hindi/English; performance/hardware center, HDD-aware I/O, compression/reclaim, thermal/network adaptation, zero-render idle, observability; G560 Balanced Legacy and conditional 2 GB 1080p H.264 acceptance. No existing requirement is silently removed.

## 6. Resource and measurement contract

“Near-zero idle” means no unnecessary software polling, frame generation, animation timers, indexing or resident heavy engines; not literal zero CPU/RAM/GPU. Browser, AI models, Android/Windows runtimes, codecs, indexer, backup/update workers are demand-loaded, lifecycle-managed and reclaimable. Initial targets, not measured results: aggressive idle 100–180 MB; normal desktop 150–250 MB; UI private working set ≤50 MB. Account separately for framebuffer/VRAM/shared pages; revisit targets if hardware requires it. On 2 GB, prefer compression/selective discard over unbounded tmpfs, RAM disk, preload or cache. Measure boot, launch/switch, RAM private/shared, idle wakeups, CPU/GPU, HDD bytes/seeks, dropped frames, A/V sync, throughput, thermals and power with raw data. A target is not a guarantee until repeated measurements on named hardware pass.

## 7. Documentation updated with every implementation change

Update together: current implementation status (percentages, SHA, exact commands/results, blockers, hardware status); master roadmap (dependencies and next gates); canonical master/subsystem spec; MASTER_INDEX and docs index; requirement-to-test evidence matrix; release notes and known limitations. Documentation-only changes do not increase implementation percentages. Increase estimates only when behavior is implemented, integrated, tested and evidenced. Maintain all 220 optimization entries in the traceability matrix.

## 8. Next actions in dependency order

1. Inspect current main, workflows and latest artifacts; run clean release gate and preserve actual failure logs.
2. Fix build/boot/ISO/release blockers first.
3. Stabilize Stage 1–2 lifecycle, IPC, ELF, address-space and failure cleanup with fault injection.
4. Complete Stage 3 persistence, filesystem repair and power-loss recovery.
5. Complete Stage 4 driver/network contracts and truthful capability reporting.
6. Complete Stage 5 usable native session and event-driven idle.
7. Deliver independent recovery/install/update/security.
8. Integrate real browser/media engines, then Windows/Android runtimes under licensing and dependency plans.
9. Complete ZERO AI tool execution behind the permission broker.
10. Run G560 and wider hardware matrix, performance/soak/fault tests; publish evidence before release claims.
11. Run CI on every commit and nightly longer QEMU stress/soak. Daily reports should be generated from actual test evidence; scheduling reports does not implement features.

## 9. Status labels

Implemented = real code behavior. Unit-tested = isolated tests pass. Integrated = subsystem path tested. QEMU-verified = exact guest scenario passed with configuration/log. Hardware-verified = exact physical configuration passed. Release-certified = all release gates passed for a named artifact. Partial/foundation = API/policy scaffold only. Blocked/not tested = reason and next action recorded.

Final instruction: continue from the actual repository state, inspect before editing, implement low-completion/high-dependency blockers first, preserve all requirements, test and update docs in the same work stream, and report exact commit, files, commands/results, remaining gaps and evidence-weighted percentages. Never call ZEROOS 100% complete or production-ready before every required gate is satisfied.
