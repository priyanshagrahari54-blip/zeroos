# ZEROOS MASTER BLUEPRINT

This is the living product, architecture and engineering specification for ZEROOS.

## 1. Vision

ZEROOS is a real native x86-64 operating system, not a UI mockup, Linux skin, toy kernel, or simulation.

Primary goals:
- very low idle overhead
- fast boot and application launch
- responsive on old hardware
- native modular kernel and userspace
- secure process isolation
- reliable updates and recovery
- modern original desktop
- optional compatibility layers
- optional AI integration
- measurable performance
- mature implementations from the beginning

Target early hardware includes approximately 2 GB RAM, HDD storage and older Intel integrated graphics. Exact resource targets must be measured rather than invented.

## 2. Product principles

1. Correctness before optimization.
2. Performance must be measured.
3. Event driven over polling.
4. Lazy initialization where safe.
5. Explicit ownership and lifecycle.
6. Minimal resident background work.
7. Security boundaries are first-class.
8. Every major subsystem has tests and diagnostics.
9. No throwaway/basic architecture intended for later replacement.
10. Core OS must work without AI.

## 3. System layers

Hardware
-> x86-64 architecture/HAL
-> boot and kernel
-> memory, scheduler, interrupts, IPC, drivers
-> system services
-> system APIs
-> compositor/window manager
-> desktop shell and applications

Cross-cutting systems:
- diagnostics
- security
- package/update
- recovery
- telemetry
- optional isolated AI service

## 4. Kernel

Responsibilities:
- CPU and interrupt control
- scheduler
- tasks/threads/processes
- physical and virtual memory
- timers
- synchronization
- IPC
- system calls
- device framework
- storage/block interfaces
- networking interfaces
- security primitives
- accounting
- diagnostics

Kernel invariants:
- user pointers are validated
- ownership is explicit
- sleeping never occurs under spinlocks
- IRQ-sensitive locking is IRQ-safe
- executable writable memory is avoided
- failures leave deterministic states
- stack corruption is detected
- interrupt frame ownership is explicit

## 5. Process and thread model

Eventually separate:
- process
- thread
- address space
- scheduler entity
- file descriptor table
- credentials
- security context

Lifecycle:
NEW -> RUNNABLE -> RUNNING -> BLOCKED/SLEEPING -> RUNNABLE -> EXITING -> ZOMBIE -> REAPED

Future thread context:
- GPRs
- RIP/RSP/RFLAGS
- FPU/SSE/AVX state
- FS/GS
- TLS
- CR3/address-space
- scheduler state
- CPU affinity

## 6. Scheduler

Current capabilities:
- cooperative context switch
- timer preemption
- idle task
- wait queues
- timed sleep
- zombie reaping
- preempt_count
- need_resched
- stack guards
- interrupt-frame resume

Target:
- scheduler entities
- priorities
- fair scheduling
- virtual runtime/eligibility model
- real-time class
- optional deadline class
- per-CPU runqueues
- CPU affinity
- load balancing
- tickless operation
- latency accounting
- tracing
- context-switch benchmarking

Evaluate CFS/EEVDF concepts without copying Linux implementation.

## 7. Memory

Physical:
- page allocator
- object/slab allocator
- page ownership
- refcounts
- higher-order allocation
- per-CPU caches
- fragmentation tracking
- reclaim
- page cache
- swap

Virtual:
- higher-half kernel
- physical direct map
- per-process address spaces
- page faults
- demand paging
- lazy allocation
- copy-on-write
- VMAs
- guard pages
- ASLR
- NX
- W^X
- huge pages
- page-table reclamation
- PCID/INVPCID
- SMP TLB shootdowns

## 8. Interrupts and timers

Architecture:
hardware -> ISR -> saved context -> dispatcher -> handler -> scheduler decision -> restore -> iretq/context return

Requirements:
- exact hardware frame model
- explicit frame ownership
- consumed frames invalidated
- stack bounds validation
- short IRQ handlers
- deferred work for expensive processing
- correct EOI

Timer evolution:
PIT bootstrap -> APIC timer -> calibrated monotonic clock -> high resolution timers -> tickless/nohz where useful.

## 9. Synchronization

Provide:
- spinlocks
- irqsave locks
- atomics
- mutexes
- rwlocks
- semaphores
- wait queues
- completions/events
- condition variables
- futex-like primitive later

Maintain a documented lock hierarchy and forbid sleeping while holding spinlocks.

## 10. IPC

Target:
- synchronous messages
- async queues
- shared memory
- events
- channels
- pipes
- sockets
- signals
- capability-controlled handles

Optimize common paths for low allocation and low copy overhead.

## 11. Driver framework

Bus -> device discovery -> driver matching -> driver instance -> device API

Target buses:
- PCI/PCIe
- ACPI
- USB

Driver services:
- IRQ
- DMA
- MMIO
- I/O ports
- power states
- hotplug
- ownership
- lifecycle

## 12. Storage

Application -> native file API -> VFS -> filesystem -> page cache -> block layer -> request scheduler -> driver -> device

Required:
- partitions
- filesystem
- journaling/crash consistency
- page cache
- buffered/direct/async I/O
- checksums
- snapshots
- encryption
- recovery

HDD optimization:
- sequential I/O
- batching
- read ahead
- metadata locality
- minimized random I/O

## 13. Networking

Application -> sockets -> protocol stack -> network device -> driver

Target:
- Ethernet
- Wi-Fi framework
- IPv4
- IPv6
- ARP/ND
- TCP
- UDP
- DNS
- DHCP
- firewall
- TLS integration
- diagnostics

Use bounded buffers, batching and zero-copy where beneficial.

## 14. Graphics

Application -> toolkit -> compositor -> window manager -> graphics API -> GPU/display -> driver

Features:
- framebuffer fallback
- acceleration
- multiple displays
- scaling
- cursor
- input
- damage tracking
- frame pacing
- occlusion
- low-power rendering
- accessibility

## 15. Desktop UI

Original visual language:
- dark cinematic/premium
- restrained transparency
- subtle depth
- clear typography
- compact controls
- limited animation
- keyboard friendly
- low GPU/CPU cost

Main shell:
- bottom launcher/taskbar
- centered universal search
- app launcher
- system tray
- notifications
- control center
- workspace switcher
- clock/status
- performance indicator

Do not copy Apple menu bar, Finder, Dock, or another OS pixel-for-pixel.

## 16. Window manager

- floating windows
- snap/tiling
- maximize/minimize
- workspaces
- overview
- keyboard navigation
- multi-monitor
- persistent workspace state
- window rules
- accessibility

## 17. Universal Search

Providers:
- apps
- files
- folders
- settings
- documents
- recent items
- system actions
- diagnostics
- AI

Pipeline:
query -> parser -> intent -> parallel providers -> ranking -> results

Indexes must update incrementally.

## 18. ZERO Files

Features:
- fast navigation
- tabs
- split view
- breadcrumbs
- search
- preview
- thumbnails
- metadata
- permissions
- archives
- copy/move queue
- conflict resolution
- bulk rename
- favorites
- recent files
- trash
- storage analysis

Copy engine:
- asynchronous
- cancellable
- resumable where possible
- optional checksum verification
- HDD aware

## 19. ZERO Terminal

- tabs
- split panes
- copy/paste
- search
- Unicode
- hyperlinks
- zoom
- fonts
- themes
- profiles
- history
- fullscreen
- SSH
- scripting
- package management
- diagnostics

Renderer should avoid unnecessary full-screen redraws.

## 20. Audio

Apps -> Audio API -> session manager -> audio engine -> drivers -> output

Support:
- speakers
- headphones
- microphone
- HDMI
- per-app volume
- routing
- mute
- hotplug
- notifications
- effects
- power efficient operation
- Bluetooth later

## 21. Input

hardware -> driver -> normalized event -> input manager -> focused application

Keyboard, mouse, touchpad, touchscreen where supported, gamepads later.

Include layouts, repeat, remapping, gestures, pointer settings and accessibility.

## 22. Power and thermal

- ACPI
- CPU idle
- frequency policy
- battery
- charging
- thermal zones
- fan
- suspend/resume
- wake sources

Policies:
- performance
- balanced
- battery saver

Avoid unnecessary wakeups.

## 23. Security

Baseline:
- user/kernel separation
- NX
- W^X
- ASLR
- stack protection
- capabilities/permissions
- sandboxing
- process isolation
- signed packages
- signed updates
- audit log

Future:
- secure boot
- TPM
- encrypted storage
- secrets service
- app permissions
- driver trust levels

## 24. Package manager

ZERO Packages:
- metadata
- dependency graph
- signatures
- repositories
- transactional install/remove
- rollback
- cache
- offline packages
- delta updates

## 25. Update engine

- signed metadata
- verified payloads
- staged download
- integrity checks
- atomic activation
- A/B or snapshot rollback
- health boot
- automatic rollback
- stable/preview/developer channels

## 26. Recovery

Recovery must work when normal desktop fails.

Capabilities:
- boot repair
- filesystem check
- snapshot restore
- update rollback
- safe mode
- driver disable
- logs
- terminal
- network recovery
- reset

## 27. Diagnostics

ZERO Diagnostics:
- CPU
- RAM
- disk/SMART
- network
- GPU
- temperature
- battery
- audio
- USB
- boot timeline
- kernel logs
- crash logs
- service failures

Human UI plus machine-readable diagnostics API.

## 28. Performance Center

Dashboard:
CPU, RAM, disk, network, GPU, temperature, power.

Metrics:
- utilization
- frequency
- latency
- throughput
- memory pressure
- scheduler latency
- wakeups
- background activity

Use bounded histories to control memory.

## 29. Notifications and Focus

Notifications:
- priority
- grouping
- actions
- persistence
- quiet mode
- per-app permissions
- rate limiting

Focus modes:
- Study
- Work
- Deep Focus
- Presentation
- Custom

## 30. Study Center

- Notes
- PDF viewer
- annotations
- highlights
- flashcards
- quizzes
- revision planner
- calculator
- dictionary
- focus timer
- AI study assistant

AI should explain, summarize, quiz, plan revision and help code while supporting learning.

## 31. App framework

ZERO App Framework:
- lifecycle
- windows
- IPC
- permissions
- settings
- notifications
- storage
- network
- audio
- graphics
- accessibility
- background limits

## 32. Compatibility

Windows and Android runtimes are optional user-space layers.

They must not become kernel dependencies.

Launcher can expose native, Windows and Android apps together.

Compatibility runtimes require sandboxing and resource controls.

## 33. Backup and snapshots

- filesystem snapshots
- system state
- package state
- configuration
- transactional restore
- local/external/network backups

## 34. Observability

Kernel:
- tracepoints
- counters
- ring-buffer logs
- panic dumps
- scheduler tracing
- allocator statistics
- IRQ latency

Userspace:
- service health
- crash reporting
- boot timeline
- performance telemetry

Instrumentation must have low disabled overhead.

## 35. Testing

Layers:
1. unit
2. subsystem
3. kernel integration
4. QEMU boot
5. fault injection
6. stress
7. performance
8. compatibility
9. recovery
10. upgrade/rollback

Each subsystem needs positive, negative, boundary, concurrency and resource-exhaustion tests.

## 36. Performance philosophy

Do not claim speed or RAM superiority without controlled benchmarks.

Measure:
- boot
- idle RAM
- idle CPU
- process creation
- context switch
- syscall latency
- memory allocation
- page fault
- disk I/O
- filesystem metadata
- network throughput/latency
- UI frame latency
- application launch
- suspend/resume

Record hardware, commit, compiler, configuration, sample count, median, p95, p99 and variance.

## 37. Definition of done

A subsystem is complete only when:
- architecture documented
- API defined
- implementation mature
- ownership/lifecycle explicit
- errors handled
- concurrency reviewed
- security reviewed
- resource usage measured
- tests exist
- failure modes tested
- recovery behavior defined
- CI covers critical paths
- documentation matches code


## Advanced-First Production Standard

All subsystem targets in this blueprint are production architecture targets, not future polish. Stage/dependency ordering must never be interpreted as permission to ship intentionally basic implementations.

For each subsystem, implementation must include its intended ownership, lifecycle, concurrency, security, resource, diagnostics and recovery contracts as early as dependencies allow.

A subsystem remains PARTIAL/EXPERIMENTAL until its required implementation, negative testing, stress testing, resource validation, recovery behavior and supported-hardware verification are complete.

## Production Maturity Matrix

| Domain | Production expectations |
|---|---|
| Kernel | SMP-safe architecture, hardened memory/interrupt paths, mature scheduler, diagnostics and recovery |
| Memory | ownership/refcounts, demand paging, COW, reclaim, protection, pressure handling |
| Scheduler | runqueues, priorities/fairness, affinity, load balancing, latency accounting and tracing |
| Storage | queued I/O, cache/writeback, consistency, recovery, snapshots/integrity |
| Drivers | lifecycle, DMA/IRQ, hotplug/power/error recovery and capability detection |
| Networking | complete protocol/service boundary, firewall, diagnostics and failure handling |
| Graphics | acceleration/fallback, compositor, frame pacing, damage tracking, accessibility |
| Desktop | isolated services, original shell, search/settings/notifications and adaptive resource policy |
| Security | privilege separation, permissions/capabilities, secure updates and auditability |
| Recovery | detection, isolation, repair, rollback and user-visible diagnostics |

No row is considered production merely because a happy-path demo works.


# ZEROOS 10-STAGE MASTER EXECUTION PLAN

This section is the authoritative execution extension for the current repository roadmap. Stages 0-5 describe the existing production-engineering foundation; stages 6-10 are the next five execution stages. The plan is dependency-ordered, production-first, and must never introduce throwaway implementations.

## Global requirements for every stage

Every stage follows:

AUDIT -> DESIGN -> IMPLEMENT -> TEST -> STRESS -> MEASURE -> DOCUMENT -> INTEGRATE

Universal production gate:
- declared scope is actually implemented;
- public contracts and ownership/lifetime rules are documented;
- concurrency and failure paths are reviewed;
- security boundaries are reviewed;
- CPU/RAM/I/O/GPU/network/thermal behavior is measured where relevant;
- positive, negative, boundary, concurrency and resource-exhaustion tests exist where applicable;
- QEMU/CI and supported hardware evidence is recorded;
- documentation matches the code;
- no feature is called supported merely because detection, parsing, a host test, or an architectural contract exists.

Global product constraints:
- ZEROOS is a real native x86-64 OS, not a mockup, Linux skin, simulator, or toy.
- Target old hardware, including the Lenovo G560 class, without making unverified hardware claims.
- Installed != loaded != running != active. Dormant features must not consume continuous CPU/GPU/network work.
- Prefer event-driven execution, lazy initialization, bounded queues, batching, shared read-only state, cache reuse and explicit lifecycle management over polling or unnecessary resident services.
- Literal zero resource use while an active feature is rendering, decoding, executing or transferring is impossible; engineering targets are minimum useful work, near-zero idle/background overhead, and bounded thermal load.
- Native 1366x768 is the G560 reference display target; FHD/1080p content should be efficiently decoded/scaled when hardware permits. 2K/4K must never be assumed on the G560 reference profile.
- Animated wallpaper must stop rendering when hidden/covered/fullscreen/locked and adapt frame rate/resolution under pressure.
- 1080p playback, browser media, gaming and graphics must use hardware acceleration/decode when genuinely supported; no claim without measured validation.
- HDD paths must prioritize sequential I/O, request merging, read-ahead, write batching, metadata locality and bounded random I/O.
- Network stack should target efficient 100 Mbps operation where the physical adapter, link, driver and network permit; the OS cannot manufacture link capacity.
- Security is foundational: privilege separation, capabilities/permissions, sandboxing, NX/W^X/ASLR/stack protection, signed packages/updates, secure boot/TPM where available, encrypted storage/secrets services, firewall and recovery.
- Windows and Android are isolated user-space compatibility layers, never kernel dependencies.
- ZERO AI is the personal ZEROOS-specific AI service, permission-brokered, dormant until requested, and separate from Forge AI. It must not require AI for core OS operation.
- Controller/gamepad support is explicitly out of ZEROOS scope unless separately re-authorized; do not add it to requirements.
- Do not claim benchmark multipliers or "zero overhead" as facts without controlled measurements.

## Combined 10-stage table

| Stage | Scope | Primary exit gate |
|---|---|---|
| 0 | Reproducible engineering, CI, build/release foundations | deterministic builds/tests, CI evidence |
| 1 | Kernel execution, scheduler/context/SMP certification | scheduler lifecycle stable under stress |
| 2 | Process/thread, ring-3, syscall, IPC, userspace execution | first reliable user process + syscall ABI |
| 3 | VFS, filesystem, block/storage, HDD-aware persistence | crash-consistent persistent storage |
| 4 | Driver model, PCI/ACPI/DMA/IRQ, network, audio, power/thermal | detected vs operational support is explicit |
| 5 | Graphics/compositor/window/session/desktop platform | live desktop session with bounded rendering |
| 6 | Security enforcement + packages/updates + recovery | fail-closed security and rollback-capable system |
| 7 | GPU/graphics acceleration + media + browser/native app foundation | measured accelerated desktop/media path |
| 8 | Windows compatibility + Android runtime foundation + gaming | isolated compatibility execution only where tested |
| 9 | ZERO AI + automation + ecosystem services + performance engineering | brokered AI/actions and low-overhead service lifecycle |
| 10 | G560 + hardware matrix + long-duration certification + release | release evidence, recovery, security and performance gates |

# STAGE 6 — SECURITY, PACKAGES, UPDATES AND RECOVERY

### Objective
Turn the existing security primitives and host-tested policy cores into enforced OS boundaries without making the kernel depend on desktop services.

### Work
1. Complete privilege separation and capability/permission enforcement.
2. Define process security contexts, credential model and permission checks.
3. Enforce NX/W^X/ASLR/stack protections and add CFI/hardening where architecturally appropriate.
4. Add sandbox enforcement hooks; fail closed when a required policy cannot be enforced.
5. Complete firewall policy binding to the packet path.
6. Integrate encrypted storage/secrets architecture with explicit key lifecycle; never embed production secrets.
7. Complete package metadata/signature verification and trust policy.
8. Complete transactional update flow: download -> verify -> stage -> preflight -> activate -> health check -> commit/rollback.
9. Integrate snapshots with update rollback and recovery.
10. Build recovery environment, safe mode, driver disable/recovery terminal and filesystem repair paths.
11. Add security regression, fuzz, fault-injection and privilege-boundary tests.
12. Keep security telemetry local/opt-in for any external reporting.

### Exit evidence
- unauthorized access is rejected at the real enforcement boundary;
- signed update tampering/replay/wrong-key tests pass;
- failed activation can recover deterministically;
- recovery works without the normal desktop;
- security failures do not silently downgrade to success.

# STAGE 7 — GPU, GRAPHICS ACCELERATION, MEDIA, BROWSER AND NATIVE APP FOUNDATION

### Objective
Move from framebuffer/compositor foundations to a real accelerated graphics/media/application platform while retaining a low-overhead path for old hardware.

### Work
1. Finalize GPU abstraction and driver ownership model.
2. Implement the first supported accelerated backend; evaluate Vulkan where the hardware/driver stack can support it.
3. Keep framebuffer/CPU fallback for unsupported hardware.
4. Preserve damage tracking, occlusion culling, frame pacing and adaptive rendering.
5. Make wallpaper/event rendering dormant when not visible; use static-frame and frame-skip paths.
6. Add hardware video decode/encode integration where available; define software fallback and codec capability reporting.
7. Build 1080p playback path with sustained-load, sync, memory and thermal tests.
8. Complete native browser engine integration as an isolated userspace process tree; browser UI must not imply a working web engine until one exists.
9. Complete ZERO App Framework runtime contracts for lifecycle, permissions, storage, graphics, audio, IPC and accessibility.
10. Integrate native apps incrementally: Files, Terminal, Settings, Media Player, PDF, Notes/Study tools, Diagnostics/Performance Center.
11. Ensure all background work is cancellable, bounded and suspendable.
12. Add UI state/condition contract coverage for LOW_RESOURCE, OFFLINE, PERMISSION_DENIED, REDUCED_MOTION and localization.

### Exit evidence
- accelerated path is measured against fallback;
- 1080p media is tested on the target hardware matrix;
- desktop frame latency and resource usage are recorded;
- browser is clearly separated into implemented/tested vs planned capability;
- native app lifecycle and permissions are enforced by the real OS.

# STAGE 8 — WINDOWS, ANDROID, COMPATIBILITY AND GAMING

### Objective
Provide isolated modern application compatibility without contaminating the native kernel or core desktop.

### Windows
1. Complete PE/COFF loading and validation.
2. Implement user-space Win32/Win64 API translation contracts incrementally.
3. Define DLL loading, import resolution, thread/process, synchronization, filesystem and registry compatibility boundaries.
4. Add graphics compatibility through a tested translation path; evaluate DXVK/VKD3D-style architecture where technically suitable, without copying implementation blindly.
5. Keep compatibility runtimes sandboxed and resource-governed.
6. Maintain explicit unsupported diagnostics; never silently emulate unsupported behavior.
7. Establish a real Windows application compatibility matrix before publishing support claims.

### Android
1. Maintain the selected baseline architecture: AOSP 14 / API 34, primary arm64-v8a baseline as already documented.
2. Keep Android runtime isolated, resource-controlled and dormant when unused.
3. Integrate graphics/audio/input/network adapters through documented service contracts.
4. Define APK/package lifecycle, permissions, storage and launcher integration.
5. Do not claim Android app/device support until CI or real-hardware evidence exists.

### Gaming
1. Build game process lifecycle and performance governor on top of real graphics/runtime capabilities.
2. Support measured FPS targets, foreground priority, memory-pressure adaptation and thermal throttling.
3. Use hardware acceleration and efficient I/O where available.
4. Preserve explicit no-controller-support scope.
5. Publish only tested game/runtime/hardware combinations.

### Exit evidence
- no compatibility runtime is a kernel dependency;
- sandbox/resource controls are enforced;
- supported apps are backed by reproducible test evidence;
- unsupported APIs produce deterministic diagnostics;
- gaming performance is measured rather than promised.

# STAGE 9 — ZERO AI, AUTOMATION, SERVICE ECOSYSTEM AND PERFORMANCE ENGINEERING

### Objective
Make ZERO AI a deeply integrated but optional ZEROOS service, while keeping the entire OS responsive when AI is idle.

### ZERO AI architecture
request -> permission broker -> context provider -> backend selector -> inference -> action executor -> audit/result

### Work
1. Complete permission-scoped ZERO AI actions for files, apps, terminal, settings, diagnostics, study and automation.
2. Enforce explicit grants for context, filesystem mutation, process launch, network egress and persistence.
3. Keep model backends dormant until submit; bounded queues and cancellation are mandatory.
4. Wipe sensitive request/result buffers when their lifecycle ends.
5. Support local backend first where hardware permits, with explicit remote egress permission when remote inference is used.
6. Never grant kernel privileges directly to the AI.
7. Add action previews/confirmation for destructive or high-impact operations.
8. Integrate universal search and diagnostics as controlled AI context providers.
9. Complete service manager lifecycle states: DORMANT/WARM/ACTIVE/THROTTLED/SUSPENDED/STOPPED.
10. Add performance tracing for CPU wakeups, memory pressure, disk I/O, GPU work, network activity and service activation.
11. Optimize idle/background paths using event-driven execution, batching and bounded telemetry.
12. Build workload profiles for G560/2 GB-class systems and higher-capability systems without hard-coding one machine's limits.
13. Maintain a benchmark suite with median/p95/p99, variance, hardware, commit, compiler and configuration metadata.

### Exit evidence
- ZERO AI can perform only explicitly authorized actions;
- no request means no active inference/polling loop;
- service lifecycle transitions are observable;
- performance regressions are detected in CI/benchmark runs;
- core OS remains fully functional with ZERO AI disabled.

# STAGE 10 — HARDWARE CERTIFICATION, LONG-DURATION STABILITY AND RELEASE

### Objective
Prove the system on real hardware and establish release gates.

### G560 reference certification
1. Create a hardware profile for the Lenovo G560 class using detected actual configuration, not assumptions.
2. Validate native 1366x768 display path.
3. Validate HDD performance, boot, page-cache behavior and sustained I/O.
4. Validate 2 GB-class memory pressure behavior where applicable to the tested machine.
5. Validate 1080p source playback/scaling where the actual decoder path supports it.
6. Validate Wi-Fi/Ethernet according to the installed adapter; record actual link rate instead of assuming 100 Mbps.
7. Validate CPU/GPU temperatures, fan behavior, throttling and sustained workload behavior.
8. Validate sleep/resume, battery/AC transitions and recovery.
9. Validate long-duration desktop, browser, media, file-copy and compilation workloads.
10. Record failures and unsupported hardware explicitly in HARDWARE.md/VALIDATION.md.

### Release certification
- boot/reboot/reset stress;
- scheduler and SMP stress;
- memory pressure/reclaim stress;
- filesystem crash/recovery tests;
- network soak tests;
- graphics frame-time tests;
- 1080p media soak;
- security regression/fuzz/fault tests;
- update/rollback/recovery tests;
- compatibility tests;
- idle resource measurements;
- thermal soak;
- performance regression comparison.

### Exit evidence
Stable release requires:
- no known critical kernel crash in the supported matrix;
- security gates pass;
- update and recovery gates pass;
- hardware support claims match evidence;
- performance baseline is published;
- documentation is synchronized with code;
- CI is green.

## Research/decision discipline

Use the two external AI research outputs as inputs, not authority. Merge and deduplicate them, then classify each technology as ADOPT / BENCHMARK / PROTOTYPE / OPTIONAL / REJECT based on ZEROOS constraints. Re-check important claims against primary technical documentation and controlled benchmarks before architecture changes.

Do not import Linux, Windows, Android or macOS architecture wholesale. ZEROOS may use proven concepts and compatible standards while retaining its own kernel, APIs, lifecycle model and desktop architecture.

## Required living documents

The following documents must remain synchronized:
- docs/ZEROOS_MASTER_BLUEPRINT.md — product + architecture truth
- docs/ZEROOS_MASTER_ROADMAP.md — stage execution truth
- docs/ROADMAP.md — short execution bridge
- docs/BLUEPRINT_VERIFICATION.md — evidence and audit rules
- docs/HARDWARE.md — detected vs operational hardware matrix
- docs/VALIDATION.md — test/hardware evidence
- docs/ARCHITECTURE.md — subsystem contracts and dependency graph
- docs/ZEROOS_MASTER_PROMPT_10_STAGE.md — reusable execution prompt

A roadmap statement is never evidence of implementation.


## Stage 10 certification addendum (2026-10-01)

Stage 10 is **BLOCKED**, not complete. Exact-SHA CI and host checks are evidence for build/host/QEMU subsets only. No physical Lenovo G560-class profile, physical HDD/GPU/network/display test, 2 GB pressure run, thermal/battery run, installed-update recovery test, or qualifying long-duration soak was available. Do not elevate subsystem implementation or documented intent to SUPPORTED or PRODUCTION READY. The evidence ledger and gate disposition are in `VALIDATION.md` and `RELEASE_CERTIFICATION.md`.


### Stage 1 VMM permission-isolation evidence (2026-10-01)

The failed-protect ancestor-permission regression is implemented and gated in QEMU CI run 36855983261. This evidence does not change support claims, does not establish hardware testing, and does not close Stages 1–5 or Stage 10. Follow the ordered stage gate in `ZEROOS_MASTER_ROADMAP.md`.


### Stage 1 VMM frame/table retirement evidence (2026-10-01)

The page-table unlink-before-free and unconditional frame-retirement shootdown changes pass local checks and QEMU CI run 36857163730. This narrows a kernel-memory safety gap but does not establish physical SMP or concurrent-writer correctness, nor does it close Stages 1–5.


### Stage 1 VMM q35 storage regression signal (2026-10-01)

A q35 storage CI gate failed once on run 36858051968 after the earlier full-flush table-reclamation change; the serial tail ended before storage milestones and contained no panic marker. The targeted invalidation follow-up passed run 36859092757. Do not dismiss the failure: repeated q35 runs and physical storage testing remain open.


### Stage 1 CI boot reliability signal (2026-10-01)

Exact-SHA workflow run 36860017977 on docs-only commit `64f78779e3581bee45999f29a804b898b6fef09d` failed the Boot test before q35 storage; its serial tail ended during scheduler timer output without a panic marker. The exact failed assertion is unknown. The q35 storage failure in 36858051968 and boot failure in 36860017977 remain open; run 36859092757 is a passing datapoint, not proof of stable behavior. Workflow diagnostics were expanded to show recent non-timer milestones and missing boot markers.

Exact-SHA run 36860832315 on `647f84d2ec494123b964c16bb9f6997cf7e3b7a4` passed boot/SMP and q35 AHCI/NVMe two-boot persistence after adding diagnostics. Run [36873757374](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36873757374) passed the standalone Boot test but failed q35 two-boot storage. Disk 2 ended before a scheduler heartbeat after process-lifetime/wait-queue/context-worker markers; disk 1 reached tick 8865 with scheduler/process/userspace/hotplug counters complete but no storage-manager marker. The divergent tails do not establish a common root cause or show that storage drivers ran. Per-probe progress and per-disk milestone annotations are diagnostic improvements only; follow-up adds finer process-probe and storage-launch messages, also diagnostic. Failures 36860017977, 36861689464 and 36873757374 remain unresolved and must not be waived.
