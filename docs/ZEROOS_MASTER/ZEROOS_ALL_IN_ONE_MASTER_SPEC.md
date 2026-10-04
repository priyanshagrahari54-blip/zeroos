# ZEROOS — MASTER CONSOLIDATED SPECIFICATION

> Single-source consolidation of the current ZEROOS planning, architecture, requirements, validation, hardening, AI handoff, micro-requirements, and remaining-gap documents.
>
> Generated from the repository documents listed below. Source sections are preserved as much as practical; this file is a consolidation, not a replacement for executable code or test evidence.

## Source Documents
- `docs/ZEROOS_MASTER_ROADMAP.md`
- `docs/ZEROOS_MASTER_BLUEPRINT.md`
- `docs/ROADMAP.md`
- `docs/BLUEPRINT_VERIFICATION.md`
- `docs/ARCHITECTURE.md`
- `docs/HARDWARE.md`
- `docs/BOOT_SPEC.md`
- `docs/VALIDATION.md`
- `docs/ZEROOS_MASTER_PROMPT_10_STAGE.md`
- [`ZEROOS_10_STAGE_HARDENING.md`](./ZEROOS_10_STAGE_HARDENING.md)
- `docs/ZEROOS_AI_HANDOFF.md`
- `docs/ZEROOS_FULL_REQUIREMENTS_225_PLUS_QA.md`
- `docs/ZEROOS_MICRO_REQUIREMENTS_FROM_START.md`
- `docs/ZEROOS_REMAINING_GAP_CLOSURE.md`

## Important Evidence Rule
A requirement written in this document is **not** proof that the implementation exists. Host tests, QEMU tests, real-hardware tests, long-duration soak tests, security evidence, and performance measurements remain required according to the applicable gates.

---



# SOURCE 1: docs/ZEROOS_MASTER_ROADMAP.md

# ZEROOS MASTER ROADMAP

## Execution rule

Every milestone follows:
AUDIT -> DESIGN -> IMPLEMENT -> TEST -> STRESS -> MEASURE -> DOCUMENT -> INTEGRATE

Never create a basic version merely to replace it later.

## Current state

Already substantially implemented:
- Multiboot2 bootstrap
- x86-64 long mode
- bootstrap paging
- serial/debug diagnostics
- physical page bitmap allocator
- 4-level VMM
- huge-page splitting
- spinlocks and atomics
- IDT and ISR stubs
- PIC/PIT
- timer
- IRQ registration
- task creation
- cooperative context switching
- idle task
- timer preemption framework
- wait queues
- timed sleep
- zombie reaping
- preempt_count
- stack guards
- scheduler diagnostics
- QEMU CI boot path

Current priority:
Scheduler/context lifecycle must be proven stable before moving to unrelated major kernel work.

Previous failure:
invalid opcode at RIP 0x1bf and impossible task id 0x21 after scheduler activity.

Hardening already added:
- stale interrupt-frame invalidation
- tagged IRQ exit
- consumed-frame retirement
- frame bounds validation
- saved-stack validation
- current_task pointer/state validation
- scheduler diagnostics

## NEXT 50-STEP BATCH

1. Verify latest HEAD.
2. Verify latest CI run.
3. Inspect build logs.
4. Inspect serial output.
5. Inspect debug port.
6. Inspect QEMU reset output.
7. Verify scheduler reaches 100 ticks.
8. Verify worker completion.
9. Verify wait completion.
10. Verify sleep completion.
11. Verify idle fallback.
12. Audit current_task writes.
13. Audit interrupt_frame writes.
14. Audit interrupt_frame clears.
15. Audit saved_stack writes.
16. Audit state transitions.
17. Audit every scheduler selection.
18. Verify IRQ selection mode.
19. Verify cooperative selection mode.
20. Verify frame stack bounds.
21. Verify saved stack bounds.
22. Verify task ID/slot consistency.
23. Verify wait queue membership.
24. Verify sleep queue membership.
25. Prevent wait+sleep double membership.
26. Verify exit removes queue membership.
27. Verify zombie reaping.
28. Verify page_free locking interaction.
29. Verify task_lock IRQ safety.
30. Stress RBX.
31. Stress RBP.
32. Stress R12.
33. Stress R13.
34. Stress R14.
35. Stress R15.
36. Stress repeated cooperative yields.
37. Stress timer preemption.
38. Stress mixed yield/preemption.
39. Stress sleep/preemption.
40. Stress wait/preemption.
41. Stress wake/timeout race.
42. Stress exit during scheduling.
43. Stress idle transitions.
44. Add deterministic scheduler trace.
45. Add context-switch sequence numbers.
46. Add per-task transition counters.
47. Add panic context dump.
48. Re-run QEMU.
49. Re-run CI.
50. Declare scheduler stable only after evidence.

## PHASE K — Kernel maturity

- process object
- thread object
- PID/TID
- parent/child
- wait/exit
- exec
- file descriptor table
- credentials
- syscall ABI
- user stack
- syscall entry/exit
- safe user copies
- page fault path
- signal/event model
- IPC
- resource limits

## PHASE M — Memory maturity

- kernel object allocator
- slab/object caches
- per-CPU page caches
- page refcounts
- ownership
- higher-order allocation
- fragmentation tracking
- reclaim
- page cache
- swap
- higher-half kernel
- physical direct map
- user address spaces
- page faults
- demand paging
- COW
- VMAs
- guard pages
- ASLR
- PCID
- TLB shootdowns
- page table reclamation

## PHASE S — Scheduler maturity

- scheduler entities
- priorities
- runqueue abstraction
- scheduling class interface
- fair scheduler
- virtual runtime/eligibility
- real-time scheduler
- deadline evaluation
- per-CPU runqueues
- CPU affinity
- load balancing
- CPU hotplug
- tickless operation
- latency tracing
- wakeup latency benchmark
- context switch benchmark
- lock contention benchmark

## PHASE D — Drivers

- device objects
- bus objects
- driver registry
- driver matching
- PCI
- ACPI
- DMA
- MMIO
- IRQ routing
- USB
- HID
- storage
- network
- graphics
- audio
- power
- thermal

## PHASE F — Storage

- block layer
- request queues
- HDD-aware scheduling
- partitioning
- VFS
- filesystem
- journaling
- page cache
- buffered I/O
- direct I/O
- async I/O
- fsck/recovery
- snapshots
- checksums
- encryption

## PHASE N — Networking

- net device API
- packet buffers
- Ethernet
- IPv4
- IPv6
- ARP/ND
- UDP
- TCP
- DNS
- DHCP
- firewall
- socket API
- TLS integration
- network diagnostics
- Wi-Fi

## PHASE U — Userspace

- init
- service manager
- process supervisor
- IPC
- logging
- device manager
- storage manager
- network manager
- audio service
- settings service
- notification service
- package service
- update service

## PHASE G — Graphics and UI

- framebuffer
- display abstraction
- input subsystem
- GPU abstraction
- compositor
- surfaces
- window manager
- UI toolkit
- font system
- taskbar
- launcher
- universal search
- control center
- notifications
- workspace manager
- settings
- performance center
- file manager
- terminal
- screenshot/annotation
- accessibility

## PHASE A — Native applications

- ZERO Files
- ZERO Terminal
- Browser
- Notes
- PDF viewer
- Calculator
- Dictionary
- Study Center
- Code Editor
- Media Player
- Settings
- Diagnostics
- Performance Center

## PHASE T — Study platform

- notes
- rich text
- markdown
- PDF annotation
- highlighting
- flashcards
- quizzes
- revision planner
- focus timer
- dictionary
- calculator
- AI study assistant
- offline-first support

## PHASE C — Compatibility

Windows compatibility:
- user-space
- isolated
- sandboxed
- resource controlled

Android:
- user-space
- isolated
- resource controlled
- unified launcher

## PHASE X — Security

- privilege separation
- capabilities
- permissions
- sandboxing
- NX
- W^X
- ASLR
- stack protection
- package signing
- update signing
- secure boot
- TPM
- encrypted storage
- secrets service
- audit

## PHASE R — Recovery

- recovery environment
- boot repair
- filesystem check
- snapshots
- update rollback
- safe mode
- driver isolation
- recovery terminal
- network recovery
- reset

## PHASE P — Performance

Benchmark:
- boot
- idle RAM
- idle CPU
- process creation
- context switch
- syscall
- allocation
- page fault
- filesystem
- disk
- network
- UI frame latency
- app launch
- suspend/resume

Always publish methodology with results.

## PHASE Z — Release

Development:
- functionality

Preview:
- regression stability
- recovery
- update rollback
- measured resources

Stable:
- security review
- hardware matrix
- performance baseline
- recovery validation
- update validation
- documentation
- no known critical crashes

## Final dream

ZEROOS becomes a complete ecosystem:
Kernel + Drivers + Security + Storage + Network + Desktop + Native Apps + Study Platform + Compatibility + Recovery + Updates

while remaining fast, lightweight, native, reliable and measurable.


## Advanced-First Execution Rule

The roadmap is a dependency roadmap, not a basic-first/advanced-later roadmap.

Every milestone targets production architecture immediately. A blocked dependency may delay activation, but it must not force a throwaway implementation.

### Universal Production Gate

Before marking a milestone production:
- implementation is complete for its declared scope;
- public contracts are documented;
- ownership/lifetime/concurrency are verified;
- security boundary is reviewed;
- resource behavior is measured;
- failure and recovery paths are tested;
- negative/stress/fault tests pass where applicable;
- QEMU and supported hardware validation pass;
- CI is green;
- documentation matches code.

### Stage 0–5 Production Targets

Stage 0 → production engineering/reproducibility/CI
Stage 1 → production kernel and core execution architecture
Stage 2 → production userspace and service/IPC architecture
Stage 3 → production persistent storage and filesystem
Stage 4 → production hardware, drivers, networking, audio, power/thermal
Stage 5 → production graphics, compositor, shell and desktop services

Later stages remain separate product gates for native applications, security hardening, updates/recovery, compatibility, AI and ecosystem services.


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


---

# SOURCE 2: docs/ZEROOS_MASTER_BLUEPRINT.md

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


---

# SOURCE 3: docs/ROADMAP.md

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

Stages 0-5 are the existing production foundation. The next five stages are:

6. Security enforcement, packages, updates and recovery.
7. GPU acceleration, media, browser and native app foundation.
8. Windows compatibility, Android runtime foundation and gaming.
9. ZERO AI, automation, service lifecycle and performance engineering.
10. G560/hardware certification, long-duration stability and release.

The detailed authoritative definitions are in `ZEROOS_MASTER_ROADMAP.md`. The reusable agent prompt is `ZEROOS_MASTER_PROMPT_10_STAGE.md`.

All stage claims remain evidence-gated: detection != operational support, host-tested != hardware-supported, roadmap intent != implementation.


---

# SOURCE 4: docs/BLUEPRINT_VERIFICATION.md

# ZEROOS BLUEPRINT VERIFICATION

Use this document to verify that the repository contains the complete project vision.

## Required master documents

- docs/ZEROOS_MASTER_BLUEPRINT.md
- docs/ZEROOS_MASTER_ROADMAP.md
- docs/ARCHITECTURE.md
- docs/ROADMAP.md
- docs/HARDWARE.md
- docs/BOOT_SPEC.md

## Repository bridge documents

The short execution bridge is `docs/ROADMAP.md`. Current hardware and boot contracts are `docs/HARDWARE.md` and `docs/BOOT_SPEC.md`. The detailed master blueprint and master roadmap remain authoritative for the intended system scope.

## How to verify in GitHub

Open the repository and enter the docs directory.

The two ZEROOS master documents are the authoritative detailed product blueprint and execution roadmap.

## How to verify coverage

The blueprint must contain sections for:
- boot
- kernel
- scheduler
- processes
- threads
- memory
- virtual memory
- interrupts
- timers
- synchronization
- IPC
- drivers
- storage
- networking
- graphics
- audio
- input
- power
- thermal
- security
- package manager
- updates
- recovery
- diagnostics
- performance
- notifications
- focus mode
- universal search
- file manager
- terminal
- study center
- AI
- app framework
- Windows compatibility
- Android compatibility
- backup/snapshots
- testing
- observability
- release engineering
- Forge AI integration

## How to verify UI coverage

The UI roadmap must include:
- original visual language
- bottom launcher/taskbar
- centered universal search
- Forge AI entry
- app launcher
- notification center
- control center
- workspace switcher
- window snapping
- floating windows
- overview
- multi-monitor
- file manager
- terminal
- settings
- performance center
- diagnostics
- screenshot/annotation
- accessibility
- low-power rendering

## How to verify engineering quality

Every subsystem should eventually have:
- architecture
- API
- ownership
- lifecycle
- failure modes
- security model
- concurrency model
- resource budget
- tests
- stress tests
- performance measurements
- recovery behavior
- documentation

## How to verify current implementation

Do not confuse roadmap coverage with implementation status.

The roadmap describes the intended system.
The repository code determines what is actually implemented.

For every milestone:
AUDIT -> DESIGN -> IMPLEMENT -> TEST -> STRESS -> MEASURE -> DOCUMENT -> INTEGRATE

## New-chat context check

In a new ChatGPT conversation, paste the master handoff context from the previous conversation, then ask:

CONTINUE ZEROOS FROM CURRENT REPOSITORY STATE. FIRST VERIFY THE REPOSITORY AND CURRENT CI, THEN CONTINUE THE HIGHEST-PRIORITY WORK WITHOUT RESTARTING.

A correct continuation should:
1. inspect the repository
2. recognize the existing architecture
3. recognize the scheduler/context corruption history
4. verify actual CI
5. continue implementation rather than rebuilding the project from scratch
6. verify the latest scheduler stress certification and update implementation status before advancing to the next subsystem


## 10-stage synchronization rule

The repository now uses the 10-stage master execution plan in `ZEROOS_MASTER_ROADMAP.md` and the reusable execution prompt in `ZEROOS_MASTER_PROMPT_10_STAGE.md`.

Verification must distinguish:
- **implemented**: code path exists and is integrated;
- **host-tested**: deterministic host tests exercise the contract;
- **QEMU-tested**: guest boot/integration evidence exists;
- **hardware-tested**: real hardware evidence exists;
- **supported**: only when the relevant validation matrix explicitly says so.

Stages 0-5 remain the existing foundation. Stages 6-10 are the next execution block:
6 security/update/recovery;
7 GPU/media/browser/native apps;
8 Windows/Android compatibility and gaming;
9 ZERO AI/ecosystem/performance;
10 hardware certification and release.

A later stage may not bypass an earlier blocking dependency. Every stage follows AUDIT -> DESIGN -> IMPLEMENT -> TEST -> STRESS -> MEASURE -> DOCUMENT -> INTEGRATE.


---

# SOURCE 5: docs/ARCHITECTURE.md

# ZEROOS — SYSTEM ARCHITECTURE
Version: 1.0 | Master Architecture

## 1. Architectural Model
ZEROOS is a layered, modular x86-64 operating system.

Hardware
-> Boot/firmware interface
-> Kernel core
-> Hardware abstraction/drivers
-> Kernel services
-> Userspace runtime
-> System services
-> Desktop/compositor
-> Native applications
-> Compatibility runtimes
-> Optional AI/cloud/device services

Cross-cutting: security, observability, resource governance, update/recovery.

## 2. Kernel Boundary
The kernel owns:
- CPU mode and interrupt control,
- physical memory,
- virtual memory,
- scheduler,
- processes/threads,
- synchronization,
- timers,
- IPC primitives,
- syscall boundary,
- privileged hardware control,
- core security enforcement.

The kernel should not own desktop policy or application UI.

## 3. Boot Architecture
Expected progression:
Firmware/bootloader -> Multiboot2 -> early CPU setup -> paging -> long mode -> kernel entry -> memory initialization -> GDT/TSS -> VMM -> IDT/interrupts -> timers -> scheduler -> userspace bootstrap.

Each transition has a verifiable milestone.
Boot failures must identify the last completed milestone.

## 4. Memory Architecture
### Physical
Page allocator discovers usable ranges from boot memory information, reserves kernel/boot structures and returns aligned pages.

Future evolution:
bootstrap bitmap -> scalable allocator -> per-CPU caches -> object/slab allocator -> reclaim.

### Virtual
Four-level x86-64 page tables with explicit mapping permissions.
Support planned for:
- user/kernel separation,
- demand paging,
- copy-on-write,
- memory-mapped files,
- shared mappings,
- page reclaim,
- optional swap.

### Ownership
Every physical page and major kernel object needs ownership/lifetime semantics.

## 5. CPU Architecture
`CPU_ARCHITECTURE.md` defines capability discovery, the SSE2/FPU baseline,
NX enablement, invariant-TSC measurement and the per-CPU ownership record.
The current supported matrix is x86-64 QEMU/PC with a capability-probed
legacy PIC fallback or a validated LAPIC/IOAPIC timer path. When valid MADT
processor records and the Local APIC path are available, the SMP boundary
prepares and handshakes bounded AP records; otherwise it selects an explicit
BSP-only recovery mode rather than fabricating online CPUs. Multi-vCPU
scheduling and hardware certification remain later gates.

## 6. Execution Architecture
### Tasks
A task represents schedulable execution state.

### Threads
Threads represent execution contexts belonging to a process/address space.

### Scheduler
Scheduler state transitions must be explicit:
RUNNING -> RUNNABLE -> RUNNING
RUNNING -> BLOCKED
BLOCKED -> RUNNABLE
RUNNING -> ZOMBIE/EXITED where applicable.

Interrupt-return context and voluntary context-switch context must not be conflated without a documented invariant.

## 7. Interrupt Architecture
IDT dispatches exceptions and IRQs.
PIC is the validated rollback path; the current APIC/IOAPIC implementation
owns the validated timer route and the IPI mechanisms used by the SMP boundary.
Full modern multiprocessor support still requires non-timer routing, per-CPU
scheduler execution and complete SMP coordination.
Timer interrupts drive scheduling/timers.
IRQ registration must separate hardware delivery from device-driver work.

## 8. SMP Architecture
The current Stage 1 boundary provides:

- bounded ACPI MADT CPU discovery;
- a low-memory real-mode/protected-mode/long-mode AP trampoline;
- per-CPU GS records and per-CPU GDT/TSS/IDT installation;
- INIT/SIPI startup with a generation-tagged online handshake and explicit
  failure state;
- one bounded INIT/SIPI retry per AP, with late-attempt rejection and BSP-only
  recovery when an AP cannot complete initialization;
- inter-processor TLB shootdown request/acknowledgement plumbing, including
  removal of a failing AP from the target mask;
- AP idle/interrupt dispatch that never borrows the BSP scheduler context.

Per-CPU scheduler queues, AP device-IRQ ownership, FPU state policy, lock
contention instrumentation and full multi-vCPU stress/hardware certification
remain production gates. The AP boundary therefore fails closed during
startup, reports degraded mode, and never fabricates an online CPU.

## 9. Userspace Architecture
Userspace begins with an init/bootstrap process.
Core services are separate processes where practical:
- service manager,
- device manager,
- storage manager,
- network manager,
- security service,
- update service,
- desktop session,
- compositor,
- notification service.

## 10. Syscall Architecture
Syscalls are a versioned ABI.
Required groups:
process/thread, memory, files, IPC, synchronization, time, networking, devices, permissions.
User pointers are validated.
ABI structures have explicit sizes/version fields where extensibility is required.

## 11. IPC
Planned mechanisms:
- message queues,
- shared memory,
- event/notification handles,
- pipes,
- sockets.
IPC must support blocking and nonblocking modes without busy waiting.

## 12. Storage
VFS provides a stable namespace.
Filesystem drivers implement filesystem-specific operations.
Storage stack:
device -> block layer -> cache -> filesystem -> VFS -> permissions -> userspace API.

Snapshots and backups are layered above filesystem primitives.

Stage 3 implementation (details: STORAGE.md, VFS.md, ZJFS.md):
- discovery: PCI class 01 → AHCI (NCQ, MSI) and NVMe (MSI-X, per-CPU
  queues) drivers. Legacy IDE is PARTIAL (reported, not driven);
- block layer: fixed request pools, priority + aging scheduler (HDD C-SCAN,
  SSD FIFO, NVMe multi-queue), merging, flush barriers, retries, an
  event-driven timeout watchdog, and controller reset with a bounded
  drain;
- GPT validation with backup recovery. Only ZEROOS-typed ZJFS partitions
  are mounted, and disks are never auto-formatted;
- bounded page cache (dirty limits, background flusher, sticky writeback
  errors, pinned mmap pages) and the ZJFS journaling filesystem (ordered
  data, checksummed metadata, replay, orphans, fsck/repair);
- VFS with explicit refcounted ownership and uid/gid permissions, exposed
  to Ring 3 through syscalls 25–50 (`ZEROOS_ABI_FEATURE_FILES`);
- one storage manager task that becomes the event-driven storage worker
  (periodic commit, deferred process-exit cleanup).

## 13. Driver Architecture
Drivers should expose capability-oriented interfaces.
Bus enumeration identifies hardware.
Device manager loads only required drivers.
Optional drivers remain dormant.

## 14. Graphics
Display stack:
GPU/display discovery -> kernel/driver interface -> graphics service -> compositor -> shell/apps.
Hardware acceleration is preferred when available.
Software fallback is mandatory for basic operation where practical.

### Stage 5 status (display primitive + desktop platform core)
Implemented (this stage):
- Kernel display primitive (`kernel/fb.c`): Multiboot2 framebuffer discovery
  (info tag 8), physical reservation of overlapping RAM, supervisor MMIO
  mapping with readback verification, and the `DISPLAY_INFO` syscall
  (ID 51, ABI feature bit 8) returning geometry + PRESENT/degraded flags.
  Missing/unusable framebuffers degrade to the serial console milestone and
  never fail the boot. The kernel owns no desktop policy.
- Pixel-mapping syscall (`DISPLAY_PRESENT`, ID 52, feature bit 9): Ring-3
  submits damage rectangles in the native scanout format; validation is the
  host-tested `display_present_request_valid` contract (bounds, overflow,
  format/bpp agreement, stride floor and cap), writes go through
  `fb_write_pixels` row chunks with the whole source range pre-validated,
  and the boot probe verifies Ring-3 writes with a kernel readback
  (`display present contract verified`). Degraded boots fail ENOENT by
  contract; concurrent submitters stay memory-safe via the fb lock, with a
  single display-service writer as desktop policy.
- Input stack (`kernel/input.c` + `scancode_core` + `mouse_core` +
  INPUT_POLL/WAIT): i8042 controller bring-up, IRQ1 (keyboard) and IRQ12
  (mouse) routing on both the IOAPIC and PIC topologies
  (`apic_route_legacy_irq`, `pic_unmask_irq`), set-1 scancode and 3-byte
  mouse-packet decoding into the locked `input_core` queue, event-driven
  wait/wake for Ring-3, and a kernel waiter/waker certification probe
  (`input blocking wait/wake path passed`). Degradation is
  serial-reported per device (keyboard readiness gates Ring-3 start; the
  mouse is best-effort) and never fails the boot. Desktop-side event
  routing (pointer focus, click-to-focus/raise, implicit grab, overlay
  priority, clamped relative motion) lives in `userspace/desktop/` and is
  host-tested.
- Cryptographic primitives (`kernel/crypto.c`, linked into the kernel):
  ChaCha20 block/cipher, Poly1305 and AEAD_CHACHA20_POLY1305 exactly as
  specified in RFC 8439, with a streaming Poly1305 context for MAC data
  built from several buffers. Host-verified against the RFC's published
  vectors (block 2.3.2, cipher 2.4.2, MAC 2.5.2, key generation 2.6.2,
  AEAD 2.8.2) plus wrong-key/tampered-tag/tampered-ciphertext/tampered-AAD
  negatives that must fail with authenticated-output zeroing. Policy —
  key storage, nonce generation, who may encrypt what — deliberately
  stays with future callers (vault, updates, privacy centre).
- Automation framework (`userspace/desktop/src/automation.c`): bounded
  event/rule engine (32 rules, 64-entry audit ring) with explicit
  permission grants, per-rule cooldowns, lifetime fire caps, injected
  action ops (notify/settings/callback/log) and drainable audit records
  (rule, event, result, tick) for the privacy center — host-tested.
- Session/shell process (`userspace/session/session.c`, embedded and
  launched by `kernel/session.c`): the first Ring-3 shell process binds
  the display service to the real DISPLAY_INFO/DISPLAY_PRESENT syscalls,
  rasterizes a compositor frame into a staging framebuffer and submits
  the damaged region, pumps INPUT_POLL/INPUT_WAIT through the desktop
  input router, and certifies each contract with serial milestones
  (live and degraded display paths both accepted; reaped cleanly).
  Its launcher maps a 16-page (64 KiB) downward-growing stack
  (`ZEROOS_USER_STACK_PAGES`) because `session_main` reserves >17 KiB of
  frame space in one function — a single mapped page faulted below the
  stack region on the first local store (CI-observed before the fix).
- Desktop platform core (`userspace/desktop/`): fixed-capacity, zero-heap,
  freestanding-clean modules for lifecycle, resource governor, display
  service (scanout submit gating), window
  system, compositor (retained scene, damage, occlusion, pacing, cache,
  software fallback), Universal Search, schema-driven settings,
  notifications, accessibility, i18n (en+hi), and service watchdog —
  gated by `make desktop-check` (3500+ assertions, hosted + freestanding).

- Browser tab lifecycle (`userspace/desktop/src/browser.c`): the
  ACTIVE -> IDLE -> FROZEN -> DISCARDED ladder (Phase 8.5) driven by an
  injected monotonic clock, metadata-survives-discard reloads, crash
  states with explicit recovery (no auto-restart, double-crash and
  early-focus rejected), bounded at 16 tabs with capacity errors —
  host-tested.  Rendering engine and tab UI remain.
- AI broker (`userspace/desktop/src/ai.c`): Part F contracts are in
  section 17; permissions, downgrade, dormancy and wipe semantics are
  host-tested.
- Windows compatibility core (`userspace/compat/`): Part C contracts
  are in section 18; host-tested via `make compat-check`.
- ZERO Bar core (`userspace/desktop/src/bar.c`): applet layout with a
  flexible title area, pointer/keyboard activation with action routing,
  quick toggles with explicit provider denial accounting, notification
  badges, DPI-scaled physical width — host-tested.  Shell chrome
  rendering remains in the not-yet list.
- Launcher core (`userspace/desktop/src/launcher.c`): bounded app
  registry, case-insensitive query with prefix-over-substring ranking
  and recency ties, launch dedup (EBUSY), hook-driven failure states,
  empty-state recents — host-tested.
- Capability gate (`userspace/desktop/src/capability.c`): per-service
  activation with explicit masks, runtime grant/revoke, check-gated
  privileged ops with an audit ring, and crash/stop deactivation that
  drops every grant (no sticky privileges across restarts) —
  host-tested.  Wiring services onto the gate and the rest of Part B
  (firewall, vault, snapshots, transactional updates) remain.  The
  launcher and ZERO Bar are wired to the gate (spawn and
  settings-write capabilities respectively).
- Metrics recorder (`userspace/desktop/src/metrics.c`): named
  counters plus a present-interval histogram with percentile and
  average reads; the display service records presented/refused/failed
  outcomes and inter-present intervals through the optional hook, and
  the module is part of the freestanding session link — host-tested.
- Transactional update core (`userspace/desktop/src/update.c`):
  section 20 pipeline (download → verify → stage → preflight →
  activate → health check → commit) as an explicit state machine with
  injected side-effect hooks, rollback on health/activation failure,
  verify failure that never activates, cancel only before activation,
  and rejected-transition accounting — host-tested.
- Strict URL parser (`userspace/desktop/src/url.c`): http/https/about
  only; javascript:, data:, file: and userinfo never navigate;
  control characters, label rules, port ranges and percent-escapes
  (including control-byte decodes like %00/%0A) rejected with named
  reasons — host-tested.
- PE image validator (`userspace/compat/src/pe.c`): first loader
  stage — MZ/PE signature, x86-64 machine, PE32+ format, section
  table and entry/header bounds against truncation with explicit
  diagnostics (section 18) — host-tested.
- Study Center core (`userspace/desktop/src/study.c`): deck/card
  registry with a bounded spaced-repetition ladder (0/1/3/7/21/60
  days), lapse and ease accounting, due-card selection and
  focus-session timing — host-tested; heavier surfaces attach later.
- FPS monitor (`userspace/desktop/src/fps.c`): clock-injected
  sliding-window FPS, frame-time percentiles, budget-breach counting
  and the 8/16/33 ms performance profiles — host-tested; overlay and
  controller support remain in the not-yet list.
- Snapshot manager (`userspace/desktop/src/snapshot.c`): bounded
  registry with CREATING→READY/FAILED lifecycle, hook-driven restore
  and capacity pruning that refuses when the discard hook fails —
  host-tested.
- Encrypted vault (`userspace/desktop/src/vault.c`): AEAD
  (ChaCha20-Poly1305) wrapping of every secret under an injected
  32-byte master key — no plaintext at rest, explicit auth failure on
  tamper or wrong key, key wiped on lock — host-tested against the
  certified crypto module.
- Navigation controller (`userspace/desktop/src/nav.c`): strict-URL
  gated go/back/forward with bounded history and blocked-scheme
  accounting — host-tested.
- Firewall policy engine (`userspace/desktop/src/firewall.c`): up to
  32 first-match rules (direction/protocol/port+IP ranges/app scope/
  established-only), default deny, explicit flow validation and
  allow/deny counters — host-tested; the kernel packet path binds to
  it later.  Stress suite (`test_stress.c`) drives sustained
  capacity cycles across browser/vault/firewall/snapshot/fps with
  exact end-state accounting — host-tested.
- Sandbox policy core (`userspace/desktop/src/sandbox.c`): named
  profiles with explicit capability masks (fs/net/spawn/display/
  audio/device classes), fail-closed checks for unknown profiles and
  classes, per-profile denial counters and an audit ring —
  host-tested; seccomp/namespace enforcement binds later.
- Clipboard service (`userspace/desktop/src/clipboard.c`): bounded
  11-entry history with owner/format metadata, sensitive clippings
  that become current but never enter history, current-excluding
  depth cycling, eviction of oldest and a clear-all privacy wipe —
  host-tested.
- Downloads manager (`userspace/desktop/src/downloads.c`): bounded
  queue with explicit QUEUED/RUNNING/DONE/FAILED/CANCELED lifecycle,
  single-active-transfer policy (cooperative sharing), monotonic
  progress with regression rejection, start-hook failure accounting
  and terminal-state immutability — host-tested.
- Media policy core (`userspace/desktop/src/media.c`, part H): origin
  registry with explicit rights (play/cache/export/sync), default
  deny for unregistered origins, and an unconditional DRM gate —
  protected items are refused with per-reason counters; the contract
  contains no bypass path by construction.  Host-tested.
- Overview/expose model (`userspace/desktop/src/overview.c`, part
  A): ceil(sqrt) thumbnail grid over a work area with edge-inclusive
  gaps, exact hit-testing (gaps and empty cells excluded), keyboard
  focus cycling with wrap, remove-with-relayout and degenerate-area
  rejection (never zero-size thumbs).  Host-tested with exact grid
  math.
- File manager core (`userspace/desktop/src/filemgr.c`, shell
  surface): injected directory source (session binds
  `zeroos_readdir`; tests bind memory — never a faked filesystem),
  stable name/size/mtime sorts, hidden filtering with visible-index
  selection, bounded 16-deep path history with back/forward and
  forward-drop discipline, capacity truncation flagged (not hidden),
  source errno propagation with explicit failed state, and
  permission-gated remove/mkdir (join sanitation rejects traversal,
  denied actors never touch ops).  Host-tested.
- Formula engine (`userspace/desktop/src/formula.c`, part G):
  documented math subset — precedence, right-associative INTEGER
  powers (non-integer exponents are explicit errors, never
  silent NaN), unary minus above power so `-2^2 = -4`, \frac and
  \sqrt (Newton iteration, negative argument errors), variable
  bindings across parses, 128-char input / 64-node arena / depth-16
  caps all failing with positions.  Host-tested.
- AI study assistant (`zd_study_assist_request/submit`, part G):
  builds a SUMMARIZE broker request from study context (deck + due
  card, SELECTION context bit only, payload truncated safely) and
  submits through the AI broker — permission denial happens in the
  broker up front, so an ungranted request never reaches a backend.
  Fixed an include-guard mismatch in `ai.h` (define did not match
  ifndef) exposed by the new include path; all desktop headers are
  now guard-audited.
- Study notes + dictionary (`notes.c`/`dict.c`, part G): bounded
  notebook (64 notes, 256-byte bodies, pin, case-insensitive
  substring search over title+body with documented cap semantics)
  and a dictionary loaded from an injected word source (case-
  insensitive sort/dedup, binary-search lookup, prefix suggestions
  with total-vs-written accounting; load errors leave the dict
  unreadable at -95).  No bundled corpus is claimed — the word list
  binds later, tests inject fixtures.  Host-tested.
- OCR contract (`userspace/desktop/src/ocr.c`, part G): honest
  capability gate — no engine linked in this build, availability 0,
  every call fails -95 AFTER full argument validation; pluggable
  engine registration path tested with a test fixture.  No text is
  ever fabricated and no accuracy is claimed until an engine lands.
- Terminal core (`userspace/desktop/src/term.c`, shell surface):
  bounded 24x80 (max 120x240) cell grid with streaming parser —
  CSI cursor/erase/SGR (colors + bold/underline/inverse), LF/CR/BS/
  TAB, column wrap, 128-line scrollback ring (oldest evicted, order
  preserved), UTF-8 collected across writes with invalid bytes
  counted, OSC titles consumed (BEL and ESC \ terminators), ESC
  intermediates such as `ESC ( B` drained so their final bytes never
  leak into the screen, oversized CSI counted once and drained.
  Unknown sequences are consumed + counted, never shown as text.
  No PTY yet: child binding comes later through zd_term_write.
- Privacy centre (`userspace/desktop/src/privacy.c`, part B):
  read-only aggregation of denial/refusal counters from sandbox,
  firewall, media, ecosystem and clipboard engines into a domain
  breakdown (filesystem/network/content/sync) plus a documented risk
  ladder (none/watch/review/elevated/act-now with explicit
  thresholds 1/25/50/200 and review-over-elevated precedence),
  sensitive-clip events surfaced as protected, optional sources
  contribute zero rather than failing — host-tested against real
  engine counters.
- Update<->snapshot production binding (`zd_snapshots_bind_update`,
  part B): the update machine's `stage_apply` hook captures a fresh
  "update" snapshot (capture errors abort staging atomically) and
  `rollback` restores the newest READY one (none available -> -2,
  which the machine converts to FAILED — never a silent half-
  rollback).  Wiring fix: `stage_apply` and the snapshot `capture`
  hook were documented but never invoked; both are now called during
  their transitions with fail-closed semantics and regression tests.
- Cloud/device ecosystem core (`userspace/desktop/src/eco.c`, part
  J): offline-first sync queue — pairing grants zero permissions,
  grants/revoke operate on exact bits, enqueue demands the paired
  device + that bit, revoke/unpair drop dependent pending entries,
  offline flush transmits nothing while keeping the queue intact,
  and online flush re-validates pairing and permissions per entry
  before delivery (refusals counted per reason).  Host-tested.
- Gaming core (`userspace/desktop/src/gaming.c`, part I): per-app
  profiles (eco/balanced/perf, target fps, overlay + low-latency
  flags), physical-to-logical button remapping with unmapped
  accounting, and a cooperative yield matrix (degrade / overlay-off /
  target-floor) decided from measured fps and memory pressure —
  decisions only; sampling stays in the fps ring.  Host-tested.
- PDF document contract (`userspace/desktop/src/pdf.c`, part G):
  bounded zero-heap subset parser over caller-owned bytes — header
  and version check, `/Type /Page` walk with object-number recovery,
  `/Contents N 0 R` resolution to content streams, Tj/TJ literal
  extraction with escapes and rollback of non-show operands, 64-page
  and 1 KiB-per-page caps, and explicit unsupported failures (`-95`
  for FlateDecode/encryption, `-22` malformed) with status names for
  UI surfaces.  Hand-authored fixtures cover both pages, filtered,
  encrypted, truncated, escaped, over-cap and contentless cases.
- Fault-injection suite (`userspace/desktop/tests/test_fault.c`):
  seeded LCG drives 512 rounds of invalid inputs across settings,
  notify, clipboard, downloads, snapshots, firewall, sandbox,
  lifecycle and the performance center; every return must stay in
  the errno range, caps must hold, state machines must reject
  wrong-state sequences, and known-good paths must still succeed
  afterwards — part M FAULT evidence, reproducible from a fixed seed.
- Performance center (`userspace/desktop/src/perfcenter.c`): ring of
  frame-time samples with p50/p95/worst percentiles and a health
  ladder (good/fair/poor/critical) driven by measured thresholds —
  p95 vs frame budget, FPS floor, memory-pressure bands, thermal
  throttle, eco governor and staged-update flags — emitting
  combinable, concrete suggestions (close background, lower detail,
  reduce motion, cool down, wait for update, check memory).
  Host-tested across every band plus ring-wrap.
- Built-in search providers (`providers.c`): the commands provider
  (register/execute with prefix scoring and duplicate rejection) and
  the diagnostics provider (bounded health lines), both honoring the
  pipeline cancellation contract; apps/files/settings providers bind
  at the shell layer — host-tested.
- Update payload verification (`zd_update_verify_payload`): AEAD
  with producer nonce, injected update key and the version bound as
  AAD — tamper, cross-version replay and wrong-key all return the
  explicit failure (section 20).

Not yet implemented (contracts defined, explicit in PHASES.md):
- Shell UI chrome (ZERO Bar, launcher, overview surfaces) on top of the
  session process and its consumers for the automation rules, browser
  rendering engine and tab UI, GPU driver beyond scanout, mouse
  wheel/extended aux protocols, USB HID devices, cloud/device services
  (offline-first behavior, explicit per-device permissions).

### UI condition contract (part A cross-cutting)

`ui.h` owns the shared condition vocabulary mandated by the prompt:
NORMAL / LOADING / EMPTY / ERROR / OFFLINE / PERMISSION_DENIED /
LOW_RESOURCE, plus REDUCED_MOTION / KEYBOARD_NAV / LOCALIZED overlay
modes that never replace the condition.  Labels are `state.*` catalog
keys resolved through the i18n layer (English + Hindi, zero
fallbacks enforced by tests), and negative `ZD_E*` returns map to
conditions with a documented table.  Working per-surface state
machines are NOT redesigned - they map into the contract:

| Surface state machine | Maps to conditions |
| --- | --- |
| filemgr `hist_state` 0/1/2/3 | EMPTY / LOADING / NORMAL / ERROR |
| nav `ZD_NAV_*` | EMPTY / LOADING / NORMAL / ERROR |
| display `ZD_DISPLAY_*` | ATTACHING->LOADING, LIVE->NORMAL, DEGRADED/SUSPENDED->ERROR |
| downloads `ZD_DL_*` | QUEUED/RUNNING->LOADING, DONE/CANCELED->NORMAL, FAILED->ERROR |
| launcher `ZD_LAUNCH_*` | PENDING/RUNNING->LOADING, IDLE->NORMAL, FAILED->ERROR |
| snapshot `ZD_SNAP_*` | EMPTY->EMPTY, CREATING->LOADING, READY->NORMAL, FAILED->ERROR |
| notify lifecycle ACTIVE/DEFERRED | NORMAL while visible (domain lifecycle stays) |
| eco connectivity offline | OFFLINE |
| term parser `pstate` | internal parser phase - not a UI condition |

Surfaces adopt incrementally; the contract + mapping is tested
(`test_ui.c`: transitions, mode flags, interactivity matrix, errno
mapping, localized labels for all seven conditions).

### Stage 5 binding inventory (production status)

Every desktop core introduced in Stage 5 is host-tested with real
assertions (no mocks presented as functionality).  This inventory is
the authoritative list of what still connects those cores to live
subsystems — each pending binding is a documented next step, never a
claim:

| Core | Host evidence | Pending live binding |
| --- | --- | --- |
| Display service + compositor + input router | session process wired to `DISPLAY_PRESENT`/`INPUT_*` + boot certification | multi-GPU beyond the single present path |
| File manager | source-injected listing/sort/select/ops suites | session binds `zeroos_readdir` (ABI file calls 25–50) |
| Terminal | parser/scrollback/SGR suites | PTY or pipe + child spawn for shell output |
| Cloud/device sync | offline queue + permission suites | transport over kernel sockets; real device pairing |
| Firewall engine | first-match/default-deny suites | kernel packet-path hook (userspace policy today) |
| Sandbox profiles | fail-closed gates + audit suites | OS enforcement (seccomp/namespace) hooks |
| Update payload verification | AEAD vectors + tamper/replay suites | key provisioning (OS-injected key; no PKI yet) |
| OCR | capability gate + pluggable-engine path (fixture only) | licensed engine; until then `-95`, no accuracy claims |
| PDF subset | hand-authored fixtures (plain streams) | filtered/encrypted docs stay `-95` (explicit) |
| Media/gaming/ecosystem policy | gate/yield/permission suites | decoder backends, controller hardware, transports |
| Search providers | commands/diagnostics live; pipeline suites | apps/files/settings providers bind to shell services |
| Privacy centre | real-counter aggregation suites | session wiring over live engines |
| Automation/AI/browser/watchdog/lifecycle/governor | suites + CI boot milestones | in-session activation (already exercised in guest CI where noted) |
| UI condition contract | `test_ui.c` (7 conditions, modes, errno map, hi/en labels) | per-surface adoption is a mapping (table above), no surface redesign |
| Real hardware | — | **not run — no claim** (VALIDATION row stays open) |

## 15. Networking
Network stack is independent of desktop UI.
Network manager handles links/configuration.
Firewall enforces policy near the packet path.
Diagnostics expose DNS, route, link and latency information.

## 16. Resource Governor
Every service declares:
priority, memory budget, CPU budget, I/O class, wake policy and suspension policy.

Lifecycle:
DORMANT -> WARM -> ACTIVE -> THROTTLED -> SUSPENDED -> STOPPED.

The governor reacts to:
foreground workload, memory pressure, battery, thermal state, I/O pressure and user mode.

## 17. AI Architecture
AI service has:
request broker -> permission check -> model/backend selector -> context provider -> inference -> action executor.
Model execution may use CPU/GPU/NPU when available.
No model remains actively generating or polling when no request exists.
Status: the local broker (`userspace/desktop/src/ai.c`) implements
request -> permission check -> backend select -> run with explicit
grants (context bits, remote egress, persistence), automatic
remote-to-local downgrade when the egress grant is absent, a bounded
queue with drop counters, injected backend hooks (a missing hook counts
a failure, never a fake success), payload wipe on drain and
dormant-until-submit residency — host-tested in `make desktop-check`.
Model adapters, context providers and action executors attach on top of
these contracts later.  This ZEROOS platform is deliberately separate
from Forge AI.

## 18. Compatibility Architecture
Windows:
Application -> Win32/Win64 API layer -> compatibility runtime -> POSIX-like/native ZEROOS services -> kernel ABI.
Status: the compatibility core (`userspace/compat/`, `make
compat-check`) is implemented and host-tested: explicit process
lifecycle where INSTALLED != RUNNING (start/stop/crash with full
residency release when stopped — dormant when unused), drive-letter and
registry-hive translation with named rejections for UNC paths, NTFS
alternate streams and unknown hives, REG_SZ/DWORD/BINARY validation,
and DLL refcount bookkeeping with case-insensitive singleton loading.
The PE loader and API translation layers build on these contracts;
unsupported constructs fail with explicit diagnostics, never silent
emulation.

Android:
Android application -> Android framework/runtime -> graphics/audio/input/network adapters -> ZEROOS services.
Runtime is separately managed and loaded on demand.
Documented baseline (chosen now, before any claim): AOSP 14
(Android 14, API level 34), primary ABI arm64-v8a, security
bulletin level tracked at integration time; the runtime executes in
an isolated userspace compartment with its own lifecycle and no
privileged ZEROOS IPC by default.
Claims policy: public compatibility statements may list only
devices/versions exercised by the tested matrix.  Current tested
matrix: **empty — no Android application or device is claimed
supported** until CI/real-hardware rows in `VALIDATION.md` record
them.

## 19. Security Architecture
Boot trust, kernel privilege separation, userspace isolation, permissions, process capabilities, encrypted storage (symmetric foundation: RFC 8439 ChaCha20-Poly1305 primitives in `kernel/crypto.c`), firewall, application sandboxing, update verification and recovery.

Security services must remain available under ordinary load and become more conservative under suspicious activity.

## 20. Update Architecture
Use staged updates:
download -> verify -> stage -> preflight -> activate -> health check -> commit or rollback.
System-critical updates should use an A/B or equivalent atomic strategy where storage permits.

## 21. Observability
Unified event model:
boot milestones, kernel events, service events, driver faults, resource pressure, crash reports and update status.
Telemetry must be opt-in where it leaves the device. Local diagnostics should be useful without cloud access.

## 22. Architectural Invariants
- Kernel never trusts userspace.
- Drivers cannot bypass ownership rules.
- Foreground work cannot be starved by background maintenance.
- A feature must have a lifecycle.
- A public ABI cannot change silently.
- Recovery paths are part of the feature, not post-processing.


## 22. Advanced-First Architecture Maturity

The architecture is designed for production maturity from the outset. Stage ordering is dependency ordering, not a justification for simplistic subsystem implementations.

### Kernel

The production kernel architecture must be able to evolve toward:
- SMP/per-CPU execution;
- robust scheduling classes;
- scalable memory allocation;
- demand paging/COW/reclaim;
- robust object lifetime;
- hardened user/kernel boundaries;
- structured diagnostics;
- driver isolation.

### Storage

The production storage boundary must support:
- queued I/O;
- device-specific scheduling;
- page cache/writeback;
- filesystem consistency;
- recovery;
- snapshots;
- encryption/integrity;
- safe update integration.

### Drivers

Drivers are capability-oriented, lifecycle-managed and failure-aware. DMA, IRQ, hotplug, suspend/resume and error recovery are first-class concerns.

### Graphics/Desktop

Graphics must separate hardware, graphics service, compositor, shell and application UI. Frame scheduling, damage tracking, resource ownership, accessibility and crash isolation are architectural requirements.

### Production Boundary Rule

No compatibility runtime, AI subsystem, desktop component or native application may become a hidden dependency of the kernel. Conversely, kernel contracts must be sufficiently mature that userspace does not depend on undocumented implementation details.

## Stage 4 hardware boundary
Legacy PCI mechanism #1 discovery is an observation-only service. It scans
segment 0 and publishes bounded device identity, BAR snapshots and conventional
capability offsets, and sizes BARs (display-class functions excepted). Only
drivers that claim a function (AHCI and NVMe, Stage 3 §12) enable decoding
and bus mastering, map BARs and activate MSI/MSI-X; BARs are never
reassigned and other devices stay unbound. ACPI remains validated
RSDP/root/MADT topology discovery; AML and power management are not present.
DMA/IOMMU, USB, display, audio, and complete network services are not implemented. The networking foundation now includes Ethernet framing, bounded IPv6 extension parsing, IPv4 firewall/UDP validation, a host-tested netif→UDP endpoint callback path, and IPv4/UDP egress with checksums when the caller supplies a resolved next-hop MAC; it still has no concrete NIC, integrated ARP/route resolution, system sockets, or userspace network service. A standalone IPv4 ARP parser/builder and expiring neighbor-cache helper are host-tested; cache learning rejects unsolicited replies and requires a matching live request, but the helper is not wired to packet ingress/egress. PS/2 keyboard AND mouse input ARE wired: `kernel/input.c` configures the i8042 controller, routes IRQ1 and IRQ12 on IOAPIC/PIC topologies, decodes scancodes and mouse packets into the `input_core` queue (which the driver serializes with its own lock), and serves the INPUT_POLL/INPUT_WAIT syscalls; USB HID, mouse wheel/extended aux packets remain unwired, and event delivery to Ring-3 is proven by boot certification rather than live keystrokes in CI. Ethernet/ARP, IPv4 and bounded IPv6 extension-header parsing, UDP/TCP/DHCP/DNS parser/state helpers, bounded route/flow tables, the host-tested `netif`→IPv4/UDP dispatcher→owner-bound UDP endpoint queue, an owner-scoped DMA callback contract, overlap-checked typed resource registry, and generic driver lifecycle/resource-cleanup state machine are testable foundations, not integrated kernel device-service implementations.
The authoritative detected-versus-operational support matrix is in
[`HARDWARE.md`](HARDWARE.md). Detection must not be represented as support.
\n\n## 10-stage architecture alignment\n\nThe architecture follows the authoritative 10-stage execution plan in `ZEROOS_MASTER_ROADMAP.md`: Stages 0-5 are the existing foundation; Stages 6-10 cover security/update/recovery, GPU/media/browser/native apps, Windows/Android compatibility and gaming, ZERO AI/ecosystem/performance, and hardware certification/release.\n\nCross-cutting invariants remain: installed != loaded != running != active; event-driven over polling; explicit ownership/lifetime; security boundaries first; no compatibility runtime or ZERO AI dependency in the kernel; measured resource behavior; detection != operational support. Controller/gamepad support remains out of scope.\n

---

# SOURCE 6: docs/HARDWARE.md

# ZEROOS Hardware Architecture

## Supported-device matrix (Stage 4 baseline)

| Area | Detected / implemented | Operational support | Unsupported / explicitly unclaimed |
|---|---|---|---|
| x86-64 CPU, RAM, serial, PIT/PIC | Yes | Boot, memory management, diagnostics, timer | Other architectures |
| ACPI RSDP/root/MADT | Validated discovery | Interrupt topology discovery and existing APIC path | AML, ACPI power/thermal/battery, suspend/resume |
| PCI segment 0 | Mechanism #1 scan, identity/class, BAR sizing (not display class), bounded capabilities incl. MSI/MSI-X/PCIe; typed resource interval registry (standalone) | Claimed AHCI/NVMe functions: decoding + bus mastering, uncached NX BAR mapping, MSI/MSI-X activation. All others remain unbound | ECAM/MCFG, other segments, BAR assignment, overlap validation of BARs against the resource map, surprise-removal hotplug |
| DMA/IOMMU | No IOMMU backend | Owner-scoped DMA map/unmap API contract with bounded mapping tokens and teardown refusal while mappings remain | Hardware translation/cache-coherency implementation, bounce buffers, isolation/domain setup, device reset integration |
| USB/input/display/audio/network | Event-driven bounded `netif` Ethernet frame queue with required caller-provided IRQ-safe lock callbacks; PS/2 keyboard + mouse (i8042, IRQ1/IRQ12, set-1 decode + 3-byte aux packets → input queue → INPUT_POLL/WAIT); no USB HCD | Descriptor framing validator; fixed-capacity input queue/registry; bounded audio ring; display mode validator; Ethernet/VLAN and ARP framing parsers; IPv4 validation; bounded IPv6 extension-header parsing (Hop-by-Hop, Routing, Fragment, AH, Destination; no ESP, jumbograms, or reassembly); default-deny IPv4 policy and bounded flow tracking/routes; UDP framing; host-tested bounded IPv4 UDP ingress dispatcher with destination checks, default-deny firewall policy and checksum verification; bounded owner-tagged UDP endpoint/receive-queue table with generation handles (no syscalls); checked IPv4/UDP Ethernet transmit builder with DF, IPv4 and UDP checksums via caller-resolved next-hop MAC; host-tested IPv4 ARP request/reply validation/builders and fixed-capacity expiring neighbor cache that admits replies only for outstanding, matching local requests (not connected to a NIC or transmit path); limited TCP connection-state helper/timeouts, DHCP lease-state machine and option parsing, DNS message validator; lifecycle/resource cleanup state machine | Mouse wheel/extended aux protocols (4-byte/ID-prefixed), HID decoding, USB HCD/device enumeration, DMA/audio engine, display scanout/GPU, NIC, IPv6 sockets and user-visible POSIX/BSD socket syscalls, DNS/DHCP live services, ARP/neighbor lookup integration with a NIC/transmit path, routing integration |
| Storage (Stage 3) | AHCI (SATA disks) and NVMe (namespace 1) drivers | MSI/MSI-X completion, NCQ / multi-queue, timeouts, retries, controller reset with in-flight drain, cooperative removal; GPT; ZJFS journaling filesystem; page cache; file syscalls; see STORAGE.md | Legacy IDE/ATAPI, port multipliers, AHCI INTx (polled fallback only), NVMe multi-namespace, TRIM, IOMMU isolation |

## PCI observation contract

`pci_init()` (`kernel/pci.{h,c}`, run once by the storage manager) scans the
256 buses and 32 slots of legacy configuration mechanism #1 on segment 0,
respecting multifunction headers, into a fixed table of
`ZEROOS_PCI_MAX_DEVICES` (64) functions. Overflow is counted and logged, never
silent. For each function it records:
- identity and class, and the INTx line and pin;
- MSI, MSI-X and PCIe capability offsets, from a walk bounded at 48 entries
  so a cyclic list terminates;
- BAR base, size and type. BARs are sized with I/O and memory decoding
  disabled and restored immediately afterwards.

Display controllers (class 03h) are **not** sized, because their
framebuffer is live (`kernel/fb.c`). Their BAR bases are recorded
read-only and cannot be mapped through `pci_map_bar()`.

Every class-01h function is logged (`PCI storage …`). Discovery alone
changes nothing else. Only a driver that claims a function (currently AHCI
and NVMe, Stage 3) enables decoding and bus mastering
(`pci_enable_device`), maps BARs uncached/NX into the kernel MMIO window
(`pci_map_bar`), and programs MSI or MSI-X targeting the BSP LAPIC.
Unclaimed devices, including legacy IDE, stay unbound. PCIe ECAM and
nonzero segments are not supported.

The Stage 4/5 branch briefly had a separate observation-only
`pci_enumerate()`/`pci_inventory` API. When the branches were merged, it
was folded into this single PCI subsystem, so there is exactly one owner
of configuration space.

Known gaps (PARTIAL):
- BAR ranges are not yet validated for overlap against the
  physical-memory/resource map before mapping. Firmware-assigned addresses
  are trusted, and no BAR is ever reassigned.
- Generic discovery sizes BARs without first quiescing the device. It
  disables decoding only for the brief sizing window.
- DMA runs without an IOMMU. The storage drivers DMA only into
  kernel-owned pages that they allocate and free, so a malicious or
  faulty device is not contained.
- MSI/MSI-X is enabled only by drivers that claim a device. Everything
  else stays disabled.

## Driver lifecycle and safety boundary

Future drivers must implement DISCOVER → MATCH → PROBE → RESOURCE ACQUIRE →
DMA/IRQ SETUP → INITIALIZE → REGISTER → SERVE → ERROR RECOVERY → SUSPEND →
RESUME → REMOVE → CLEANUP. Each acquired resource has one owner and one
release path. Unsupported hardware is reported unbound; no universal hardware
support is claimed. Device-specific execution belongs behind stable interfaces,
not in desktop policy.

## Existing boot target

The current target is x86-64 under GRUB/Multiboot2, with serial COM1, physical
page discovery/allocation, x86-64 virtual memory, IDT/ISR entry, 8259 PIC,
PIT, ACPI RSDP/root/MADT validation, and the bounded APIC/SMP functionality
documented in `ACPI.md`. QEMU and physical-device certification are distinct;
no real-hardware certification is implied by compilation.
\n\n## 10-stage hardware alignment\n\nHardware certification is evidence-gated. The Lenovo G560 class is the reference low-resource profile, but exact CPU, RAM, GPU, display and network adapter must be detected per machine. Native 1366x768 is the reference display target; 1080p source playback is a capability test, not a panel-resolution claim. Network throughput is recorded from the actual adapter/link. Thermal, power, HDD, memory-pressure and long-duration results must be recorded in the validation matrix.\n\nDo not convert detection, enumeration or host tests into support claims. Controller/gamepad support is not part of the current ZEROOS hardware target.\n

---

# SOURCE 7: docs/BOOT_SPEC.md

# ZEROOS Boot Specification

## Current boot contract

1. GRUB loads the ZEROOS kernel and supplies a Multiboot2 information structure.
2. The boot assembly establishes the x86-64 execution environment, installs a
   bootstrap-stack guard and transfers control to the kernel.
3. kernel_main validates the stack guard and Multiboot2 magic value before
   consuming the handoff.
4. The kernel discovers physical memory from the Multiboot2 memory map.
5. Virtual memory, synchronization, interrupt routing, and timer infrastructure are initialized in kernel bootstrap order.
6. Scheduler self-tests run before normal kernel task execution.
7. The first scheduler task is entered through the cooperative task context path.
8. Timer IRQs are handled by the normalized ISR path; scheduling is deferred to IRQ exit.

## Boot invariants

- Multiboot2 magic must match the expected value.
- The bootstrap stack guard must remain intact before C initialization.
- Kernel memory and boot metadata must remain reserved by the memory subsystem.
- IDT must be installed before interrupts are enabled.
- Timer IRQ registration must succeed before scheduler preemption is enabled.
- The scheduler must have a valid idle task before the bootstrap context is parked.
- Fatal architectural exceptions halt with serial diagnostics.

## Future boot stages

UEFI-native loading, richer firmware discovery, measured boot, and secure-boot
integration remain roadmap work. The current GRUB/Multiboot2 path validates
ACPI MADT topology, activates a validated LAPIC/IOAPIC timer route, and has a
bounded AP startup boundary with one generation-checked retry and explicit
BSP-only recovery; complete SMP scheduling and broader hardware support remain
later production gates.


---

# SOURCE 8: docs/VALIDATION.md

# ZEROOS — Stage 5 Validation and Support Matrix
Status: living evidence ledger for Stage 5 and the binding evidence policy for Stages 1-10.

The 10-stage hardening acceptance checklist is authoritative:
[`ZEROOS_10_STAGE_HARDENING.md`](./ZEROOS_10_STAGE_HARDENING.md)

Every row is backed by a command that runs in this repository or by a CI run of a recorded commit. Where evidence does not exist yet, the row says so — no claim is made without it.

## Binding 10-stage rule

For Stages 1-10, a capability can move to Supported only when the corresponding item in [ZEROOS_10_STAGE_HARDENING.md](./ZEROOS_10_STAGE_HARDENING.md) has executable evidence at the required execution class.

A host test cannot close a QEMU gate. QEMU cannot close a real-hardware gate. Detection, parsing, an architectural contract, or a mock cannot close an operational-support claim.

Never claim literal zero CPU/RAM/latency, universal compatibility, or untested hardware support.

## Evidence classes

| Class | Evidence | Status |
|---|---|---|
| UNIT | Host unit suites | Recorded green where listed |
| INTEGRATION | Full guest boot and service lifecycle | Recorded green where listed |
| NEGATIVE | Error/denial paths | Required |
| FAULT | Injected failures and recovery | Required |
| STRESS | Capacity/concurrency loops | Required |
| SOAK | Long-duration stability | Required; hardware pending |
| SECURITY | Enforcement and crypto boundaries | Host coverage exists; kernel enforcement pending |
| RECOVERY | Crash/update/filesystem recovery | Covered paths recorded; full recovery gate pending |
| QEMU | Guest certification | Required |
| REAL-HARDWARE | Physical certified ISO | Not run — no claim |
| PERFORMANCE | CPU/RAM/I/O/GPU/latency/thermal measurements | On-device evidence pending |

## Stage 5 current support limits

- Graphics/compositor/window/input foundations exist; full GPU acceleration is not yet a certified operational path.
- Windows compatibility core and PE validation exist; no Windows runtime claim.
- Android baseline is documented; runtime is absent and unclaimed.
- Browser lifecycle exists; renderer/engine binding remains pending.
- ZERO AI broker exists; backend/inference adapters remain pending.
- Media policy exists; decoder/playback integration remains pending.
- Gaming profiles/metrics exist; live game runtime remains pending.
- Real-hardware validation remains open.

## Release rule

Stage 10 remains blocked until Stages 1-9 have their declared executable evidence and the physical hardware matrix, recovery, security, performance and long-duration soak gates are complete.

---

# SOURCE 9: docs/ZEROOS_MASTER_PROMPT_10_STAGE.md

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

The next work begins at Stage 6, but Stage 6 may be blocked by an unresolved dependency from Stages 1-5. In that case, finish the blocking dependency first and document the reason. Do not bypass scheduler, userspace, storage, driver or security invariants just to advance the stage number.

When a stage is completed, record:
1. what changed;
2. what was tested;
3. what was measured;
4. what remains unsupported;
5. which document sections were updated;
6. the commit SHA.



---

# SOURCE 10: ZEROOS_10_STAGE_HARDENING.md

# ZEROOS 10-Stage Hardening Gate

This document is the binding acceptance checklist for Stages 1-10.
A checkbox is evidence-backed only when the implementation, executable
test, stress/fault/recovery evidence, measurement, and CI/hardware scope
are recorded. Documentation or host-only mocks do not close a runtime gate.

## Universal gate

For every item:
- AUDIT
- DESIGN
- IMPLEMENT
- TEST
- STRESS
- MEASURE
- DOCUMENT
- INTEGRATE

Required evidence fields:
- commit SHA
- exact command/test
- execution class: host / QEMU / real hardware
- hardware/configuration where relevant
- expected result
- observed result
- failure/recovery result
- resource measurements where relevant

Never mark support from detection, parsing, a host contract, or an architecture document alone.

## Stage 1 — Kernel / scheduler / SMP

- [ ] deterministic scheduler trace is emitted and checked in CI
- [ ] panic context diagnostics are fault-injected and checked in CI
- [ ] context ownership survives repeated yield/preemption
- [ ] sleep/wait/timeout/exit races are stress-tested
- [ ] SMP ownership, hot-offline, TLB and cross-CPU wake paths pass
- [ ] register preservation and interrupt-frame lifetime are stress-tested
- [ ] long-duration scheduler soak is recorded
- [ ] QEMU multi-vCPU evidence is green

## Stage 2 — Userspace / IPC

- [ ] process/thread lifecycle is guest-tested
- [ ] Ring-3 and syscall boundary are negative-tested
- [ ] safe user-copy rejects malformed addresses
- [ ] IPC capability generation/revocation is stress-tested
- [ ] pipe partial-write exact-count/order contract is tested
- [ ] full nonblocking pipe returns EAGAIN
- [ ] finite timeout returns ETIMEDOUT
- [ ] reader consumption wakes blocked writers
- [ ] close wakes blocked peers with correct errno
- [ ] concurrent producer/consumer integrity is verified
- [ ] resource exhaustion and recovery are guest-tested

## Stage 3 — Storage

- [ ] block timeout/fault paths pass
- [ ] request merging and HDD scheduling are bounded
- [ ] VFS/filesystem/page-cache ownership is stress-tested
- [ ] crash-consistency/power-cut replay passes
- [ ] fsck/recovery passes
- [ ] descriptor/mapping cleanup after failure passes
- [ ] persistent storage survives repeated reboot cycles

## Stage 4 — Drivers / network / power / thermal

- [ ] PCI/ACPI/DMA/MMIO/IRQ ownership is validated
- [ ] driver failure is isolated from unrelated services
- [ ] malformed device input is rejected safely
- [ ] Ethernet/IP/TCP/DNS/DHCP paths are guest-tested
- [ ] Wi-Fi support is claimed only for tested adapters
- [ ] audio path is measured before support is claimed
- [ ] thermal/power state transitions are fault-tested
- [ ] device removal/recovery paths are tested

## Stage 5 — Graphics / desktop

- [ ] compositor damage/occlusion/frame pacing are guest-tested
- [ ] hidden/covered/fullscreen surfaces stop unnecessary rendering
- [ ] display/input degradation paths recover
- [ ] desktop services survive crash/restart
- [ ] resource pressure reduces work without deadlock
- [ ] accessibility and localization paths are exercised
- [ ] real GPU acceleration is separately certified from framebuffer fallback

## Stage 6 — Security / packages / updates / recovery

- [ ] capability and permission checks are enforced at the real boundary
- [ ] sandbox violations fail closed
- [ ] NX/W^X/ASLR/stack protections are enforced where supported
- [ ] malformed/privilege-escalation probes are rejected
- [ ] firewall policy is bound to the actual packet path
- [ ] package signatures reject wrong-key/tampered/replayed payloads
- [ ] update activation is transactional
- [ ] interrupted/failed update deterministically rolls back
- [ ] snapshots and rollback are recovery-tested
- [ ] recovery boots independently of the normal desktop
- [ ] security fuzz/fault-injection suite is green

## Stage 7 — GPU / media / browser / native apps

- [ ] accelerated backend has a real driver path
- [ ] fallback path remains functional
- [ ] GPU resource ownership is bounded
- [ ] hardware video decode is measured on supported hardware
- [ ] 1080p playback has sustained-load evidence where hardware permits
- [ ] browser engine is isolated from kernel and desktop services
- [ ] browser crash/reload recovery is guest-tested
- [ ] native app lifecycle/permission/storage contracts are enforced
- [ ] background work is cancellable and suspendable

## Stage 8 — Windows / Android / gaming

- [ ] PE/COFF loading is validated against malformed inputs
- [ ] Win32/Win64 compatibility APIs are implemented only where tested
- [ ] DLL/import/thread/filesystem/registry semantics have executable evidence
- [ ] graphics translation path is tested on a real supported GPU
- [ ] Android runtime is isolated and dormant when unused
- [ ] APK lifecycle/permissions/storage are enforced
- [ ] Android compatibility matrix contains real tested workloads before claims
- [ ] gaming profiles are measured, not simulated
- [ ] thermal/memory pressure behavior during gaming is measured
- [ ] controller support remains out of scope unless explicitly re-authorized

## Stage 9 — ZERO AI / automation / ecosystem

- [ ] ZERO AI is separate from Forge AI
- [ ] ZERO AI is dormant when not requested
- [ ] every privileged AI action passes through a permission broker
- [ ] file/process/network/settings actions have explicit capabilities
- [ ] action denial is fail-closed and auditable
- [ ] AI service crash does not break core OS operation
- [ ] context/secrets are wiped according to lifecycle policy
- [ ] automation is bounded, cancellable and permission-first
- [ ] offline mode is deterministic
- [ ] AI resource usage is measured under idle/active/load conditions

## Stage 10 — Hardware / soak / release

- [ ] Lenovo G560-class boot certification is executed on physical hardware
- [ ] native 1366x768 path is measured
- [ ] HDD persistence and long-running I/O are measured
- [ ] 1080p media is tested where hardware permits
- [ ] Wi-Fi/link performance is measured on supported adapters
- [ ] thermal behavior is measured with real sensors where available
- [ ] cold/warm reboot and recovery are tested repeatedly
- [ ] long-duration idle and active soak are completed
- [ ] update/rollback/recovery cycle is repeated
- [ ] security regression suite is green
- [ ] no known critical crash remains
- [ ] release evidence records unsupported hardware/features explicitly

## Hard rule

A stage may be marked complete only when all declared exit-gate items have
executable evidence. A green host test cannot close a QEMU or hardware gate,
and QEMU cannot close a real-hardware gate.


---

# SOURCE 11: docs/ZEROOS_AI_HANDOFF.md

# ZEROOS — AI Handoff / Continuation Ledger

This file is the compact source-of-truth handoff for another AI continuing ZEROOS. It is reconstructed from the retained engineering conversation context, not a verbatim transcript. Do not invent missing history; inspect current GitHub state before changing anything.

## Project
- Repo: https://github.com/priyanshagrahari54-blip/zeroos
- Real native x86-64 OS; not Linux skin, mockup, simulator, or toy.
- Goal: complete daily-driver OS, production-grade from the start.
- Separate project: Forge AI (general engineering AI). ZERO AI is only the personal ZEROOS-integrated AI.

## Non-negotiable product requirements
- Low unnecessary CPU/RAM/wakeups/I/O; event-driven and dormant components.
- Windows application compatibility is core.
- Android app compatibility is core.
- Modern AAA gaming/GTA V is core.
- Vulkan/GPU acceleration is core.
- Original UI inspired by Windows 11 + ChromeOS, with useful ideas from macOS but not a clone.
- Browser, Files, Terminal, Settings, media, diagnostics, performance center and native apps are core.
- Controller support is explicitly out of scope.
- Evidence, not declarations, closes stages.

## Resource/performance rules
“0 resource” means near-zero unnecessary overhead, not literal zero CPU/RAM while active.
Use event-driven services, dormant components, foreground priority, bounded background work, write batching, read-ahead, adaptive page cache, optimized journaling, throttled updates/downloads, thermal-aware scheduling, power-aware states, HDD-aware I/O, damage/occlusion tracking, and GPU only when useful.
Historical targets were hypotheses, not guarantees: overall unnecessary overhead about 2–4x lower; EEVDF 1.1–1.3x; reclaim 1.1–1.5x under pressure; HDD scheduler/read-ahead 1.2–2x; Vulkan 1.1–2x; hardware decode 2–10x CPU reduction. Replace all with measured benchmarks.

## Display
- G560 target: 1366x768 native.
- External FHD if supported.
- 2K/4K only where hardware/display warrants it.
- Animated wallpaper: adaptive 15–30 FPS; hidden/covered/fullscreen/game/lock => no continuous rendering; pressure => 60→30→20→15→static→suspended.
- Never claim active animation uses literally zero GPU.
- HD-first/FHD-feel: crisp typography, scalable assets, no unnecessary 4K decode, damage tracking, occlusion, frame pacing, hardware decode where available.

## Network
- 100 Mbps is a target where adapter/router/signal/ISP/driver permit; never guarantee it.
- Ethernet, ARP, IPv4/6, UDP/TCP, DNS/DHCP, sockets, TLS, firewall, diagnostics.
- Full Wi-Fi stack/adapter certification is incomplete.

## Security target
Secure/Measured Boot where supported, user/kernel isolation, NX/W^X, ASLR, capabilities, sandboxing, driver isolation, IOMMU/DMA protection, signed packages/updates, atomic rollback, encrypted storage, firewall, app permissions, exploit mitigations, memory-safe code where suitable, fuzzing/static analysis/sanitizers/fault injection, low-overhead telemetry.
ChaCha20/Poly1305/AEAD has been verified; full enforcement/integration is incomplete.

## ZERO AI
Personal ZEROOS AI, not public generic AI. Must use a permission/agent broker, not unrestricted kernel access.
Capabilities may include files, apps, terminal, coding, settings, diagnostics, study and automation.
Required: dormant by default, explicit capabilities, fail-closed denial, auditability, cancellation, bounded resources, crash isolation, secrets/context lifecycle, deterministic offline behavior where applicable.
Backend/inference is incomplete.
Keep ZERO AI separate from Forge AI.

## Android
Desired model:
- no always-resident Google Play Services;
- browser can access Play Store web;
- secure ZEROOS APK installer for manual APK lifecycle;
- Android runtime starts only for Android workload;
- optional compatibility service dormant by default.
Removing Play Services can break push, location, Sign-In, Maps, billing, Play Integrity, etc.
Browser Play Store access does not itself guarantee supported APK installation.

## Windows
Isolated/resource-controlled userspace runtime.
Need PE/COFF, Win32/Win64 APIs only where tested, DLL/import resolution, process/thread/filesystem/registry semantics, real GPU graphics translation, malformed-input tests, explicit unsupported diagnostics.
Current compat foundation does NOT execute foreign Windows code; INSTALLED != RUNNING. Current header contract has path/registry translation, DLL refcounts, fixed capacities, zero heap, host tests. PE loader/import/syscall translation remain future work.
DXVK/VKD3D-style Vulkan translation is a design direction, not completed.

## Gaming
GTA V / modern AAA, Vulkan, real GPU, thermal/memory pressure, fullscreen/background suppression, gaming profiles, real workload matrix. Not production implemented yet. Controller support out of scope.

## Architecture direction
Custom x86-64 kernel; EEVDF-style scheduling; tickless; demand paging/MGLRU-style reclaim; HDD-aware scheduler; journaling FS; Vulkan; damage tracking; hardware video decode; NAPI/GRO/GSO-style networking; browser process isolation; PE/COFF + Vulkan Windows path; Android Binder/ART-style architecture; Secure Boot/TPM/sandbox/ASLR/NX/CFI; LLVM/Clang; perf/eBPF-style observability; thermal governor; adaptive profiles.
Implementation dependency order: Kernel → Memory → Scheduler → HDD/Filesystem → Drivers → Graphics → Network → Security → UI → Media → Browser → Windows → Android → ZERO AI.

## Universal stage gate
AUDIT → DESIGN → IMPLEMENT → TEST → STRESS → MEASURE → DOCUMENT → INTEGRATE.
Completion requires implementation, documented contracts, ownership/lifetime/concurrency review, security boundary review, resource measurement, failure/recovery tests, stress/fault tests, required QEMU/hardware validation, green CI, matching docs.
Evidence records should include commit SHA, exact test/command, execution class (host/QEMU/hardware), hardware/config, expected/observed result, failure/recovery result, measurements.
Host != QEMU != real hardware.

## Ten stages
### 0 — reproducible engineering/CI
Mostly complete.

### 1 — kernel/scheduler/context/SMP
Need scheduler/context lifecycle, wait/sleep/preemption, runqueue ownership, interrupt-frame ownership, register preservation, SMP, hot-offline, deterministic trace, panic diagnostics, multi-vCPU and soak.
Substantial implementation; final evidence gate was pending at handoff.

### 2 — process/thread/ring3/syscall/IPC/userspace
Need lifecycle/PID/TID/parent-child/wait/exit/exec/fd/credentials/syscall ABI/user copies/page faults/signals/events/IPC/resource limits/ring3 and pipe semantics.
Broad evidence exists. Pipe certification is the remaining important gate.

### 3 — VFS/filesystem/block/storage
Need block queues, HDD scheduler, merge/priority/aging/bounded queues, VFS, journaling, page cache, buffered/direct/async I/O, fsck/recovery, snapshots, checksums/encryption, crash/power-cut replay and persistence.
Substantial implementation; physical crash/recovery evidence still required.

### 4 — drivers/network/audio/power/thermal
Need device/bus model, PCI, ACPI, DMA, IRQ, USB/HID, storage, network, graphics, audio, power/thermal, Wi-Fi tested adapters, malformed device input handling.
Foundations substantial; hardware certification incomplete.

### 5 — graphics/desktop
Need framebuffer/display/input/GPU abstraction, compositor, surfaces, WM, UI toolkit/fonts, taskbar/launcher/search/control center/notifications/workspaces/settings/performance/files/terminal/screenshot/accessibility; damage/occlusion/frame pacing/background suppression.
Compositor/window/input foundations are host-tested; real GPU and desktop chrome incomplete.

### 6 — security/packages/updates/recovery
Need actual privilege/capability/permission/sandbox enforcement, NX/W^X/ASLR/stack protection, signed packages/updates, Secure Boot/TPM, encrypted storage/secrets/audit, transactional updates/rollback, recovery boot, fsck/repair/safe mode/driver isolation/recovery terminal/network recovery, fuzz/fault injection.
Not production complete.

### 7 — GPU/media/browser/native apps
Need real GPU/Vulkan driver/runtime, fallback, bounded GPU, hardware decode, 1080p where hardware permits, browser engine/isolation/crash recovery, and native apps: Files, Terminal, Browser, Notes, PDF, Calculator, Dictionary, Study Center, Code Editor, Media Player, Settings, Diagnostics, Performance Center.
Not production complete.

### 8 — Windows/Android/gaming
Need PE/Win32/Win64, DLL/import/process/thread/filesystem/registry semantics, real graphics translation, Android isolated dormant runtime, APK lifecycle/permissions, real workload matrix, GTA V/AAA tests, thermal/memory pressure and gaming profiles.
Not production complete.

### 9 — ZERO AI/automation/ecosystem/performance
Need ZERO AI backend, capability broker, files/process/network/settings operations, fail-closed/audit, crash isolation, context/secrets lifecycle, cancellation, bounded automation, offline deterministic behavior, performance measurements.
Architecture/contracts partly exist; backend incomplete.

### 10 — physical release certification
G560-class boot, 1366x768, HDD persistence, 1080p media where possible, Wi-Fi measurements, thermal sensors, cold/warm reboot/recovery, long soak, rollback, security regression, no critical crashes, explicit unsupported list.
BLOCKED until 1–9 evidence and hardware certification.

## Current repo audit retained
- Boot: Multiboot2 → long mode → paging → kernel entry.
- Physical allocator: bitmap + tests.
- VMM: 4-level map/unmap/permissions/MMIO, huge-page splitting.
- GDT/TSS, IDT/ISR, PIC/PIT, timer/IRQ.
- Scheduler: task creation, cooperative context switch, idle, timer preemption framework, wait queues, timed sleep, zombie reaping, preempt_count, stack guards, diagnostics.
- SMP: AP startup, GS/per-CPU, TLB shootdown plumbing; not fully certified.
- FPU/SSE/AVX context switching not implemented.
- Userspace: process/thread/ring3/ELF/syscall/IPC/shared memory foundations.
- Storage: AHCI/NVMe/block/page cache/VFS/ZJFS; HDD C-SCAN, merging, priority/aging, bounded queues; legacy IDE detected but not driven; AHCI legacy INTx partial/polling fallback.
- Network: Ethernet/ARP/IP/UDP/TCP/DNS/DHCP/socket pieces; full Wi-Fi not established.
- USB/audio cores.
- Display: framebuffer discovery/MMIO; current boot path has 1024x768x32 target while product target is 1366x768.
- GPU/Vulkan real driver/runtime not done.
- Compositor: retained scene/damage/occlusion/pacing/cache host-tested.
- Window/input core exists; desktop chrome pending.
- Browser lifecycle exists; modern browser engine absent.
- Files/Terminal/Settings/Notifications/Accessibility/i18n/Performance Center foundations exist.
- Security crypto verified; enforcement incomplete.
- Windows compat foundation only; no production PE/Win32/DirectX runtime.
- Android runtime absent.
- ZERO AI broker/dormancy contracts host-tested; backend absent.
- Package/update/rollback/recovery foundations exist.
- Thermal/power hardware incomplete.
- G560 certification, 1080p media, gaming, 100 Mbps Wi-Fi certification not done.

## Important historical scheduler failure
A previous scheduler bug produced invalid opcode at RIP 0x1bf and impossible task id 0x21.
Hardening added stale interrupt-frame invalidation, tagged IRQ exit, consumed-frame retirement, frame bounds validation, saved-stack validation, current_task/state validation and scheduler diagnostics.
Task context contract:
- RUNNING owns CPU and no resumable interrupt frame.
- RUNNABLE/BLOCKED has exactly one live context: interrupt frame OR saved_stack.
- ZOMBIE has no resumable context and is reclaimed later.

## Recent commits
- e68f1bbf28b64aa41113f53addd6bcbb7a6b120c — deterministic scheduler trace accumulator; emits ZEROOS scheduler deterministic trace: 01-02-04-08-10-20-40.
- 20a444824ce24da57bd83a3988efd27edcd4efb7 — adds task_panic_diagnostics_self_test declaration.
- daabb211cb1a841e1d3ea2673724497ce2c0bb52 — adds fault-build panic diagnostics self-test.
- cb18e336b713b7bb9478963cbb3e976e1da10eff — invokes panic self-test after task_system_init() and before scheduler_init() in fault builds.
- 6574dba4e443f7dec642ecd05518c0c96a72eb21 — CI checks deterministic trace and adds scheduler panic-diagnostics fault certification; expected markers include ZEROOS PANIC: scheduler panic diagnostics self-test and task-table invariant dump.
- 6f107d2d7f76e82c977d84623afa32492aee7e0f — adds binding 10-stage hardening checklist.
- 96d60dc049c893eda9731e08b5c86142e53c06b8 — rewrites VALIDATION.md as evidence policy/support ledger.

## CI evidence
Previous successful main workflow:
- run 36586162814
- SHA 9e0ad4c9ee3c7d5e4cc25a5844f5005f5b89c698.
Evidence included scheduler/wait/sleep/preemption/fairness/zombie/frame ownership/per-CPU/offline gates, userspace/process/ring3/syscall/IPC/recovery gates, storage gates, 4-vCPU and NX/fault paths.

New run that MUST be checked first:
- run 36731939636
- SHA 6574dba4e443f7dec642ecd05518c0c96a72eb21
- event push.
At handoff it was in progress. Do not close Stage 1 until this run is green and the dedicated markers are present.

## Issue #10 retained checklist
Stage 1:
- [x] reproduce latest HEAD/CI/QEMU
- [x] 100+ timer ticks
- [x] worker/wait/sleep/idle
- [x] current_task ownership
- [x] interrupt_frame/saved_stack lifecycle
- [x] task transitions/runqueue
- [x] RBX/RBP/R12-R15
- [x] repeated yield
- [x] timer preemption
- [x] mixed yield/preemption
- [x] sleep/wake/timeout races
- [x] exit/reaping
- [x] idle
- [x] SMP ownership/offline
- [ ] deterministic scheduler trace
- [ ] panic diagnostics
- [x] QEMU/CI evidence

Stage 2:
- [ ] finish PR #6 pipe semantics/review
- [x] process/thread lifecycle
- [x] ring3 isolation
- [x] syscall ABI/user copies
- [x] address space/page fault
- [x] IPC/wait/exit
- [x] credentials/permissions
- [x] resource limits
- [x] userspace stress/recovery

## PR state
PR #6:
- bounded byte-stream pipe semantics
- head SHA 4ee314a21f26a22498321ac8dc8d2d65c0ca1775
- merge-conflict state; do not force-merge.
Required evidence: partial-capacity exact count/order; full nonblocking EAGAIN; finite timeout ETIMEDOUT; writer wake after reader consumption; close wakeups; concurrent producer/consumer integrity.

PR #11:
- dedicated pipe certification: partial-write count/order, PEEK non-consumption, EAGAIN, ETIMEDOUT, EPIPE, cleanup, ipc_debug_validate().
- moved to Ready-for-review.
- must have green CI and clean merge state before integration.

PR #8: Stage 5, merge conflict.
PR #9: SMP/storage stress, merge conflict.
PR #7: merged SMP boot fixes + Stage 3/4/5 work.

## Next actions — execute, do not repeatedly ask
1. Poll run 36731939636.
2. If failed, inspect logs, fix root cause, push targeted fix, wait for CI.
3. If green, update Issue #10 and tick deterministic trace + panic diagnostics only with exact evidence.
4. Check PR #11 CI/mergeability; integrate only if green/conflict-free.
5. Re-run Stage 2 certification; resolve PR #6 only with executable evidence.
6. Stage 3: crash consistency, replay/fsck, persistence, power-cut/fault injection.
7. Stage 4: malformed device input, DMA/IRQ fault paths, Wi-Fi adapters, power/thermal.
8. Stage 5: real GPU/fallback, 1366x768, frame pacing, background suppression.
9. Stage 6: real security enforcement, package signatures, transactional updates/rollback, recovery.
10. Stage 7: Vulkan/GPU, media decode, browser.
11. Stage 8: Windows, Android, APK lifecycle, gaming.
12. Stage 9: ZERO AI backend/broker.
13. Stage 10: G560 hardware, soak, security/performance regression, release.

## Hard rules
- Never mark complete because a mock/API/struct exists.
- Never tick without executable evidence.
- Host != QEMU != hardware.
- Never fabricate benchmarks or hardware results.
- Never silently merge conflicted PRs.
- Prefer small testable commits.
- Every capability needs a validation gate.
- Preserve implemented/host-tested/QEMU-tested/hardware-tested/production-certified distinctions.
- Do not force-push or rewrite history unnecessarily.
- Keep Windows/Android runtimes dormant when unused.
- Keep ZERO AI separate from Forge AI.
- Controller support remains out of scope.
- Stage 10 remains blocked until upstream evidence exists.

## Current truth
Kernel/boot/memory/userspace/storage/network/compositor foundations are substantial. Scheduler certification was still being closed. GPU/Vulkan, browser engine, Windows runtime, Android runtime, gaming, full security enforcement, ZERO AI backend, Wi-Fi certification and G560 physical certification are not complete.

## For the next AI
Treat GitHub as the source of truth. Inspect current branch, workflows, issues, PRs, docs, source and tests before acting. If this document conflicts with current repository evidence, current repository code/CI wins and this document should be updated.


---

# SOURCE 12: docs/ZEROOS_FULL_REQUIREMENTS_225_PLUS_QA.md

# ZEROOS — FULL REQUIREMENTS / NEED / SPEED / Q&A MASTER
Version: 1.0
Status: Binding product + engineering requirements
Purpose: Preserve the complete ZEROOS context for a new AI/engineer without relying on chat history.

## 0. RULES OF TRUTH

1. ZEROOS is a real native x86-64 operating system, not a mockup, simulator, Linux skin, or demo.
2. A requirement is not complete because documentation says so. Completion requires executable evidence.
3. Host tests, QEMU tests, and physical-hardware tests are separate evidence classes.
4. Detection, stubs, contracts, mocks, or architecture-only code do not prove operational support.
5. Every stage follows: AUDIT -> DESIGN -> IMPLEMENT -> TEST -> STRESS -> MEASURE -> DOCUMENT -> INTEGRATE.
6. Never fake a checkbox. A checkbox is ticked only after evidence exists.
7. Advanced-first engineering is required; avoid throwaway implementations that must later be replaced.
8. Security, performance, recovery, and resource use are release requirements, not optional polish.
9. ZERO AI is personal ZEROOS AI and deeply integrated with ZEROOS. Forge AI is a separate general-purpose AI project.
10. Controller support is explicitly out of scope for ZEROOS requirements.

## 1. PRODUCT IDENTITY AND CORE GOALS

Q1. What is ZEROOS?
A. A real native x86-64 general-purpose operating system intended to become a daily-driver desktop OS.

Q2. Is ZEROOS a Linux distribution?
A. No. The target architecture is a custom native kernel and OS stack.

Q3. Is ZEROOS a simulator?
A. No. QEMU is a validation environment; the product itself is a real OS.

Q4. What is the primary product goal?
A. A stable, secure, fast, low-overhead daily-driver OS with native services, original UI, modern graphics, compatibility runtimes, recovery, and integrated ZERO AI.

Q5. What does “low resource” mean?
A. Minimize unnecessary CPU wakeups, RAM retention, disk I/O, network activity, GPU work, background services, indexing, logging, and repeated computation.

Q6. Can ZEROOS literally use zero CPU/RAM?
A. No. Active software requires resources. The engineering target is near-zero unnecessary idle overhead and event-driven activation.

Q7. What is the performance philosophy?
A. Minimum unnecessary work, bounded thermal load, minimum unnecessary disk/network I/O, measurable latency, predictable degradation, and aggressive dormancy for unused subsystems.

Q8. What must be measured?
A. Boot time, idle CPU/RAM, process launch, context switch, syscall, allocation, page fault, filesystem, disk, network, UI frame latency, app launch, suspend/resume, thermal behavior, and long-duration stability.

Q9. What is the release philosophy?
A. No feature is considered production-ready merely because it compiles. It must survive functional, stress, fault, recovery, security, performance, and integration validation.

Q10. What hardware is the initial certification target?
A. Lenovo G560-class physical hardware is the explicit Stage 10 certification target, while broader hardware support is capability-profile based.

## 2. RESOURCE / SPEED REQUIREMENTS

Q11. What is the idle CPU target?
A. Near-zero unnecessary CPU usage; event-driven services should sleep when no work exists. Exact percentage must be established by physical benchmarks rather than invented.

Q12. What is the idle RAM target?
A. Keep the base system lean, reclaimable, and proportional to enabled functionality. Do not promise a fixed number without hardware/build measurements.

Q13. What is the disk I/O target?
A. Avoid periodic polling and unnecessary writes. Batch/coalesce writes, use intelligent read-ahead, bounded queues, cache intelligently, and suppress background work under contention.

Q14. What is the HDD requirement?
A. HDDs are first-class supported storage devices. The I/O scheduler must account for seek cost, sequential locality, merging, aging, bounded queues, and starvation prevention.

Q15. What is the SSD/NVMe requirement?
A. Use the same block abstraction but allow device-specific scheduling and queue-depth behavior. Avoid HDD assumptions on SSD/NVMe.

Q16. What is the scheduler requirement?
A. Fair, preemptive, low-latency scheduling with tickless operation where possible, wakeup correctness, per-CPU behavior, affinity, load balancing, and deterministic diagnostics.

Q17. Which scheduling concepts are required?
A. EEVDF-style fairness/eligibility, priority handling, real-time/deadline hooks where needed, per-CPU runqueues, affinity, load balancing, tickless operation, and latency tracing.

Q18. What is the memory-management requirement?
A. Demand paging, reclaim, page cache, COW, VMAs, guard pages, ASLR, PCID/TLB optimization where supported, page-table lifecycle correctness, and fragmentation-aware allocation.

Q19. What is the reclaim requirement?
A. Reclaim inactive/unneeded pages while protecting active working sets; MGLRU-style policy is a candidate/target and must be benchmarked.

Q20. What is the cache requirement?
A. Adaptive page cache, bounded caches, intelligent read-ahead, write coalescing, and pressure-aware reclamation.

Q21. What is the background-work requirement?
A. Indexing, backups, updates, snapshots, downloads, scans, and maintenance must be throttled by CPU, disk, network, battery, and thermal state.

Q22. What is thermal-aware behavior?
A. Scheduler and service policy must reduce background work, animation, decode complexity, and optional workloads under thermal pressure while protecting correctness.

Q23. What happens when hardware is weak?
A. Features degrade predictably: lower animation FPS, static wallpaper, reduced background activity, smaller caches, reduced decode/graphics workload, and deferred maintenance.

Q24. What is the Wi-Fi speed requirement?
A. The target is 100 Mbps where the adapter, driver, AP/router, signal, channel, and ISP can support it. The OS cannot guarantee an external network rate.

Q25. What is the overall overhead target?
A. Reduce unnecessary OS overhead substantially relative to naive periodic/background designs. Earlier engineering estimates of roughly 2–4x lower unnecessary overhead are targets, not guarantees.

## 3. DISPLAY / WALLPAPER / UI

Q26. What is the default display target?
A. Native 1366x768 should be the default first-class profile for the G560-class target.

Q27. Should 4K be the default desktop asset resolution?
A. No. Use resolution-aware/vector assets and avoid unnecessary 4K decode/rendering.

Q28. What is the animated wallpaper requirement?
A. Adaptive 15–30 FPS is acceptable when active; reduce FPS or freeze to static under pressure.

Q29. Can active 4K animation use literally zero GPU?
A. No. Active animation requires rendering. The requirement is near-zero unnecessary work when hidden, covered, fullscreen, locked, or otherwise not visible.

Q30. When should wallpaper rendering stop?
A. When covered, hidden, fullscreen/game mode, lock screen, or otherwise not visible. Static wallpaper should have no continuous animation loop.

Q31. What should happen on weak GPUs?
A. Use static wallpaper or a very low-cost animation profile; do not allow decorative rendering to harm interactive performance.

Q32. What compositor features are required?
A. Damage tracking, occlusion culling, retained scene representation, frame pacing, surface caching, bounded work, and suppression of invisible surfaces.

Q33. What UI style is required?
A. Original ZEROOS UI inspired by useful Windows 11 and ChromeOS concepts, with useful functional ideas from macOS, but not a macOS clone.

Q34. What must the desktop include?
A. Window manager, taskbar/launcher, search, control center, notifications, workspaces, settings, file manager, terminal, accessibility, screenshot, performance tools, and coherent typography/input.

Q35. What is “HD-first, FHD-feel”?
A. Prioritize native 1366x768 efficiency while maintaining crisp typography, vectors/high-quality scaling, good spacing, and resolution-aware assets.

## 4. KERNEL / BOOT / SCHEDULER

Q36. What boot path exists?
A. Multiboot2 bootstrap, x86-64 long mode, bootstrap paging, kernel entry, diagnostics, and QEMU boot validation.

Q37. What physical memory mechanism exists?
A. Physical page bitmap allocation with validation/tests.

Q38. What virtual-memory mechanism exists?
A. Four-level x86-64 page tables with map/unmap, permissions, MMIO mapping, and huge-page splitting support.

Q39. What interrupt architecture is required?
A. GDT/TSS, IDT/ISR paths, IRQ routing, timer interrupts, safe interrupt-frame ownership, and validation of saved execution context.

Q40. What is the current scheduler priority?
A. Scheduler/context lifecycle stability and certification before unrelated major kernel work.

Q41. What scheduler failure occurred historically?
A. Invalid opcode at RIP 0x1bf and an impossible task id 0x21 after scheduler activity; this drove context/frame ownership hardening.

Q42. What scheduler hardening is required?
A. Stale interrupt-frame invalidation, tagged IRQ exit, consumed-frame retirement, frame bounds checks, saved-stack validation, current_task validation, state/runqueue validation, and panic diagnostics.

Q43. What scheduler stress cases are mandatory?
A. Repeated yield, timer preemption, mixed yield/preemption, sleep/wake/timeout races, wait races, exit during scheduling, idle transitions, zombie reaping, SMP ownership/offline paths, and register preservation.

Q44. Which registers must be stress-tested?
A. Callee-saved RBX, RBP, R12, R13, R14, R15 plus the complete context contract.

Q45. What is deterministic scheduler tracing?
A. A runtime marker must prove the ordered certification checkpoints occurred. Current implementation accumulates seven markers and emits the deterministic trace marker once all are observed; this is a certification marker, not a full event log.

Q46. What are panic diagnostics?
A. A dedicated fault build invokes the existing task-context panic path and verifies the panic message plus task-table invariant dump. This proves the diagnostic path executes.

Q47. What is SMP status?
A. AP discovery/startup, GS/per-CPU plumbing, TLB-shootdown plumbing, and ownership paths exist, but physical certification and full production maturity require evidence.

Q48. Is FPU/SSE/AVX context switching complete?
A. It was not complete in the audited state and must be explicitly implemented/certified before claiming full production support.

Q49. What is the scheduler release gate?
A. No Stage 1 completion without executable evidence for scheduler lifecycle, races, context integrity, deterministic trace, panic diagnostics, SMP, soak, and QEMU/CI certification.

Q50. What is the universal kernel quality rule?
A. Every ownership, lifetime, concurrency, interrupt, and context contract must be explicit and stress-tested.

## 5. PROCESSES / USERSPACE / IPC

Q51. What is required for userspace?
A. Process/thread model, PID/TID, parent/child lifecycle, wait/exit, exec, credentials, FD tables, syscall ABI, user stack, safe copies, page faults, signals/events, IPC, and resource limits.

Q52. Is ring-3 isolation required?
A. Yes. User processes must not execute with kernel privilege.

Q53. What syscall requirements exist?
A. Stable ABI, validated arguments, safe user-memory copying, fault handling, bounded execution, and clear error semantics.

Q54. What is required for page faults?
A. Correct classification, safe fault handling, demand paging/COW integration, invalid-access termination, and diagnostic evidence.

Q55. What IPC mechanisms are required?
A. Pipes/byte streams, wait queues, process lifecycle synchronization, and future extensible IPC primitives.

Q56. What exact pipe partial-write contract is required?
A. If capacity is partially available, a writer must return the exact number of bytes accepted and preserve byte ordering; it must never claim bytes that were not stored.

Q57. What happens when a nonblocking pipe is full?
A. Return EAGAIN rather than block.

Q58. What happens when a finite-timeout pipe write remains blocked?
A. Return ETIMEDOUT after the specified timeout if no capacity becomes available.

Q59. What happens when a reader consumes data?
A. A blocked writer must be woken correctly when capacity becomes available.

Q60. What happens on close?
A. Closing endpoints must wake blocked readers/writers and return the correct EOF/EPIPE-style semantics according to endpoint state.

Q61. What concurrency guarantee is required for pipes?
A. Concurrent producer/consumer stress must preserve byte_count/head/tail integrity and ordering without corruption.

Q62. What is the PR #6 status requirement?
A. The pipe hardening change must remain separate until its exact behavioral contracts have executable evidence; broad IPC tests alone are insufficient.

Q63. What are resource limits for?
A. Prevent unbounded process/thread/fd/memory/IPC growth and provide deterministic failure/recovery paths.

Q64. What is the userspace stress requirement?
A. Repeated process creation/exit, IPC, wait, fault, memory pressure, resource exhaustion, and recovery without kernel corruption.

## 6. STORAGE / FILESYSTEM

Q65. What storage stack is required?
A. Block layer, request queues, device drivers, merging, scheduling, VFS, filesystem, journaling, page cache, buffered/direct/async I/O, recovery, snapshots, checksums, and encryption.

Q66. What is ZJFS?
A. The planned/implemented ZEROOS journaling filesystem direction with crash-consistency and recovery requirements.

Q67. What must journaling prove?
A. Metadata consistency, replay correctness, atomic transaction behavior, corruption handling, and recovery after simulated crash/power-cut conditions.

Q68. What must fsck/recovery prove?
A. It must detect and repair defined filesystem inconsistencies safely without silently inventing data.

Q69. What is the disk scheduler design?
A. Merge adjacent requests, account for seek cost on HDD, use bounded queues, priority/aging, sequential locality, and prevent starvation.

Q70. What are approximate storage performance targets?
A. HDD scheduling/read-ahead improvements can be benchmarked in workload-specific ranges such as roughly 1.2–2x in favorable sequential/locality cases; these are targets, not guarantees.

Q71. What is snapshot behavior?
A. Snapshots must be consistent, bounded in overhead, recoverable, and throttled under resource pressure.

Q72. What is update storage behavior?
A. Updates should use transactional writes, atomic activation, rollback metadata, and bounded I/O.

Q73. What must be tested under power loss?
A. Interrupted metadata writes, journal replay, partial file operations, snapshot consistency, package/update transactions, and recovery boot.

## 7. DRIVERS / PCI / ACPI / DMA

Q74. What driver architecture is required?
A. Device/bus objects, registry/matching, PCI, ACPI, DMA, IRQ routing, device lifecycle, resource ownership, and isolation.

Q75. What hardware buses are required?
A. PCI/PCIe, USB, storage buses, display/GPU paths, network devices, audio, and platform ACPI interfaces as applicable.

Q76. What is DMA protection?
A. IOMMU/DMA isolation where hardware supports it, plus strict buffer ownership and mapping/unmapping rules.

Q77. What must malformed device input do?
A. Fail safely, validate lengths/ranges/state, avoid kernel corruption, and produce bounded diagnostics.

Q78. What is the driver isolation requirement?
A. Faulty/unsupported device paths should be isolated as much as architecture permits so a driver failure does not unnecessarily crash the whole system.

Q79. What is the legacy IDE status?
A. Detection existed in the audit, but a complete driven production path was not established.

Q80. What is AHCI status?
A. Substantial support existed, with legacy INTx/polling fallback limitations requiring explicit certification.

## 8. NETWORK / WIFI / AUDIO / POWER

Q81. What network stack is required?
A. Ethernet, ARP/ND, IPv4/IPv6, UDP, TCP, DNS, DHCP, firewall, sockets, TLS integration, diagnostics, and Wi-Fi support for tested adapters.

Q82. Is full Wi-Fi support already proven?
A. No. Wi-Fi requires real adapter/driver/hardware certification; architecture alone is not enough.

Q83. What Wi-Fi performance should be measured?
A. Throughput, latency, packet loss, reconnect time, roaming behavior where supported, power impact, and sustained thermal/network stability.

Q84. What is the 100 Mbps target?
A. 100 Mbps is a practical target for capable tested Wi-Fi hardware and network conditions, not an OS-only guarantee.

Q85. What audio requirements exist?
A. Audio device abstraction, buffers, low-latency playback/recording, device lifecycle, volume/routing, and graceful unsupported-device behavior.

Q86. What power-management requirements exist?
A. ACPI integration, CPU/device power states, idle states, suspend/resume where supported, battery behavior, and wake-source correctness.

Q87. What thermal requirements exist?
A. Sensor discovery, thermal zones, throttling policy, background-work suppression, safe graphics/media behavior, and recovery after temperature falls.

## 9. GRAPHICS / GPU / MEDIA

Q88. What graphics stack is required?
A. Display/framebuffer abstraction, GPU abstraction, compositor, surfaces, window system, rendering API, and fallback paths.

Q89. Is Vulkan required?
A. Yes, for modern GPU acceleration where supported, with fallback for unsupported hardware.

Q90. What does Vulkan provide here?
A. Modern GPU command submission and rendering capability. It does not magically guarantee performance; drivers and hardware must be certified.

Q91. What approximate graphics benefit was previously targeted?
A. Roughly 1.1–2x in suitable rendering workloads versus less efficient paths was used as an engineering target, not a guarantee.

Q92. What media requirement exists?
A. Hardware-accelerated video decode where supported, with CPU fallback, bounded memory, and thermal-aware behavior.

Q93. What is the 1080p requirement?
A. 1080p playback should be supported on hardware/runtime combinations capable of sustained decode; exact support must be measured.

Q94. What should happen under GPU/thermal pressure?
A. Lower visual workload, reduce animation, prefer hardware decode, reduce background rendering, and preserve interactive responsiveness.

Q95. What must the compositor do with fullscreen apps/games?
A. Suppress unnecessary desktop rendering and wallpaper work, use efficient presentation paths, and avoid rendering occluded surfaces.

## 10. BROWSER / NATIVE APPS

Q96. What browser is required?
A. A real modern browser engine/runtime, not merely a shell around a webview, is required for full daily-driver ambitions. The audited repository did not yet contain a complete modern engine.

Q97. What browser security is required?
A. Process isolation, sandboxing, site/content isolation where supported by the engine, memory protections, permission controls, crash recovery, and safe update handling.

Q98. What native apps are required?
A. ZERO Files, ZERO Terminal, Browser, Notes, PDF viewer, Calculator, Dictionary, Study Center, Code Editor, Media Player, Settings, Diagnostics, and Performance Center.

Q99. What should native apps do when inactive?
A. Suspend/dormant where practical, release unnecessary resources, stop timers, and avoid polling.

Q100. What is app launch performance?
A. Measure cold launch, warm launch, first-frame time, disk-read volume, CPU time, and memory footprint. Do not invent a universal millisecond promise before hardware benchmarks.

## 11. WINDOWS COMPATIBILITY

Q101. Why is Windows compatibility a core requirement?
A. Modern Windows application compatibility is a core product goal, not an optional demo feature.

Q102. What compatibility model is intended?
A. Isolated Windows user-space/runtime with PE/COFF loading, Win32/Win64 API compatibility, DLL/import handling, filesystem/registry translation, graphics translation, process/thread semantics, and resource controls.

Q103. Does ZEROOS execute foreign Windows code directly in the kernel?
A. No. Compatibility belongs in controlled user-space/runtime layers.

Q104. What must PE parsing validate?
A. Headers, section ranges, alignment, imports, relocations, entry points, signatures/metadata where applicable, and malformed-input boundaries.

Q105. What is required for DLL semantics?
A. Dependency resolution, load/unload/refcount lifecycle, import/export resolution, architecture checks, search-path controls, and failure diagnostics.

Q106. What graphics compatibility is required?
A. Real GPU translation paths such as DirectX-to-Vulkan-style translation where supported, with validated fallbacks.

Q107. What does Windows compatibility NOT mean?
A. It does not mean every Windows application is automatically compatible. Support must be documented by API/workload matrix and tested.

Q108. What gaming requirement exists?
A. Modern PC gaming, including demanding titles such as GTA V-class workloads, is a core target where hardware/runtime support permits.

Q109. What must gaming profiles do?
A. Suppress nonessential background work, prioritize game responsiveness, reduce desktop rendering, manage thermal/memory pressure, and restore normal policy after exit.

Q110. Is controller support required?
A. No. Controller support is explicitly excluded from the ZEROOS requirement set.

## 12. ANDROID

Q111. What is the Android strategy?
A. Android runtime is isolated and dormant until an Android app actually runs.

Q112. Is a native Play Store app required?
A. No. The design avoids a resident native Play Store application.

Q113. Is Google Play Services required as a resident component?
A. No. They should not be resident by default.

Q114. How does the user access Play Store?
A. Through the browser/web where possible. APK installation uses a secure ZEROOS APK/package installer rather than assuming the browser can directly perform unsupported Google distribution flows.

Q115. What is the limitation of removing Google Play Services?
A. Apps depending on push, location, Google Sign-In, Maps, billing, Play Integrity, or Google-specific APIs may not function fully.

Q116. What must the APK installer verify?
A. Package structure, signature/authenticity policy, permissions, architecture, compatibility, install transaction, rollback/removal, and malicious/malformed input.

Q117. What is Android resource behavior?
A. Runtime, ART-like components, Binder-like IPC, and supporting services remain dormant when no Android app is active.

Q118. What must Android isolation prevent?
A. Unbounded access to host files, devices, credentials, network, and privileged OS operations without explicit permission mediation.

## 13. SECURITY

Q119. What is the security target?
A. Modern defense-in-depth: Secure/Measured Boot where supported, kernel/user isolation, NX/W^X, ASLR, CFI/exploit mitigations where practical, capabilities/permissions, sandboxing, signed packages/updates, encrypted storage, firewall, secrets handling, and strong update/recovery.

Q120. What boot security is required?
A. Secure Boot/Measured Boot integration where firmware/hardware supports it, trusted key policy, verified components, and recovery for invalid updates.

Q121. What memory protections are required?
A. NX, W^X, ASLR, stack protection, validated page permissions, safe user/kernel copies, and CFI/hardening where feasible.

Q122. What is fail-closed security?
A. If a permission, signature, sandbox, or capability decision cannot be safely established, deny the privileged operation rather than silently granting it.

Q123. What is package security?
A. Signed metadata/packages, authenticity verification, dependency integrity, atomic installation, rollback, and anti-downgrade policy where appropriate.

Q124. What is update security?
A. Transactional update, verification before activation, rollback on failed boot/health checks, and preservation of a known-good system.

Q125. What is encrypted storage?
A. Disk/data encryption integrated with key/secrets management and recovery procedures, with explicit threat-model documentation.

Q126. What security testing is required?
A. Fuzzing, malformed-input tests, privilege probes, sandbox escape attempts, fault injection, race tests, static analysis, sanitizers where available, and regression suites.

Q127. Should dark-web sources be used?
A. Not as an authoritative requirement source. Safe/legal threat intelligence may be used, but security decisions must rely on verifiable technical evidence.

## 14. PACKAGES / UPDATES / RECOVERY

Q128. What package manager is required?
A. Transactional package installation/removal, dependency handling, signatures, permissions, versioning, rollback, and reproducible metadata.

Q129. What update behavior is required?
A. Download safely, verify, stage atomically, activate transactionally, health-check, and rollback automatically if necessary.

Q130. What recovery environment is required?
A. Boot repair, filesystem check, snapshot restore, update rollback, safe mode, driver isolation, recovery terminal, and network recovery where supported.

Q131. What happens after a bad update?
A. System must preserve or return to a known-good bootable state.

Q132. What is safe mode?
A. Minimal services/drivers needed for diagnosis and recovery, with nonessential components disabled.

## 15. ZERO AI

Q133. What is ZERO AI?
A. The personal AI integrated specifically into ZEROOS, not a generic public AI platform.

Q134. Is ZERO AI the same as Forge AI?
A. No. Forge AI remains a separate general-purpose engineering AI project.

Q135. What is the architecture?
A. ZERO AI uses a permission/agent broker rather than unrestricted kernel access.

Q136. What can ZERO AI do?
A. Work with files, launch/control permitted apps, use terminal capabilities, assist with coding, configure settings, diagnose problems, study, automate workflows, and manage approved system tasks.

Q137. Can ZERO AI access the kernel directly?
A. No unrestricted direct access. Privileged actions go through explicit audited capability interfaces.

Q138. What happens when ZERO AI is unused?
A. AI services/backends should remain dormant and consume minimal resources.

Q139. What must the AI permission model provide?
A. Explicit capabilities, scope, user authorization, revocation, auditability, fail-closed denial, and safe cancellation.

Q140. What must AI automation guarantee?
A. Bounded execution, cancellation, timeouts, resource limits, rollback/recovery where possible, and no silent escalation.

Q141. What about secrets?
A. ZERO AI should not receive unrestricted credential databases. Secrets access must use scoped APIs and explicit authorization.

Q142. What happens if ZERO AI crashes?
A. It must not crash the kernel or corrupt system state. The AI runtime is isolated/restartable.

Q143. What should happen offline?
A. Deterministic local capabilities should continue where supported; network-dependent AI functions should fail clearly rather than pretending success.

## 16. OBSERVABILITY / TESTING

Q144. What observability is required?
A. Structured logs, counters, tracing, panic dumps, scheduler diagnostics, performance counters, resource telemetry, and reproducible test evidence.

Q145. What should diagnostics include?
A. CPU/core, task state, current_task, interrupt frame, saved stack, runqueue membership, memory mappings, device state, and relevant subsystem counters.

Q146. What is the fault-injection requirement?
A. Intentionally trigger controlled failures in scheduler, memory, IPC, storage, drivers, packages, security, AI, and recovery paths and verify bounded behavior.

Q147. What is long-duration soak testing?
A. Run representative workloads for extended periods to expose leaks, fragmentation, race conditions, thermal drift, device instability, and recovery failures.

Q148. What must every benchmark report?
A. Hardware/configuration, build commit, exact command/workload, warm/cold state, duration, sample count, median/tail where relevant, resource use, and failure/recovery outcome.

Q149. What are benchmark classes?
A. Boot, idle, scheduling, process creation, context switch, syscall, allocation, page fault, filesystem, disk sequential/random I/O, network, UI frame latency, media decode, app launch, suspend/resume, thermal, and soak.

Q150. What is the benchmark rule?
A. A performance factor such as 1.2x or 2x is a workload-specific measured result/target, never a universal promise.

## 17. CI / REPOSITORY / DOCUMENTATION

Q151. What CI requirement exists?
A. Reproducible build, QEMU boot, scheduler tests, userspace tests, storage tests, graphics/compat host tests, fault builds, and release gates.

Q152. What is the role of QEMU?
A. Deterministic development/certification environment for boot, kernel, userspace, devices, and stress tests; it does not replace real hardware certification.

Q153. What docs are authoritative?
A. ZEROOS_MASTER_BLUEPRINT.md, ZEROOS_MASTER_ROADMAP.md, ARCHITECTURE.md, ROADMAP.md, HARDWARE.md, BOOT_SPEC.md, VALIDATION.md, ZEROOS_10_STAGE_HARDENING.md, and this full handoff document.

Q154. What is the roadmap rule?
A. Roadmap is planning; it is not proof of implementation.

Q155. What is the evidence rule?
A. Each milestone records commit SHA, exact test/command, execution class, hardware/configuration, expected result, observed result, failure/recovery result, and resource measurements.

Q156. What was Issue #10?
A. A Stage 1–2 certification gate for scheduler/SMP plus userspace completion. It must only be checked from real evidence.

Q157. What was PR #6?
A. A pipe-semantics hardening PR, requiring executable proof for partial writes, EAGAIN, ETIMEDOUT, wakeups, close behavior, and concurrent integrity before merge.

Q158. What was PR #7?
A. It was merged and contained SMP boot fixes plus Stage 3/4/5 work.

Q159. What was the scheduler diagnostic implementation?
A. Deterministic trace marker in kernel scheduler probes, plus a dedicated fault-build panic diagnostic self-test and CI boot assertion.

Q160. What does a green CI run prove?
A. Only the tests it actually executed and passed. It does not prove physical hardware, unsupported devices, or unimplemented runtimes.

## 18. 10-STAGE HARDENING

Q161. What is Stage 0?
A. Reproducible engineering and CI foundations.

Q162. What is Stage 1?
A. Kernel execution, scheduler, context switching, interrupts, SMP, and lifecycle certification.

Q163. What is Stage 2?
A. Processes, threads, ring-3, syscalls, memory faults, IPC, credentials, permissions, resource limits, and userspace recovery.

Q164. What is Stage 3?
A. VFS, filesystem, block storage, journaling, page cache, crash consistency, and persistence.

Q165. What is Stage 4?
A. Driver model, PCI/ACPI/DMA/IRQ, network, Wi-Fi, audio, power, and thermal behavior.

Q166. What is Stage 5?
A. Graphics, compositor, windows, sessions, desktop shell, input, rendering, frame pacing, and suppression of unnecessary rendering.

Q167. What is Stage 6?
A. Security enforcement, packages, signed updates, transactional update/rollback, recovery, sandboxing, and fault injection.

Q168. What is Stage 7?
A. Real GPU acceleration, media, browser, and native applications.

Q169. What is Stage 8?
A. Windows compatibility, Android compatibility, gaming, real workload matrices, and thermal/memory-pressure validation.

Q170. What is Stage 9?
A. ZERO AI, automation, ecosystem integration, and measured performance optimization.

Q171. What is Stage 10?
A. Physical hardware certification, long-duration stability, recovery, update rollback, security regression, performance validation, and release readiness.

Q172. Why can Stage 10 not be ticked early?
A. It depends on real evidence from Stages 1–9 and physical hardware. A downstream certification gate cannot honestly close while upstream functionality is incomplete.

## 19. HARDWARE / G560 / REAL-WORLD REQUIREMENTS

Q173. What physical display target matters?
A. 1366x768 native display on the Lenovo G560-class machine.

Q174. What storage target matters?
A. HDD persistence and crash/recovery behavior must be tested on real hardware.

Q175. What media target matters?
A. 1080p media should be tested on the actual hardware, recording CPU/GPU/thermal behavior and sustained playback.

Q176. What network target matters?
A. Real Wi-Fi adapter throughput, latency, reconnect, stability, and power behavior must be measured.

Q177. What thermal target matters?
A. Sensors must be identified, sustained workloads run, throttling observed, and recovery verified.

Q178. What boot/reboot tests matter?
A. Cold boot, warm reboot, repeated reboot, crash recovery, bad-update recovery, filesystem recovery, and safe-mode/recovery boot.

Q179. What long-duration requirement matters?
A. Soak representative desktop, network, storage, media, browser, and compatibility workloads and inspect leaks, crashes, thermal drift, and device instability.

Q180. What if hardware cannot support a feature?
A. Record it as unsupported for that capability profile. Do not fake support; provide fallback where possible.

## 20. ARCHITECTURE COMPONENT REQUIREMENTS

Q181. What userspace core services are needed?
A. init, service manager/supervisor, IPC, logging, device manager, storage manager, network manager, audio, settings, notifications, package/update services.

Q182. What memory architecture is needed?
A. Object/slab allocation, per-CPU caches, refcounts, higher-order allocation, fragmentation handling, reclaim, page cache, swap if used, higher-half/direct-map decisions, address spaces, VMAs, COW, ASLR, PCID/TLB management.

Q183. What networking internals matter?
A. Net-device API, packet buffers, Ethernet, IPv4/IPv6, ARP/ND, UDP/TCP, DNS/DHCP, firewall, socket API, TLS, diagnostics.

Q184. What graphics internals matter?
A. Framebuffer/display discovery, GPU abstraction, surfaces, compositor, damage/occlusion, window manager, UI toolkit, fonts, launcher, taskbar, search, notifications, workspaces, settings, accessibility.

Q185. What storage internals matter?
A. Block abstraction, request queues, merging, scheduling, VFS, filesystem, journal, page cache, buffered/direct/async I/O, fsck, snapshots, checksums, encryption.

Q186. What security internals matter?
A. Privilege separation, capabilities, permissions, sandboxing, NX, W^X, ASLR, stack protection, package/update signing, secure boot, TPM integration where available, encrypted storage, secrets service, audit.

## 21. FUNCTIONAL COMPLETENESS

Q187. What does “daily driver” require beyond boot?
A. Stable desktop, storage, networking, browser, media, apps, updates, recovery, security, compatibility, and predictable hardware support.

Q188. What does “real gaming” require?
A. Real GPU driver/runtime, graphics translation, filesystem/process correctness, memory management, audio/input as applicable, shader/runtime support, performance profile, and sustained thermal stability.

Q189. What does “modern Windows apps” require?
A. More than PE parsing: loader, ABI/API coverage, DLLs, threads, synchronization, filesystem, registry semantics, graphics, networking, system libraries, packaging, and tested application matrix.

Q190. What does “Android support” require?
A. Runtime/ABI, application lifecycle, Binder-like IPC, filesystem integration, permissions, graphics, audio, network, APK installation, runtime isolation, and workload testing.

Q191. What does “secure by default” require?
A. Least privilege, deny-by-default capabilities, signed updates, sandboxing, memory protections, firewall, encrypted data where configured, and recovery from failed trust/update states.

Q192. What does “fast” require?
A. Low latency and low unnecessary work, not just high peak benchmark scores.

Q193. What does “low resource” require?
A. Event-driven architecture, dormancy, bounded caches, adaptive quality, suppressed background rendering, efficient I/O, and measured resource budgets.

Q194. What does “stable” require?
A. No critical crashes under certified workloads, bounded failure modes, recovery paths, stress/soak evidence, and regression CI.

Q195. What does “production-grade” require?
A. Correctness, security, performance, observability, recovery, compatibility, documentation, CI, and physical validation.

## 22. IMPLEMENTATION PRIORITY

Q196. What is the dependency-first implementation order?
A. Kernel -> memory -> scheduler -> process/userspace -> storage -> drivers -> networking/power -> graphics -> security -> native apps -> browser/media -> Windows -> Android -> gaming -> ZERO AI -> hardware certification.

Q197. Why not build the UI first?
A. UI depends on stable process, graphics, input, filesystem, service, and security contracts. Building it first creates throwaway work.

Q198. Why not build AI first?
A. ZERO AI depends on stable OS capability APIs, permissions, process isolation, filesystem, settings, networking, observability, and recovery.

Q199. Why not claim compatibility before runtime completion?
A. Detection or parsing does not establish behavioral compatibility.

Q200. What is the immediate engineering principle?
A. Close the highest-risk dependency and certification gates with executable evidence before expanding feature breadth.

## 23. FINAL REQUIREMENT MATRIX

Q201. Is ZEROOS required to be native?
A. Yes.

Q202. Is low overhead required?
A. Yes, with measured resource budgets.

Q203. Is Windows compatibility required?
A. Yes, as a core product goal.

Q204. Is Android compatibility required?
A. Yes, with isolated/dormant runtime design.

Q205. Is gaming required?
A. Yes, including modern AAA-class workloads where hardware/runtime permits.

Q206. Is Vulkan/GPU acceleration required?
A. Yes where hardware supports it, with fallback.

Q207. Is 1366x768 first-class?
A. Yes.

Q208. Is 4K default rendering required?
A. No.

Q209. Is controller support required?
A. No.

Q210. Is ZERO AI required?
A. Yes, as a deeply integrated personal ZEROOS capability layer.

Q211. Is Forge AI part of ZEROOS?
A. No. It is separate.

Q212. Can unused runtimes stay resident?
A. They should not; Windows/Android/ZERO AI/browser/background services should be dormant or suspended where practical.

Q213. Can unsupported hardware be declared supported?
A. No.

Q214. Can a host test close a hardware requirement?
A. No.

Q215. Can QEMU close a real-hardware requirement?
A. No.

Q216. Can a mock close an operational requirement?
A. No.

Q217. Can documentation close an implementation requirement?
A. No.

Q218. What is the final release condition?
A. All required stages have executable evidence, security/recovery gates pass, performance/resource measurements are documented, physical target hardware is certified, unsupported capabilities are explicitly recorded, and CI remains green.

## 24. MASTER HANDOFF INSTRUCTION FOR ANY NEW AI

Q219. What should a new AI do first?
A. Read this file plus ZEROOS_MASTER_BLUEPRINT.md, ZEROOS_MASTER_ROADMAP.md, ARCHITECTURE.md, HARDWARE.md, VALIDATION.md, and ZEROOS_10_STAGE_HARDENING.md.

Q220. What should it never do?
A. Never invent implementation status, never fake test results, never tick checkboxes without evidence, never treat host mocks as hardware support, and never silently replace requirements with generic assumptions.

Q221. How should it handle missing evidence?
A. Mark the requirement BLOCKED/PENDING and identify the exact executable evidence needed.

Q222. How should it handle performance claims?
A. Treat prior multipliers as targets/hypotheses until measured on the defined workload and hardware.

Q223. How should it handle compatibility claims?
A. Report exact tested application/API/hardware matrices and limitations.

Q224. How should it handle failures?
A. Reproduce, capture logs/trace, isolate the invariant violation, implement the smallest production-grade fix, rerun focused tests, rerun stress/regression, then update evidence.

Q225. What is the definition of “done”?
A. Implemented + tested + stressed + measured + documented + integrated + recovered from failure, with the correct evidence class.

## 25. REQUIRED SUBSYSTEM ACCEPTANCE QUESTIONS

Q226. Kernel: can repeated context switches preserve every required register?
A. Must be proven by executable stress tests.

Q227. Kernel: can interrupts return only to valid owned frames?
A. Must be proven by frame validation and stress/fault tests.

Q228. Memory: can memory pressure reclaim safely?
A. Must be proven by pressure/stress tests with no corruption.

Q229. Process: can a child exit while parent waits without races?
A. Must be proven under repeated concurrent tests.

Q230. IPC: can producer/consumer stress run without byte-stream corruption?
A. Must be proven with integrity checks and exact byte counts.

Q231. Storage: can a power-cut simulation recover?
A. Must be proven with crash/replay/fsck tests.

Q232. Driver: can malformed device input fail safely?
A. Must be proven with fault injection.

Q233. Network: can Wi-Fi sustain the target workload?
A. Must be measured on the actual adapter/router/environment.

Q234. Graphics: does hidden content stop rendering?
A. Must be measured using compositor/GPU counters or equivalent instrumentation.

Q235. Media: can 1080p sustain playback?
A. Must be measured on target hardware for the exact codec/path.

Q236. Browser: does a renderer crash stay isolated?
A. Must be proven with process/sandbox crash tests.

Q237. Windows: does a target application actually execute useful workloads?
A. Must be tested end-to-end, not inferred from PE parsing.

Q238. Android: does an APK install/run with correct permissions?
A. Must be tested end-to-end with the intended runtime.

Q239. Security: does denied access really fail closed?
A. Must be tested with negative/privilege-escalation cases.

Q240. Updates: does a bad update automatically recover?
A. Must be tested with interrupted/invalid update scenarios.

Q241. Recovery: can the system return to a known-good state?
A. Must be tested from realistic failures.

Q242. ZERO AI: can it perform a permitted action without kernel escape?
A. Must be tested through the permission/agent broker.

Q243. ZERO AI: does denial prevent the action?
A. Must be proven with explicit negative tests.

Q244. Performance: does background work back off under pressure?
A. Must be measured during CPU/disk/network/thermal contention.

Q245. Release: can the system survive long-duration mixed workloads?
A. Must be proven by soak testing on the relevant evidence class.

## 26. ENGINEERING NON-NEGOTIABLES

- Never use polling when an event-driven mechanism is practical.
- Never keep a dormant subsystem hot without a measured reason.
- Never render invisible UI.
- Never continuously decode/recompute static content.
- Never perform periodic disk writes without a bounded purpose.
- Never allow unbounded queues.
- Never allow starvation without a deliberate policy.
- Never silently ignore malformed input at trust boundaries.
- Never trust userspace pointers without validation.
- Never let a compatibility runtime obtain unrestricted kernel authority.
- Never let AI bypass capability controls.
- Never claim hardware support from host-only tests.
- Never claim performance multipliers as universal truths.
- Never ship an update path without rollback.
- Never ship a security boundary without negative tests.
- Never tick a release checkbox without evidence.
- Prefer simple, measurable, auditable mechanisms over hidden complexity.
- Preserve deterministic diagnostics for hard-to-reproduce failures.
- Keep unsupported features explicit rather than pretending compatibility.
- Optimize for sustained behavior, not one-second peak benchmarks.

## 27. CURRENT REPOSITORY CONTEXT TO PRESERVE

- Repository: priyanshagrahari54-blip/zeroos
- Master hardening checklist: [`ZEROOS_10_STAGE_HARDENING.md`](./ZEROOS_10_STAGE_HARDENING.md)
- Validation source: docs/VALIDATION.md
- AI handoff source: docs/ZEROOS_AI_HANDOFF.md
- Core roadmap: docs/ZEROOS_MASTER_ROADMAP.md
- Blueprint: docs/ZEROOS_MASTER_BLUEPRINT.md
- Architecture: docs/ARCHITECTURE.md
- Hardware: docs/HARDWARE.md
- Boot specification: docs/BOOT_SPEC.md
- Stage 1–2 gate: GitHub Issue #10
- Pipe hardening: PR #6
- Merged prior work: PR #7
- Scheduler deterministic trace implementation: commit e68f1bbf28b64aa41113f53addd6bcbb7a6b120c
- Panic diagnostic declaration: commit 20a444824ce24da57bd83a3988efd27edcd4efb7
- Panic diagnostic implementation: commit daabb211cb1a841e1d3ea2673724497ce2c0bb52
- Kernel hook: commit cb18e336b713b7bb9478963cbb3e976e1da10eff
- CI fault/trace gates: commit 6574dba4e443f7dec642ecd05518c0c96a72eb21
- Last known CI run at handoff: #1141 / run 36731939636, head 6574dba4e443f7dec642ecd05518c0c96a72eb21; status at the time of writing was in_progress. Verify current status before claiming green.

## 28. FINAL ONE-LINE PRODUCT DEFINITION

ZEROOS is a real native x86-64, low-overhead, secure, recoverable, modern desktop OS designed as a daily driver, with original UI, efficient graphics/media, Windows and Android compatibility, modern gaming capability, and a permission-brokered deeply integrated ZERO AI—where every production claim must be backed by executable evidence.


# 29. REMAINING DETAILED ENGINEERING INVENTORY

This section preserves additional engineering details that are easy to lose when the product requirements are summarized.

## 29.1 Boot and early kernel

- Multiboot2 is the bootstrap contract.
- x86-64 long mode must be entered before normal kernel execution.
- Bootstrap page tables must be validated before higher-level memory management takes over.
- Kernel entry must establish a known execution environment.
- Serial/debug output must remain available during early bring-up.
- Boot diagnostics must be deterministic enough to identify the last successful initialization phase.
- QEMU boot is a repeatable certification path, not the final hardware proof.
- Boot failures must produce bounded diagnostics rather than silent hangs.
- Reset/reboot loops must be detectable during stress testing.
- Early allocation must not depend on uninitialized higher-level allocators.

## 29.2 Physical and virtual memory

- Physical page ownership must be explicit.
- Bitmap allocation/free operations require invariant checks.
- Double-free, invalid-free, alignment, and out-of-range page operations must be tested.
- Virtual mappings require explicit permission state.
- MMIO mappings must not accidentally become ordinary cacheable RAM mappings.
- Huge-page splitting must preserve permissions and ownership.
- Address-space teardown must reclaim page-table structures safely.
- Page faults must distinguish not-present, permission, user/kernel, and malformed-address conditions.
- Copy-on-write must preserve reference counts and write-fault semantics.
- Guard pages should be used around critical stacks/buffers where practical.
- User mappings must never expose kernel-only pages.
- TLB invalidation/shootdown must be tied to actual mapping lifecycle.
- PCID optimization is conditional on CPU support.
- Higher-half/direct-map decisions must be documented and benchmarked.
- Swap is optional only if the chosen memory architecture explicitly provides another bounded pressure policy; no accidental overcommit is allowed.

## 29.3 Allocators

- Early boot allocator and production allocator have separate contracts.
- Slab/object allocation should minimize metadata overhead.
- Per-CPU caches can reduce lock contention but must have bounded memory retention.
- Reference counting requires overflow/underflow protection.
- Higher-order allocations need fragmentation/failure diagnostics.
- Allocation failures must be deterministic and recoverable where possible.
- No allocator may silently corrupt state on exhaustion.
- Stress tests must cover allocation/free churn and memory pressure.

## 29.4 Scheduler and context

- Scheduler entities need explicit ownership.
- Runqueue membership must have one authoritative state.
- A task cannot simultaneously be in incompatible states.
- Context-switch paths must document which stack/frame is authoritative at each transition.
- Interrupt return must never consume a stale frame.
- Yield and preemption must share well-defined lifecycle rules.
- Wait queues must not wake already-dead tasks.
- Timeout cancellation must not race with timeout firing.
- Zombie reaping must have a single owner/lifecycle rule.
- Idle tasks must never be treated as ordinary runnable user work.
- CPU offline paths must migrate or terminate runnable state safely.
- Scheduler diagnostics should include sequence numbers and transition counters.
- Deterministic traces should identify the certification checkpoint order.
- A full event trace remains stronger than the current seven-bit certification marker and can be added later for forensic debugging.

## 29.5 SMP

- Per-CPU data must be initialized before use.
- AP startup must have timeout/error handling.
- CPU online/offline transitions need ownership rules.
- Cross-CPU wakeups need memory-ordering guarantees.
- TLB shootdowns require acknowledgement/liveness rules.
- Scheduler load balancing must not migrate a task whose state is changing without synchronization.
- Interrupt affinity must be explicit.
- SMP tests must include uneven CPU load and CPU shutdown/restart paths where supported.

# 30. REMAINING STORAGE DETAILS

- Block devices need a common request abstraction.
- Requests should support merge/coalesce where safe.
- Queue depth must be bounded.
- HDD policy should consider seek locality and starvation.
- SSD/NVMe policy should avoid unnecessary seek-oriented heuristics.
- Read-ahead must adapt to sequentiality instead of blindly prefetching.
- Write-back must be bounded and pressure-aware.
- Dirty-page accounting must prevent uncontrolled cache growth.
- Direct I/O must bypass or coordinate with page cache according to explicit semantics.
- Async I/O needs cancellation/error reporting.
- Filesystem metadata needs integrity checks.
- Journaling must define transaction boundaries.
- Recovery must be idempotent where possible.
- Snapshot references must not leak indefinitely.
- Encryption must integrate with key lifecycle and recovery.
- File timestamps/permissions/ownership semantics must be stable.
- Path parsing must reject malformed/unsafe traversal cases.
- Mount/unmount must not invalidate live references.
- Device removal/error must propagate to waiting I/O.
- Filesystem tests need random crash injection, not only clean shutdown tests.

# 31. REMAINING NETWORK DETAILS

- Network buffers require ownership/lifetime rules.
- Packet parsing must validate every length before access.
- Ethernet frame bounds must be checked.
- ARP/ND state needs timeout and cache limits.
- IPv4/IPv6 parsing needs extension/header validation.
- UDP socket lifecycle must be race-safe.
- TCP requires sequence/ack/window/congestion state correctness.
- DNS cache needs TTL/size limits.
- DHCP renewal must not block the entire network stack.
- Firewall rules need deterministic ordering and logging policy.
- TLS should use a well-defined cryptographic implementation and certificate-validation model.
- Network failure must not spin CPU indefinitely.
- Reconnect/backoff must be bounded.
- Wi-Fi drivers must expose capability profiles rather than claiming universal adapter support.
- Throughput tests must distinguish LAN capability from Internet/ISP capability.

# 32. REMAINING DRIVER DETAILS

- Every device driver needs probe, initialize, operate, error, reset, suspend, resume, and remove lifecycle definitions where applicable.
- MMIO regions must be validated and mapped with correct permissions.
- DMA buffers need alignment, ownership, lifetime, and mapping state.
- IRQ handlers should do minimal work and defer expensive processing.
- Interrupt storms need detection and mitigation.
- Unsupported devices must fail cleanly.
- Device hotplug must not leave dangling references.
- USB descriptors must be length-validated.
- HID reports must be bounds-checked.
- Storage completion paths must tolerate device errors.
- GPU reset/failure needs recovery or safe fallback.
- Audio device loss needs graceful stream shutdown/reopen.
- Thermal sensor failure must not result in unsafe assumptions.
- ACPI parsing must validate table lengths/checksums/addresses.

# 33. REMAINING GRAPHICS DETAILS

- Display discovery must handle firmware-provided framebuffer information safely.
- Mode setting must validate dimensions/stride/pixel format.
- Surface ownership must be explicit.
- Damage regions must be merged/bounded.
- Occlusion must prevent work on fully hidden surfaces.
- Frame pacing must avoid busy waiting.
- Vsync/presentation behavior must be capability-dependent.
- GPU command buffers require validation before submission.
- GPU memory must have bounded accounting.
- GPU reset must not corrupt user-space state.
- Software framebuffer fallback must remain functional for unsupported hardware.
- Fonts should use efficient caching.
- UI text rendering must be resolution-aware.
- Scaling must avoid repeated expensive resampling.
- Screenshots should capture the compositor state without unnecessary copies where possible.
- Fullscreen applications should trigger desktop background suppression.
- Lock screen must stop unnecessary background rendering.

# 34. REMAINING MEDIA DETAILS

- Hardware decoder discovery must be capability-based.
- Codec profile/level limits must be checked.
- Unsupported codec must use explicit fallback/error behavior.
- Decoder buffers must have bounded lifetime.
- Audio/video synchronization must use a stable clock model.
- Dropped-frame policy should preserve responsiveness under pressure.
- 1080p is a target workload, not a universal guarantee.
- Thermal throttling must be observable during sustained playback.
- Hardware decode should be preferred when it reduces CPU/thermal cost.

# 35. REMAINING UI / UX DETAILS

- UI animations must have a global reduced-motion/low-power mode.
- Animation timers should stop when the surface is not visible.
- Notifications need priority, grouping, and suppression rules.
- Search indexing must be lazy/background and throttled.
- File manager must not recursively scan huge trees synchronously on the UI thread.
- Settings should expose resource/performance profiles.
- Performance Center should expose CPU/RAM/disk/network/GPU/thermal telemetry.
- Diagnostics should expose unsupported hardware and driver state clearly.
- Accessibility must include keyboard navigation, scaling, contrast, and reduced motion where implemented.
- Internationalization must avoid hard-coded layout assumptions.
- Input handling must separate hardware events from UI interpretation.
- Window lifecycle must survive minimize/restore/fullscreen transitions.
- UI crash must not imply kernel crash.

# 36. REMAINING NATIVE APPLICATION DETAILS

### ZERO Files
- Directory navigation
- Copy/move/delete
- Rename
- Search
- Hidden/system file policy
- Progress reporting
- Cancellation
- Permission errors
- Large-directory performance
- Crash-safe file operations

### ZERO Terminal
- PTY/terminal abstraction
- Process launch
- stdin/stdout/stderr
- signal/termination semantics
- resize handling
- history
- bounded scrollback
- secure privilege boundaries

### Settings
- Hardware profile
- Display
- Network
- Audio
- Power
- Security
- Updates
- Permissions
- AI controls
- Compatibility controls

### Diagnostics / Performance Center
- CPU utilization
- memory pressure
- disk latency/queue depth
- network throughput
- GPU utilization where available
- thermal sensors
- service state
- recent failures
- boot/recovery state
- benchmark execution

# 37. REMAINING WINDOWS RUNTIME DETAILS

- PE/COFF parser is only the beginning.
- Loader must establish process address space correctly.
- Imports/exports require deterministic resolution.
- Relocations require architecture-aware validation.
- Thread-local storage needs explicit semantics.
- Exception/unwind behavior needs compatibility planning.
- Win32 handles need translation to ZEROOS resources.
- Registry operations require a controlled persistence model.
- Windows paths need translation without security traversal.
- DLL search paths must be controlled to reduce hijacking risk.
- Process/thread priority mapping must be documented.
- Synchronization primitives need semantic compatibility.
- Named objects/IPC need an isolated namespace.
- Windows filesystem behavior must not bypass ZEROOS permissions.
- Network APIs need translation to the native socket layer.
- DirectX APIs need a tested graphics translation layer.
- Unsupported API calls must return documented errors rather than silently doing something different.
- Application compatibility must be recorded by actual application/workload.

# 38. REMAINING ANDROID DETAILS

- APK parser must validate ZIP/package boundaries.
- Manifest parsing must be safe.
- Signature verification must be explicit.
- Permission declarations must map to ZEROOS capability policy.
- Application UID/identity isolation must be maintained.
- Binder-like IPC needs lifetime and transaction limits.
- Runtime memory must be bounded.
- Android graphics must map to the ZEROOS graphics/GPU stack safely.
- Android filesystem access must be sandboxed.
- Background Android processes should be suspended/terminated according to lifecycle policy.
- Play Services-dependent features need explicit compatibility status.
- APK updates must be transactional and reversible.
- Uninstall must remove app-owned state according to documented policy.

# 39. REMAINING GAMING DETAILS

- Game process priority must be controlled without starving system safety tasks.
- Background indexing/download/update work should pause or throttle.
- Desktop compositor should minimize unnecessary work.
- Shader/cache storage must be bounded.
- GPU memory pressure must be visible.
- Game crashes must return to desktop without kernel failure.
- Compatibility layers must expose reproducible configuration profiles.
- Frame pacing matters in addition to peak FPS.
- Sustained thermal behavior matters more than short benchmark bursts.
- Memory leaks must be detected in long sessions.
- Network gaming latency must be measured separately from raw throughput.
- AAA support is a target, not a claim for every title until tested.

# 40. REMAINING SECURITY DETAILS

- Trust boundaries must be documented.
- Kernel/user boundary must be explicit.
- Service-to-service permissions must be explicit.
- Capability handles should be non-forgeable.
- Privileged syscalls need argument validation.
- Secrets must not appear in ordinary logs.
- Crash dumps need sensitive-data policy.
- Package verification must fail closed.
- Update rollback must protect against boot loops.
- Recovery tools must have controlled privilege.
- Debug builds must not accidentally weaken release security.
- Fault-injection builds must be clearly separated from production configurations.
- Security telemetry must be low-overhead and privacy-conscious.
- Fuzzing should target parsers, syscalls, IPC, filesystems, device descriptors, packages, and compatibility loaders.

# 41. REMAINING PERFORMANCE TECHNOLOGY INVENTORY

The following technologies/concepts were previously discussed as candidates or engineering targets. They are not all automatically adopted; each must pass architecture, correctness, benchmark, and maintenance gates.

| Area | Technology / concept | Intended ZEROOS use |
|---|---|---|
| Scheduling | EEVDF-style scheduling | Fair latency-sensitive CPU scheduling |
| Scheduling | Tickless operation | Reduce timer wakeups |
| Scheduling | Per-CPU runqueues | Reduce contention |
| Scheduling | CPU affinity | Predictable placement |
| Scheduling | Load balancing | Spread runnable work |
| Scheduling | RT/deadline classes | Special latency/deadline workloads |
| Scheduling | Wakeup preemption | Reduce interactive latency |
| Scheduling | Scheduler tracing | Diagnose latency/invariants |
| Memory | Demand paging | Load pages only when needed |
| Memory | COW | Efficient process/address-space duplication |
| Memory | MGLRU-style reclaim | Better working-set reclaim |
| Memory | Page cache | Avoid repeated storage reads |
| Memory | Adaptive read-ahead | Improve sequential I/O |
| Memory | Slab/object allocator | Efficient kernel object allocation |
| Memory | Per-CPU allocator caches | Reduce allocator lock contention |
| Memory | PCID | Reduce TLB flush cost where supported |
| Memory | Huge pages | Reduce page-table/TLB overhead where beneficial |
| Memory | Guard pages | Detect stack/buffer overrun |
| Storage | Request merging | Reduce device operations |
| Storage | HDD-aware elevator | Reduce seek overhead |
| Storage | Priority/aging | Balance latency and starvation |
| Storage | Bounded queues | Prevent runaway memory/latency |
| Storage | Write coalescing | Reduce write amplification |
| Storage | Journaling | Crash consistency |
| Storage | Checksums | Detect corruption |
| Storage | Snapshots | Recovery/update safety |
| Storage | Encryption | Data-at-rest protection |
| Network | NAPI-style polling/deferred processing | Avoid interrupt overload |
| Network | GRO | Aggregate receive packets |
| Network | GSO | Aggregate transmit work |
| Network | Zero/low-copy buffers | Reduce packet copies where safe |
| Network | Connection backoff | Avoid reconnect storms |
| Network | DNS cache | Reduce repeated resolution |
| Network | Firewall | Network policy enforcement |
| Graphics | Vulkan | Modern GPU rendering |
| Graphics | Damage tracking | Render only changed regions |
| Graphics | Occlusion culling | Avoid hidden rendering |
| Graphics | Retained scene graph | Reduce rebuild work |
| Graphics | Frame pacing | Stable presentation |
| Graphics | Surface caching | Reduce repeated allocation |
| Graphics | Fullscreen suppression | Stop desktop work behind games |
| Graphics | Hardware decode | Reduce CPU media cost |
| Graphics | Software fallback | Compatibility on weak hardware |
| UI | Vector assets | Resolution independence |
| UI | Font caching | Reduce text rendering cost |
| UI | Reduced-motion mode | Lower visual work/accessibility |
| Power | Device power states | Reduce idle energy |
| Power | CPU idle states | Reduce idle consumption |
| Power | Thermal governor | Protect sustained stability |
| Power | Background throttling | Reduce contention |
| Security | Secure Boot | Trust boot chain where supported |
| Security | Measured Boot | Attestation/measurement where supported |
| Security | NX/W^X | Reduce executable-memory abuse |
| Security | ASLR | Randomize layout |
| Security | Stack protection | Detect stack corruption |
| Security | CFI-style protection | Restrict invalid control flow |
| Security | IOMMU | DMA isolation where supported |
| Security | Sandboxing | Contain untrusted applications |
| Security | Capabilities | Least privilege |
| Security | Signed packages | Authentic software |
| Security | Atomic updates | Prevent partial activation |
| Security | Rollback | Recover from bad updates |
| Security | Fuzzing | Find parser/boundary bugs |
| Security | Sanitizers in test builds | Find memory/UB issues |
| Browser | Process isolation | Contain renderer failures |
| Browser | Site/content isolation | Reduce cross-content risk |
| Compatibility | PE/COFF loader | Windows application loading |
| Compatibility | API translation | Win32/Win64 compatibility |
| Compatibility | DX-to-Vulkan-style translation | Windows graphics |
| Compatibility | Android runtime isolation | Android application execution |
| Compatibility | Binder-like IPC | Android service/app communication |
| AI | Permission broker | Controlled ZERO AI actions |
| AI | Dormant backend | Low idle overhead |
| AI | Cancellable automation | Bounded system actions |
| AI | Audit log | Explain/track privileged actions |
| Recovery | Snapshot rollback | Known-good restore |
| Recovery | Safe mode | Minimal recovery environment |
| Recovery | Recovery terminal | Repair operations |
| Observability | Counters | Resource measurements |
| Observability | Structured logs | Machine-readable diagnostics |
| Observability | Tracepoints | Latency/debugging |
| Observability | Fault injection | Recovery validation |
| Testing | QEMU certification | Deterministic virtual validation |
| Testing | Hardware certification | Real-device validation |
| Testing | Soak tests | Long-duration stability |
| Testing | Regression CI | Prevent reintroduction of bugs |

## 41.1 Approximate performance targets previously discussed

These numbers are engineering targets/hypotheses, not guaranteed results:

- EEVDF-style scheduling: roughly 1.1–1.3x improvement in suitable fairness/latency workloads.
- MGLRU-style reclaim: roughly 1.1–1.5x under memory pressure in favorable workloads.
- HDD scheduling: roughly 1.2–2x in favorable seek/locality workloads.
- Sequential read-ahead: roughly 1.2–2x in suitable workloads.
- Vulkan/modern GPU path: roughly 1.1–2x in suitable graphics workloads.
- Hardware video decode: potentially 2–10x CPU-work reduction for suitable codecs/hardware.
- Overall unnecessary background/OS overhead: earlier target was roughly 2–4x lower than naive designs.
- None of these numbers may be presented as universal benchmarks until measured on defined hardware/workloads.

# 42. CURRENT IMPLEMENTATION / AUDIT SNAPSHOT TO PRESERVE

From the retained repository audit:

- Boot: Multiboot2 -> long mode -> paging -> kernel entry.
- Physical memory: bitmap allocator + tests.
- VMM: 4-level mappings, map/unmap, permissions, MMIO, huge-page splitting.
- GDT/TSS, interrupts, timer, IRQ paths exist.
- Scheduler is substantial but certification/hardening is a major gate.
- SMP has AP discovery/startup, GS/per-CPU, TLB shootdown plumbing, but requires complete certification.
- FPU/SSE/AVX context switching was not complete in the audited state.
- Process/thread, Ring-3, ELF, syscall, IPC/shared-memory pieces exist but require production certification.
- Storage has AHCI/NVMe/block/page-cache/VFS/ZJFS-related work and HDD scheduling features.
- Legacy IDE was detected but not fully driven.
- AHCI legacy INTx/polling fallback had limitations.
- Network has Ethernet/ARP/IPv4/IPv6/UDP/TCP/DNS/DHCP/socket pieces.
- Full Wi-Fi stack was not established.
- USB/audio cores existed.
- Display framebuffer discovery/MMIO mapping existed; boot target was 1024x768x32 in the audited path, while product certification target is 1366x768.
- Real GPU/Vulkan driver/runtime was not yet complete in the audit.
- Compositor had retained scene, damage, occlusion, pacing, and cache host tests.
- Window/input core existed; desktop shell chrome rendering was pending in the audit.
- Browser lifecycle/state existed; complete modern browser engine was not.
- File Manager/Terminal/Settings/Notifications/Accessibility/i18n/Performance Center cores existed.
- Security crypto such as ChaCha20/Poly1305/AEAD had verification, but complete policy enforcement/integration was incomplete.
- Windows compatibility foundation existed, but PE loader/Win32/DirectX runtime was not complete.
- Android runtime was absent in the audited state.
- ZERO AI permission/dormancy/broker contracts had host-tested pieces, but actual inference/backend was incomplete.
- Package/update/recovery architecture existed with transaction/rollback and recovery/snapshot test work.
- Thermal/power hardware implementation was incomplete.
- G560 certification was not complete.
- 1080p hardware media pipeline was not complete.
- Gaming was not implemented in the audited state.
- 100 Mbps Wi-Fi was not established by physical measurement.

# 43. CURRENT 10-STAGE EXECUTION DETAIL

Stage 0 — Reproducible engineering/CI:
- reproducible build
- pinned/known toolchain
- build artifacts
- boot smoke
- deterministic test entry
- regression preservation
- documentation/evidence format

Stage 1 — Kernel:
- scheduler
- context lifecycle
- interrupts
- task state
- wait/sleep
- preemption
- zombie/reaping
- SMP
- deterministic trace
- panic diagnostics
- register preservation
- stress/soak
- QEMU certification

Stage 2 — Userspace:
- process/thread
- PID/TID
- parent/child
- exec/exit/wait
- Ring-3
- syscall ABI
- safe copies
- page faults
- credentials
- permissions
- resource limits
- IPC
- pipe exact semantics
- concurrent stress
- recovery

Stage 3 — Storage:
- block layer
- request queues
- merge
- HDD elevator
- VFS
- filesystem
- journal
- page cache
- async/direct I/O
- fsck
- snapshots
- checksums
- encryption
- power-cut tests

Stage 4 — Drivers/platform:
- device model
- PCI/PCIe
- ACPI
- DMA
- IRQ
- USB/HID
- storage
- Ethernet/Wi-Fi
- audio
- power
- thermal
- malformed-device/fault tests

Stage 5 — Graphics/desktop:
- framebuffer
- display
- GPU abstraction
- compositor
- surfaces
- window manager
- input
- shell
- taskbar/launcher
- search
- notifications
- workspaces
- settings
- accessibility
- damage/occlusion/pacing
- low-power rendering

Stage 6 — Security/update/recovery:
- privilege separation
- capabilities
- permissions
- sandbox
- memory protections
- secure boot/TPM where supported
- package signatures
- update transaction
- rollback
- recovery
- fuzz/fault injection
- security regression

Stage 7 — GPU/media/browser/apps:
- real GPU path
- Vulkan
- fallback
- hardware decode
- 1080p workload
- browser engine
- renderer isolation
- native apps
- performance/thermal validation

Stage 8 — Windows/Android/gaming:
- PE loader
- Win32/Win64 APIs
- DLL/import
- registry/filesystem translation
- graphics translation
- Android runtime
- APK installer
- permissions/isolation
- gaming profile
- AAA workload matrix
- thermal/memory pressure
- controller remains out of scope

Stage 9 — ZERO AI:
- permission broker
- file/app/terminal/settings capabilities
- automation
- diagnostics
- coding/study
- audit
- cancellation
- offline behavior
- resource limits
- crash isolation
- security tests
- dormant behavior

Stage 10 — Hardware/release:
- G560 physical boot
- 1366x768
- HDD persistence
- 1080p playback where capable
- Wi-Fi measurements
- thermal sensors
- cold/warm reboot
- recovery
- rollback
- security regression
- long-duration soak
- performance/resource measurements
- release documentation

# 44. RELEASE GATE TRUTH TABLE

| Claim | Minimum evidence |
|---|---|
| boots | QEMU + target hardware as applicable |
| scheduler stable | focused stress + CI + QEMU |
| SMP stable | multi-vCPU + SMP stress + hardware where required |
| userspace stable | Ring-3/process/syscall/IPC stress |
| filesystem stable | crash/power-cut/replay/recovery |
| Wi-Fi supported | real adapter test |
| 100 Mbps target met | real adapter/network measurement |
| GPU acceleration | real supported GPU driver path |
| Vulkan support | actual Vulkan runtime/driver test |
| 1080p playback | sustained target hardware workload |
| browser support | real engine + isolation tests |
| Windows support | actual application/workload matrix |
| Android support | actual APK install/run matrix |
| gaming support | actual game/runtime matrix |
| secure update | signature + transactional + rollback test |
| recovery | realistic fault -> known-good restore |
| ZERO AI system action | permission-broker end-to-end test |
| low idle overhead | measured idle benchmark |
| long-term stability | soak test |
| release ready | all applicable stages + physical certification |

# 45. QUESTIONS THAT MUST NOT BE LOST IN FUTURE AI HANDOFFS

1. Is this implementation or only a contract?
2. Has the code actually executed?
3. On host, QEMU, or real hardware?
4. What exact commit was tested?
5. What exact command was run?
6. What was expected?
7. What was observed?
8. What happens under failure?
9. Is recovery tested?
10. Is the resource cost measured?
11. Is the feature dormant when unused?
12. Is there a polling loop that can become event-driven?
13. Can an invisible UI surface avoid rendering?
14. Can a background task be throttled?
15. Can a queue become unbounded?
16. Can a race leave duplicate ownership?
17. Can malformed input cross a trust boundary?
18. Can an update leave the machine unbootable?
19. Can compatibility code gain more privilege than required?
20. Can ZERO AI bypass permissions?
21. Is a performance multiplier measured or merely estimated?
22. Is a compatibility claim based on actual workload execution?
23. Does a QEMU result falsely imply hardware support?
24. Does a host test falsely imply kernel/hardware behavior?
25. Is the current checkbox genuinely backed by evidence?
26. Is there a missing negative test?
27. Is there a missing stress test?
28. Is there a missing long-duration test?
29. Is there a missing recovery test?
30. Is there a missing thermal/power test?
31. Is there a missing security regression?
32. Is there a missing documentation/evidence record?
33. Is the implementation advanced enough to avoid throwaway work?
34. Is an unsupported feature being honestly marked unsupported?
35. Does a fallback exist?
36. Does fallback have its own correctness/performance gate?
37. Does failure degrade predictably?
38. Does the feature increase idle CPU?
39. Does it increase idle RAM?
40. Does it increase disk writes?
41. Does it increase network traffic?
42. Does it increase GPU work?
43. Does it increase thermal load?
44. Does it interfere with foreground responsiveness?
45. Can it be suspended?
46. Can it be cancelled?
47. Can it be rolled back?
48. Can it be fuzzed?
49. Can it be fault-injected?
50. Can the exact failure be reproduced?
51. Does the diagnostic identify the invariant violation?
52. Is ownership/lifetime documented?
53. Is concurrency documented?
54. Is ABI behavior documented?
55. Are error codes deterministic?
56. Are timeouts bounded?
57. Are retries bounded?
58. Are caches bounded?
59. Are logs bounded?
60. Are telemetry costs bounded?
61. Is security fail-closed?
62. Is the capability scope minimal?
63. Are secrets protected?
64. Are crash dumps handled safely?
65. Is the update path atomic?
66. Is rollback automatic where required?
67. Is recovery independently bootable?
68. Does the browser isolate crashes?
69. Does Windows compatibility isolate crashes?
70. Does Android compatibility isolate crashes?
71. Does ZERO AI isolate crashes?
72. Does a GPU fault recover/fallback?
73. Does a network fault avoid CPU spin?
74. Does a storage fault propagate correctly?
75. Does a device removal leave dangling references?
76. Does a process exit wake the right waiters?
77. Does a timeout race remain correct?
78. Does a pipe close wake blocked operations?
79. Does partial pipe write report exact bytes?
80. Does a full nonblocking pipe return EAGAIN?
81. Does a timed-out pipe return ETIMEDOUT?
82. Does producer/consumer ordering remain exact?
83. Does scheduler context preserve all required state?
84. Does interrupt return use a valid owned frame?
85. Does SMP migration preserve task ownership?
86. Does page reclaim preserve active data?
87. Does COW preserve shared-page correctness?
88. Does filesystem replay preserve transaction semantics?
89. Does power-cut recovery preserve defined invariants?
90. Does the UI stop rendering hidden content?
91. Does wallpaper stop when covered/fullscreen?
92. Does weak hardware get a lower-cost profile?
93. Does 1080p playback remain stable over time?
94. Does gaming suppress unnecessary desktop work?
95. Does Wi-Fi target depend on external network conditions?
96. Does Android Play Services absence break documented app classes?
97. Does Windows API coverage match actual application requirements?
98. Does ZERO AI require network for the claimed function?
99. Is ZERO AI permission denial tested?
100. Is the entire release claim backed by evidence rather than documentation alone?

# 46. HANDOFF COMPLETENESS RULE

This document plus the linked master blueprint, roadmap, architecture, hardware, validation, hardening, and AI handoff documents should be treated as the retained engineering context. If a future AI finds a conflict, it must inspect the repository's current code/tests and newest evidence rather than assuming older chat statements are still true.

The repository state is authoritative for implementation. Executable evidence is authoritative for certification. The product requirements are authoritative for intended behavior. When these differ, report the difference explicitly and do not silently rewrite history.

## 47. MICRO-TO-MICRO CONTINUATION

For the granular implementation/validation checklist from boot through release, see `docs/ZEROOS_MICRO_REQUIREMENTS_FROM_START.md`. It is intentionally checklist-based and does not mark requirements complete merely because they are documented.

## 48. REMAINING GAP CLOSURE

Cross-cutting details that are easy to miss are tracked in `docs/ZEROOS_REMAINING_GAP_CLOSURE.md`, including toolchain/reproducibility, firmware edge cases, CPU/RAS, locking/memory ordering, crash forensics, time/locale, users/sessions, IPC/filesystem/network security, randomness, graphics robustness, UX reliability, package sandboxing, browser untrusted-content handling, compatibility runtime details, gaming measurement, ZERO AI agent safety/privacy, backups, observability, test infrastructure, formal invariants, documentation consistency, supply-chain security, installer/first boot, shutdown/reboot, and final repository-wide gap searches.

## End of exhaustive retained context.


---

# SOURCE 13: docs/ZEROOS_MICRO_REQUIREMENTS_FROM_START.md

# ZEROOS — MICRO-TO-MICRO ENGINEERING REQUIREMENTS FROM START
Version: 1.0
Status: Binding exhaustive engineering checklist / continuation context
Purpose: Preserve the smallest practical implementation, validation, failure, recovery, performance, security, and documentation requirements from boot through release. This document describes what must be built/proven; it does not mark implementation complete.

## 0. TRUTH / EVIDENCE RULES
- [ ] Never equate a declaration, stub, detection path, mock, host test, or compile success with operational support.
- [ ] Record exact commit SHA for every certification result.
- [ ] Record exact build command, configuration, toolchain, QEMU version/arguments, hardware identity, expected result, observed result, and artifact location.
- [ ] Separate host, unit, integration, QEMU, virtual multi-vCPU, and physical-hardware evidence.
- [ ] Every subsystem has functional, negative, race, stress, fault-injection, recovery, performance, security, and soak tests where applicable.
- [ ] Every resource owner has explicit lifetime and destruction rules.
- [ ] Every shared object has a concurrency contract.
- [ ] Every public ABI/API has argument, return-value, error, timeout, cancellation, and versioning rules.
- [ ] Every queue has capacity, backpressure, fairness, timeout, cancellation, and shutdown behavior.
- [ ] Every timer has ownership, cancellation, expiry, and teardown rules.
- [ ] Every interrupt path has frame ownership and return-path rules.
- [ ] Every DMA buffer has allocation, mapping, synchronization, ownership-transfer, and unmapping rules.
- [ ] Every privileged action has a trust boundary and least-privilege rule.
- [ ] Every persistent format has versioning and recovery rules.
- [ ] Every background task has dormancy/throttling behavior.
- [ ] Unsupported hardware/features are explicitly reported instead of silently claimed.
- [ ] No checkbox is ticked without executable evidence.

## 1. REPOSITORY / BUILD FOUNDATION
- [ ] Pin compiler/binutils versions or provide reproducible toolchain metadata.
- [ ] Pin build scripts and external dependencies.
- [ ] Define host prerequisites and versions.
- [ ] Make clean build deterministic.
- [ ] Make incremental build deterministic.
- [ ] Detect stale generated files.
- [ ] Fail on missing generated artifacts.
- [ ] Fail on compiler warnings that violate the release policy.
- [ ] Keep debug, sanitizer, fault-injection, and release configurations distinct.
- [ ] Record linker script version.
- [ ] Verify section layout.
- [ ] Verify kernel image bounds.
- [ ] Verify boot image contents.
- [ ] Verify symbol/debug artifact generation.
- [ ] Verify test binaries are from the same source revision.
- [ ] Make CI archive logs and machine-readable results.
- [ ] Add regression tests for every fixed kernel bug.
- [ ] Add a reproducibility check across two clean environments.
- [ ] Prevent host paths/timestamps/randomness from changing deterministic artifacts where practical.
- [ ] Document cross-compiler ABI assumptions.
- [ ] Document C/C++/assembly calling conventions.
- [ ] Document freestanding-library restrictions.
- [ ] Document which libc/runtime facilities are unavailable in kernel mode.

## 2. BOOT / FIRMWARE / EARLY CPU
- [ ] Validate boot protocol magic.
- [ ] Validate boot information pointer.
- [ ] Validate boot information size and structure bounds.
- [ ] Parse memory map defensively.
- [ ] Reject overlapping/invalid regions.
- [ ] Reserve kernel image pages.
- [ ] Reserve boot modules.
- [ ] Reserve bootloader structures until copied/retired.
- [ ] Reserve framebuffer/MMIO ranges.
- [ ] Reserve firmware-reported reserved regions.
- [ ] Record CPU vendor/features.
- [ ] Verify long-mode prerequisites.
- [ ] Establish known segment state.
- [ ] Establish known stack alignment.
- [ ] Establish early exception handlers before risky operations.
- [ ] Establish early serial/debug output.
- [ ] Establish early panic path.
- [ ] Establish bootstrap page tables.
- [ ] Verify identity mappings needed during transition.
- [ ] Verify kernel virtual mappings.
- [ ] Verify NX/permission policy where supported.
- [ ] Verify transition to long mode.
- [ ] Verify higher-half/direct-map design if used.
- [ ] Remove temporary mappings when safe.
- [ ] Verify stack guard strategy.
- [ ] Verify early allocator cannot return reserved pages.
- [ ] Verify boot-time allocations are accounted for.
- [ ] Verify CPU feature detection cannot execute unsupported instructions.
- [ ] Verify AP bootstrap trampoline location and lifetime.
- [ ] Verify boot continues without optional hardware.
- [ ] Verify malformed boot metadata cannot cause out-of-bounds reads.

## 3. PHYSICAL MEMORY / PAGE ALLOCATOR
- [ ] Define page size and alignment invariants.
- [ ] Define physical address width assumptions.
- [ ] Validate every free page lies in usable memory.
- [ ] Never free reserved pages.
- [ ] Never double-free a page.
- [ ] Detect allocator bitmap corruption.
- [ ] Test first/last page.
- [ ] Test fragmented free ranges.
- [ ] Test exhaustion.
- [ ] Test recovery after freeing.
- [ ] Test repeated allocate/free cycles.
- [ ] Test concurrent/per-CPU allocation if applicable.
- [ ] Define zeroing policy.
- [ ] Define DMA-capable allocation policy.
- [ ] Define contiguous allocation semantics.
- [ ] Define alignment semantics.
- [ ] Define allocation failure behavior.
- [ ] Track page ownership in debug builds.
- [ ] Track reference counts for shared physical pages where required.
- [ ] Verify refcount overflow/underflow handling.
- [ ] Verify page poisoning/debug patterns where enabled.
- [ ] Measure allocator latency and contention.
- [ ] Ensure debug accounting does not leak into release overhead unnecessarily.

## 4. VIRTUAL MEMORY / PAGE TABLES
- [ ] Define canonical-address validation.
- [ ] Validate user/kernel address boundaries.
- [ ] Validate mapping alignment.
- [ ] Validate length overflow before address arithmetic.
- [ ] Define map collision semantics.
- [ ] Define unmap absent-range semantics.
- [ ] Define permission transition semantics.
- [ ] Enforce user/supervisor isolation.
- [ ] Enforce writable/executable policy.
- [ ] Handle NX when supported.
- [ ] Handle huge-page mapping.
- [ ] Split huge pages safely before incompatible submapping.
- [ ] Reclaim empty page-table levels.
- [ ] Define page-table ownership/refcounting.
- [ ] Invalidate TLB correctly after changes.
- [ ] Use PCID only with correct lifecycle/flush semantics if enabled.
- [ ] Implement TLB shootdown ownership and acknowledgements for SMP.
- [ ] Test simultaneous map/unmap and context switch.
- [ ] Test unmap while another CPU accesses the address.
- [ ] Test stale TLB behavior.
- [ ] Test user access to kernel mappings.
- [ ] Test kernel access to unmapped user pages.
- [ ] Test guard pages.
- [ ] Test stack growth policy if supported.
- [ ] Test copy-on-write faults.
- [ ] Test page-fault recursion and failure.
- [ ] Test out-of-memory during page-table creation.
- [ ] Ensure partial page-table construction rolls back safely.

## 5. KERNEL ALLOCATORS / OBJECT LIFETIMES
- [ ] Define kmalloc/kfree semantics.
- [ ] Define alignment guarantees.
- [ ] Define zero-size allocation behavior.
- [ ] Detect integer overflow in size calculations.
- [ ] Detect double free in debug mode.
- [ ] Detect use-after-free in debug/fault builds where practical.
- [ ] Define slab/object cache ownership.
- [ ] Verify constructor/destructor ordering.
- [ ] Verify per-CPU cache flushing.
- [ ] Verify allocator behavior during interrupt context.
- [ ] Reject sleep-capable allocation from atomic context.
- [ ] Define GFP-like allocation classes if used.
- [ ] Bound emergency reserves.
- [ ] Measure fragmentation.
- [ ] Stress allocation under memory pressure.

## 6. GDT / TSS / CPU LOCAL STATE
- [ ] Define GDT entries and privilege levels.
- [ ] Define TSS ownership per CPU.
- [ ] Define kernel stack per thread.
- [ ] Define IST stacks for critical exceptions if used.
- [ ] Validate stack alignment at C entry.
- [ ] Validate GS/per-CPU base setup.
- [ ] Test CPU-local data before scheduler start.
- [ ] Test CPU-local data after AP startup.
- [ ] Test CPU migration and per-CPU references.
- [ ] Prevent stale per-CPU pointers after CPU offline.
- [ ] Test nested interrupt/exception behavior.

## 7. IDT / EXCEPTIONS / INTERRUPTS
- [ ] Install all required exception vectors.
- [ ] Define error-code normalization.
- [ ] Validate interrupt frame layout in assembly and C.
- [ ] Distinguish hardware IRQ from exception entry.
- [ ] Distinguish syscall return frame from IRQ return frame.
- [ ] Validate saved CS/SS privilege levels.
- [ ] Validate saved RIP/RSP canonicality.
- [ ] Validate RFLAGS constraints.
- [ ] Validate frame bounds against current kernel stack.
- [ ] Retire consumed frames exactly once.
- [ ] Invalidate stale frame references.
- [ ] Tag interrupt-exit paths.
- [ ] Reject impossible task/frame combinations.
- [ ] Define nested interrupt policy.
- [ ] Define interrupt masking rules.
- [ ] Verify spurious IRQ handling.
- [ ] Verify IRQ acknowledgement order.
- [ ] Verify exception during scheduler transition.
- [ ] Verify page fault during copy_from_user/copy_to_user.
- [ ] Verify double fault path.
- [ ] Verify NMI-safe assumptions where applicable.
- [ ] Verify panic path does not recursively corrupt state.

## 8. TIMER / CLOCK / TIMEKEEPING
- [ ] Define monotonic clock source.
- [ ] Define wall-clock source separately.
- [ ] Define frequency calibration.
- [ ] Detect timer drift.
- [ ] Define tickless idle behavior.
- [ ] Define timer-wheel/heap/list ownership.
- [ ] Define timer cancellation race semantics.
- [ ] Ensure expired timer cannot execute after cancellation.
- [ ] Ensure callback lifetime remains valid.
- [ ] Prevent timer callback use-after-free.
- [ ] Bound timer queue memory.
- [ ] Test millions of timers where feasible.
- [ ] Test simultaneous expiry.
- [ ] Test timeout near-zero boundary.
- [ ] Test clock wrap/large-duration arithmetic.
- [ ] Test suspend/resume clock behavior.
- [ ] Test CPU migration of timers.

## 9. TASK / THREAD / PROCESS CORE
- [ ] Define task states.
- [ ] Define legal state transitions.
- [ ] Define PID/TID allocation and reuse policy.
- [ ] Prevent stale PID/TID references.
- [ ] Define parent/child ownership.
- [ ] Define orphan handling.
- [ ] Define zombie state.
- [ ] Define reaping ownership.
- [ ] Define exit code lifetime.
- [ ] Define thread-group semantics.
- [ ] Define current_task invariants.
- [ ] Define runqueue membership invariant.
- [ ] Define waitqueue membership invariant.
- [ ] Prevent duplicate runqueue insertion.
- [ ] Prevent task on multiple mutually exclusive queues.
- [ ] Define task reference counting.
- [ ] Ensure references survive concurrent exit.
- [ ] Define kernel stack allocation/free lifetime.
- [ ] Ensure no task frees its own live stack prematurely.
- [ ] Test create/exit races.
- [ ] Test parent exits before child.
- [ ] Test child exits while parent waits.
- [ ] Test repeated PID reuse.
- [ ] Test resource exhaustion.

## 10. SCHEDULER / CONTEXT SWITCH
- [ ] Define scheduling classes.
- [ ] Define priority semantics.
- [ ] Define fairness semantics.
- [ ] Define wakeup semantics.
- [ ] Define preemption points.
- [ ] Define voluntary yield.
- [ ] Define timer preemption.
- [ ] Define sleep enqueue.
- [ ] Define timeout wake.
- [ ] Define waitqueue wake.
- [ ] Define runnable state transition.
- [ ] Define idle task.
- [ ] Define scheduler entry/exit locking.
- [ ] Define runqueue lock ownership.
- [ ] Define interrupt state across schedule().
- [ ] Save/restore all required general registers.
- [ ] Save/restore RBX/RBP/R12-R15.
- [ ] Define RIP/RSP restoration.
- [ ] Define RFLAGS restoration.
- [ ] Define segment/privilege state where required.
- [ ] Define FPU/SSE/AVX state ownership and lazy/eager policy.
- [ ] Define XSAVE feature mask if used.
- [ ] Test context switching under register sentinels.
- [ ] Test yield loops.
- [ ] Test timer preemption loops.
- [ ] Test mixed yield/preemption.
- [ ] Test sleep/wake race.
- [ ] Test timeout/exit race.
- [ ] Test wait/exit race.
- [ ] Test zombie/reap during scheduling.
- [ ] Test idle transitions.
- [ ] Test interrupt arriving during schedule.
- [ ] Test stale interrupt frame rejection.
- [ ] Test impossible task ID diagnostics.
- [ ] Test task-table invariant dump.
- [ ] Test deterministic scheduler trace.
- [ ] Test starvation bounds.
- [ ] Test priority inversion handling/policy where required.
- [ ] Test scheduler under memory pressure.
- [ ] Test scheduler under interrupt load.
- [ ] Test long-duration scheduler soak.

## 11. SMP / CPU HOTPLUG / CROSS-CPU
- [ ] Discover APs from firmware data.
- [ ] Start AP trampoline safely.
- [ ] Establish AP page tables.
- [ ] Establish AP GDT/TSS.
- [ ] Establish AP GS/per-CPU state.
- [ ] Establish AP interrupt state.
- [ ] Establish AP scheduler state.
- [ ] Verify AP reaches idle safely.
- [ ] Verify task migration.
- [ ] Define affinity mask semantics.
- [ ] Define load balancing.
- [ ] Define remote wakeups.
- [ ] Define IPI types.
- [ ] Define TLB shootdown protocol.
- [ ] Define shootdown acknowledgement timeout.
- [ ] Handle offline CPU without deadlock.
- [ ] Prevent task ownership duplication across CPUs.
- [ ] Test concurrent wake/migrate.
- [ ] Test CPU startup failure.
- [ ] Test partial SMP initialization.
- [ ] Test 1, 2, and maximum supported vCPU configurations.
- [ ] Stress SMP for long duration.
- [ ] Validate physical multi-core behavior before release claim.

## 12. USER/KERNEL ABI
- [ ] Define syscall numbering.
- [ ] Define register argument ABI.
- [ ] Define return-value ABI.
- [ ] Define errno/error encoding.
- [ ] Define restart/interruption semantics.
- [ ] Define pointer validation.
- [ ] Define size validation.
- [ ] Detect multiplication/addition overflow.
- [ ] Define copy_from_user semantics.
- [ ] Define copy_to_user semantics.
- [ ] Handle page fault during user copy without corrupting kernel state.
- [ ] Prevent TOCTOU where relevant.
- [ ] Bound syscall execution.
- [ ] Define cancellation/interruption behavior.
- [ ] Version ABI where necessary.
- [ ] Add negative tests for every syscall.
- [ ] Add fuzzing for syscall arguments.

## 13. ELF / EXEC / USER STACK
- [ ] Validate ELF magic/class/endian/machine.
- [ ] Validate program-header bounds.
- [ ] Validate segment sizes and alignment.
- [ ] Detect overlapping incompatible segments.
- [ ] Enforce segment permissions.
- [ ] Define ET_EXEC/PIE policy.
- [ ] Define ASLR policy.
- [ ] Build initial user stack correctly.
- [ ] Populate argc/argv/envp.
- [ ] Populate auxiliary vector as required.
- [ ] Define interpreter/dynamic-loader behavior.
- [ ] Define failure cleanup.
- [ ] Close inherited descriptors correctly.
- [ ] Test malformed ELF.
- [ ] Test truncated ELF.
- [ ] Test huge ELF.
- [ ] Test invalid entry point.
- [ ] Test exec failure without corrupting old process state.

## 14. SIGNALS / EVENTS / WAIT
- [ ] Define signal/event object lifetime.
- [ ] Define pending state.
- [ ] Define masking/blocking.
- [ ] Define delivery ordering.
- [ ] Define interrupted syscall behavior.
- [ ] Define default actions.
- [ ] Define process vs thread targeting.
- [ ] Define wait queues.
- [ ] Define wake-one vs wake-all.
- [ ] Prevent lost wakeups.
- [ ] Prevent duplicate wakeups.
- [ ] Test wake/timeout race.
- [ ] Test exit/wait race.
- [ ] Test signal during blocking syscall.

## 15. IPC / PIPES / SHARED MEMORY
- [ ] Define pipe capacity.
- [ ] Define byte_count/head/tail invariants.
- [ ] Define atomicity guarantee for small writes if required.
- [ ] Define partial-write semantics.
- [ ] Return exact accepted byte count.
- [ ] Preserve FIFO byte ordering.
- [ ] Full nonblocking write returns EAGAIN.
- [ ] Finite timeout returns ETIMEDOUT.
- [ ] Infinite blocking writer wakes after reader consumption.
- [ ] Reader wakes after producer writes.
- [ ] Close wakes blocked peers.
- [ ] Return EOF/EPIPE semantics correctly.
- [ ] Define broken-pipe race behavior.
- [ ] Prevent head/tail wrap arithmetic bugs.
- [ ] Prevent byte_count underflow/overflow.
- [ ] Stress multiple producers/consumers where supported.
- [ ] Stress close during blocked operations.
- [ ] Stress timeout simultaneous with wake.
- [ ] Define shared-memory mapping permissions.
- [ ] Define shared-memory lifetime.
- [ ] Define unmap while peer exits.
- [ ] Prevent stale IPC handles.
- [ ] Keep PR #6 separate until exact executable evidence exists.

## 16. FILE DESCRIPTORS / VFS API
- [ ] Define FD allocation/reuse.
- [ ] Prevent stale FD confusion.
- [ ] Define open-file-description sharing.
- [ ] Define reference counts.
- [ ] Define close-on-exec.
- [ ] Define dup/dup2 semantics if exposed.
- [ ] Define blocking/nonblocking flag behavior.
- [ ] Define seek semantics.
- [ ] Define stat metadata semantics.
- [ ] Define path normalization.
- [ ] Reject path traversal across sandbox roots.
- [ ] Define symlink policy.
- [ ] Define mount namespace/root semantics.
- [ ] Define permission checks.
- [ ] Define credential checks.
- [ ] Define concurrent unlink/open behavior.
- [ ] Define rename atomicity.
- [ ] Define directory iteration lifetime.
- [ ] Test descriptor exhaustion.
- [ ] Test close races.

## 17. BLOCK LAYER
- [ ] Define block-device abstraction.
- [ ] Define sector size.
- [ ] Define max request size.
- [ ] Validate request alignment.
- [ ] Validate range against device size.
- [ ] Define read/write completion.
- [ ] Define timeout behavior.
- [ ] Define retry policy.
- [ ] Define cancellation semantics.
- [ ] Define request ownership.
- [ ] Define bio/request lifetime.
- [ ] Define queue capacity/backpressure.
- [ ] Merge adjacent compatible requests.
- [ ] Do not merge incompatible barriers.
- [ ] Define flush/FUA semantics.
- [ ] Preserve ordering around barriers.
- [ ] Test device errors.
- [ ] Test partial I/O.
- [ ] Test queue exhaustion.
- [ ] Test hot removal where supported.

## 18. HDD / SSD / NVME SCHEDULING
- [ ] HDD scheduler accounts for seek locality.
- [ ] Use bounded queues.
- [ ] Use aging to prevent starvation.
- [ ] Define priority semantics.
- [ ] Avoid starvation of foreground I/O.
- [ ] Use read-ahead only when confidence is high.
- [ ] Stop read-ahead on random-access patterns.
- [ ] Bound read-ahead memory.
- [ ] Coalesce writes.
- [ ] Flush coalesced writes within defined latency bounds.
- [ ] Avoid HDD-specific assumptions on SSD/NVMe.
- [ ] Use queue depth appropriate to device.
- [ ] Validate NVMe completion ordering.
- [ ] Validate AHCI command ownership.
- [ ] Test queue reset/error recovery.
- [ ] Benchmark random/sequential read/write separately.

## 19. VFS / ZJFS / JOURNAL / RECOVERY
- [ ] Define inode/object identity.
- [ ] Define metadata layout.
- [ ] Define directory format.
- [ ] Define allocation bitmap/free-space metadata.
- [ ] Define journal record format/version.
- [ ] Define transaction boundaries.
- [ ] Define commit ordering.
- [ ] Define checksum coverage.
- [ ] Define replay ordering.
- [ ] Detect incomplete transactions.
- [ ] Detect corrupted journal records.
- [ ] Fail safely on unknown journal versions.
- [ ] Define fsck invariants.
- [ ] Define repair scope.
- [ ] Never silently fabricate file data.
- [ ] Define atomic rename/write semantics.
- [ ] Define durable fsync semantics.
- [ ] Define crash-consistent metadata updates.
- [ ] Test power cut at each critical write phase.
- [ ] Test repeated crash/replay cycles.
- [ ] Test full disk.
- [ ] Test journal full.
- [ ] Test corrupted metadata.
- [ ] Test corrupted checksum.
- [ ] Test snapshot create/delete under pressure.

## 20. PAGE CACHE / VM-FS INTEGRATION
- [ ] Define page-cache key.
- [ ] Define dirty/clean states.
- [ ] Define writeback ownership.
- [ ] Define writeback throttling.
- [ ] Define dirty-page limits.
- [ ] Prevent writeback storms.
- [ ] Coalesce adjacent dirty writes.
- [ ] Define cache invalidation after direct I/O.
- [ ] Define mmap interaction.
- [ ] Define truncate interaction.
- [ ] Define file deletion interaction.
- [ ] Prevent stale page-cache data after reuse.
- [ ] Define reclaim priority.
- [ ] Protect active working set.
- [ ] Test memory pressure during I/O.
- [ ] Test direct/buffered I/O interaction.

## 21. PCI / ACPI / DEVICE MODEL
- [ ] Enumerate PCI safely.
- [ ] Validate config-space access.
- [ ] Validate BAR sizes/addresses.
- [ ] Map MMIO with correct permissions.
- [ ] Unmap MMIO on device removal.
- [ ] Parse ACPI tables with length/checksum validation.
- [ ] Define device object lifetime.
- [ ] Define bus ownership.
- [ ] Define driver matching.
- [ ] Define bind/unbind.
- [ ] Define probe failure cleanup.
- [ ] Define suspend/resume callbacks.
- [ ] Define reset callbacks.
- [ ] Prevent driver access after unbind.
- [ ] Detect malformed device descriptors.
- [ ] Isolate unsupported devices.
- [ ] Preserve system operation when optional device fails.

## 22. DMA / IOMMU
- [ ] Define DMA-capable memory.
- [ ] Define cache-coherency assumptions.
- [ ] Define DMA mapping direction.
- [ ] Synchronize CPU/device ownership.
- [ ] Unmap DMA exactly once.
- [ ] Validate DMA lengths.
- [ ] Prevent DMA beyond buffer.
- [ ] Use IOMMU isolation where supported.
- [ ] Define IOMMU domain ownership.
- [ ] Flush mappings where required.
- [ ] Test malformed DMA descriptors in fault-capable drivers.
- [ ] Test device reset while DMA is active.

## 23. USB / HID
- [ ] Enumerate USB devices safely.
- [ ] Validate descriptors and lengths.
- [ ] Handle device disconnect.
- [ ] Handle endpoint errors.
- [ ] Define transfer timeout.
- [ ] Cancel transfers on teardown.
- [ ] Prevent use-after-free after disconnect.
- [ ] Implement keyboard HID.
- [ ] Implement pointer HID.
- [ ] Define input event timestamps.
- [ ] Define key repeat policy.
- [ ] Define hotplug events.
- [ ] Keep controller support explicitly out of product requirements unless separately introduced.

## 24. NETWORK CORE
- [ ] Define socket lifetime.
- [ ] Define buffer ownership.
- [ ] Validate packet lengths.
- [ ] Validate IP header lengths.
- [ ] Validate checksums.
- [ ] Handle fragmentation policy.
- [ ] Implement ARP/ND safely.
- [ ] Implement IPv4/IPv6 routing basics.
- [ ] Implement UDP semantics.
- [ ] Implement TCP state machine.
- [ ] Handle retransmission timers.
- [ ] Handle connection timeout.
- [ ] Handle reset/FIN races.
- [ ] Bound receive/transmit queues.
- [ ] Prevent packet-buffer leaks.
- [ ] Prevent CPU spin on malformed traffic.
- [ ] Define socket blocking/nonblocking behavior.
- [ ] Define select/poll/event mechanism if exposed.
- [ ] Test packet loss/reordering/duplication.
- [ ] Test connection storms.
- [ ] Test slow receiver/backpressure.

## 25. DHCP / DNS / FIREWALL / WIFI
- [ ] Validate DHCP packet lengths/options.
- [ ] Bound DHCP retries/backoff.
- [ ] Cache DNS safely with TTL.
- [ ] Bound DNS cache memory.
- [ ] Handle DNS timeout/failure.
- [ ] Prevent DNS parser overrun.
- [ ] Define firewall rule ordering.
- [ ] Define default policy.
- [ ] Define connection-state tracking if used.
- [ ] Bound firewall state tables.
- [ ] Define Wi-Fi device abstraction.
- [ ] Implement only tested adapters with explicit capability reporting.
- [ ] Handle scan/connect/disconnect/reconnect lifecycle.
- [ ] Bound reconnect storms with backoff.
- [ ] Measure throughput, latency, packet loss, reconnect time.
- [ ] Measure power/thermal impact.
- [ ] Verify 100 Mbps target only on capable real hardware/network conditions.

## 26. AUDIO
- [ ] Enumerate audio devices.
- [ ] Define sample formats.
- [ ] Define buffer/ring-buffer ownership.
- [ ] Define period size.
- [ ] Define latency target.
- [ ] Handle underrun/overrun.
- [ ] Handle device unplug.
- [ ] Define volume/mute/routing.
- [ ] Prevent audio thread busy-spin.
- [ ] Test long playback.
- [ ] Test concurrent playback/recording where supported.
- [ ] Test unsupported codec/device behavior.

## 27. POWER / ACPI / THERMAL
- [ ] Detect AC/battery state.
- [ ] Define CPU idle states.
- [ ] Define device runtime power states.
- [ ] Define suspend/resume state machine.
- [ ] Define wake sources.
- [ ] Restore devices in dependency order.
- [ ] Reinitialize timers after resume.
- [ ] Revalidate network after resume.
- [ ] Revalidate graphics after resume.
- [ ] Revalidate storage after resume.
- [ ] Read thermal sensors safely.
- [ ] Define thermal thresholds/hysteresis.
- [ ] Throttle background tasks first.
- [ ] Reduce animation under thermal pressure.
- [ ] Reduce optional decode/compute load.
- [ ] Avoid thermal oscillation.
- [ ] Test prolonged thermal load.
- [ ] Test sensor failure/missing sensor.
- [ ] Test resume after thermal throttling.

## 28. DISPLAY / FRAMEBUFFER
- [ ] Discover framebuffer correctly.
- [ ] Validate framebuffer address/size.
- [ ] Validate stride/pixel format.
- [ ] Map framebuffer safely.
- [ ] Prevent out-of-bounds scanline access.
- [ ] Handle 1366x768 target profile.
- [ ] Handle fallback resolutions.
- [ ] Define scaling policy.
- [ ] Define HiDPI policy.
- [ ] Avoid unnecessary 4K assets.
- [ ] Use vector/resolution-aware UI assets.
- [ ] Define font fallback.
- [ ] Cache glyphs with bounded memory.
- [ ] Test display hotplug if supported.
- [ ] Test invalid display mode.

## 29. GPU / VULKAN / RENDERING
- [ ] Define GPU device abstraction.
- [ ] Enumerate supported GPU capabilities.
- [ ] Define command-buffer ownership.
- [ ] Define synchronization primitives.
- [ ] Define GPU memory allocation.
- [ ] Handle GPU memory pressure.
- [ ] Handle device reset/fault.
- [ ] Define fence/semaphore lifecycle.
- [ ] Define queue submission.
- [ ] Define present path.
- [ ] Avoid CPU busy-wait on GPU completion.
- [ ] Implement Vulkan only with an actual compatible driver/runtime path.
- [ ] Provide software/fallback path where practical.
- [ ] Validate shaders/resources.
- [ ] Bound shader/cache storage.
- [ ] Measure frame latency and CPU/GPU utilization.
- [ ] Test long-duration rendering.
- [ ] Test GPU reset recovery.
- [ ] Do not claim Vulkan support from headers/contracts alone.

## 30. COMPOSITOR / WINDOW MANAGER
- [ ] Define surface lifetime.
- [ ] Define buffer ownership.
- [ ] Define damage regions.
- [ ] Merge overlapping damage.
- [ ] Skip occluded surfaces.
- [ ] Skip hidden/minimized surfaces.
- [ ] Stop wallpaper when covered/fullscreen/locked.
- [ ] Define frame pacing.
- [ ] Bound compositor work per frame.
- [ ] Avoid full-screen redraw for small changes.
- [ ] Cache reusable surfaces.
- [ ] Release cache under pressure.
- [ ] Define window focus.
- [ ] Define z-order.
- [ ] Define workspace membership.
- [ ] Define minimize/maximize/fullscreen.
- [ ] Define resize synchronization.
- [ ] Define input routing.
- [ ] Test compositor under many windows.
- [ ] Test one-pixel damage.
- [ ] Test fullscreen game transition.
- [ ] Test display sleep/wake.

## 31. DESKTOP SHELL / UX
- [ ] Define launcher/taskbar lifecycle.
- [ ] Define search indexing policy.
- [ ] Make indexing lazy/background/throttled.
- [ ] Define notification queue bounds.
- [ ] Coalesce duplicate notifications.
- [ ] Define workspaces.
- [ ] Define control center.
- [ ] Define settings model.
- [ ] Define accessibility settings.
- [ ] Define keyboard navigation.
- [ ] Define focus traversal.
- [ ] Define high-contrast/reduced-motion profiles.
- [ ] Define screenshot path.
- [ ] Define clipboard ownership/lifetime.
- [ ] Define drag-and-drop ownership.
- [ ] Define crash recovery of shell.
- [ ] Shell crash must not imply kernel failure.

## 32. WALLPAPER / MEDIA
- [ ] Static wallpaper performs no continuous animation loop.
- [ ] Animated wallpaper uses adaptive FPS.
- [ ] Reduce 60 -> 30 -> 20 -> 15 -> static/suspend under pressure.
- [ ] Stop rendering when not visible.
- [ ] Stop rendering during fullscreen/game mode.
- [ ] Stop rendering on lock where appropriate.
- [ ] Bound decoded frame memory.
- [ ] Reuse decode buffers.
- [ ] Prefer hardware decode when supported.
- [ ] Fall back safely to software decode.
- [ ] Avoid unnecessary 4K decode on 1366x768 displays.
- [ ] Test sustained 1080p playback.
- [ ] Test seek/pause/resume.
- [ ] Test malformed media.
- [ ] Test decoder reset.
- [ ] Measure CPU/GPU/thermal behavior.

## 33. NATIVE APPS
For each app:
- [ ] Define startup dependencies.
- [ ] Define IPC/API contract.
- [ ] Define file/resource ownership.
- [ ] Define crash recovery.
- [ ] Define background/dormant behavior.
- [ ] Define permission requirements.
- [ ] Define update/rollback compatibility.
- [ ] Define bounded caches.
- [ ] Define telemetry/logging policy.
- [ ] Define accessibility.
- [ ] Define localization.
- [ ] Define low-memory behavior.
- [ ] Define offline behavior.

Required apps:
- [ ] ZERO Files.
- [ ] ZERO Terminal.
- [ ] Browser.
- [ ] Notes.
- [ ] PDF viewer.
- [ ] Calculator.
- [ ] Dictionary.
- [ ] Study Center.
- [ ] Code Editor.
- [ ] Media Player.
- [ ] Settings.
- [ ] Diagnostics.
- [ ] Performance Center.
- [ ] Notifications/control center components.

## 34. BROWSER
- [ ] Use a real browser engine/runtime for the full product claim.
- [ ] Define process model.
- [ ] Isolate renderer/content processes.
- [ ] Define sandbox.
- [ ] Define site/content isolation where supported.
- [ ] Define GPU-process isolation.
- [ ] Define network-process isolation where applicable.
- [ ] Define crash restart.
- [ ] Define permission prompts.
- [ ] Define cookie/storage policy.
- [ ] Define download sandbox.
- [ ] Define file chooser permissions.
- [ ] Define certificate/TLS policy.
- [ ] Define safe update/rollback.
- [ ] Bound tab/process memory.
- [ ] Suspend background tabs.
- [ ] Avoid periodic polling when event-driven alternatives exist.
- [ ] Test malicious/malformed content.
- [ ] Test renderer crash without browser-wide/kernel failure.

## 35. SECURITY BASELINE
- [ ] Define threat model.
- [ ] Define trust boundaries.
- [ ] Define kernel/user boundary.
- [ ] Define service privilege boundaries.
- [ ] Define capability handles.
- [ ] Prevent capability forgery.
- [ ] Validate privileged syscall arguments.
- [ ] Enforce least privilege.
- [ ] Protect secrets from ordinary logs.
- [ ] Define crash-dump sensitivity policy.
- [ ] Enable NX/W^X where supported.
- [ ] Enable ASLR where practical.
- [ ] Enable stack protection.
- [ ] Add CFI-style protections where practical.
- [ ] Use memory-safe components where suitable.
- [ ] Validate all parsers.
- [ ] Fuzz syscalls, IPC, filesystem, packages, descriptors, compatibility loaders.
- [ ] Run sanitizer/fault-injection configurations.
- [ ] Keep debug/fault builds from weakening release policy accidentally.
- [ ] Measure security telemetry overhead.

## 36. CRYPTO / STORAGE ENCRYPTION
- [ ] Define cryptographic primitive usage boundaries.
- [ ] Never invent cryptographic protocol semantics.
- [ ] Validate nonce uniqueness requirements.
- [ ] Validate authentication before plaintext use.
- [ ] Protect keys in memory as practical.
- [ ] Define key storage/rotation.
- [ ] Define failure behavior for authentication errors.
- [ ] Test known vectors.
- [ ] Test tampering.
- [ ] Test truncated ciphertext/tag.
- [ ] Test replay policy where applicable.
- [ ] Define encrypted filesystem recovery behavior.
- [ ] Define lost-key behavior explicitly.

## 37. SECURE BOOT / UPDATE / PACKAGES
- [ ] Define trust root.
- [ ] Verify package signatures.
- [ ] Fail closed on invalid signatures.
- [ ] Define metadata authenticity.
- [ ] Define anti-rollback policy where required.
- [ ] Define package dependency graph.
- [ ] Detect dependency cycles.
- [ ] Define install transaction.
- [ ] Define pre-activation validation.
- [ ] Activate atomically.
- [ ] Preserve previous known-good version.
- [ ] Define automatic rollback triggers.
- [ ] Prevent boot loops after update.
- [ ] Define recovery update path.
- [ ] Define package uninstall ownership.
- [ ] Never remove shared dependencies incorrectly.
- [ ] Bound package cache.
- [ ] Bound update download bandwidth/background activity.
- [ ] Throttle updates under foreground/thermal/battery pressure.
- [ ] Test interrupted download.
- [ ] Test interrupted activation.
- [ ] Test corrupted package.
- [ ] Test failed reboot.
- [ ] Test rollback.

## 38. RECOVERY / SNAPSHOT / SAFE MODE
- [ ] Define independently bootable recovery path.
- [ ] Define recovery privilege boundary.
- [ ] Define filesystem repair mode.
- [ ] Define package rollback.
- [ ] Define snapshot restore.
- [ ] Define safe-mode service set.
- [ ] Disable optional background services in recovery.
- [ ] Provide diagnostic logs.
- [ ] Protect recovery from ordinary application modification.
- [ ] Test kernel boot failure.
- [ ] Test broken update.
- [ ] Test filesystem inconsistency.
- [ ] Test corrupted configuration.
- [ ] Test failed driver initialization.
- [ ] Verify return to known-good system.

## 39. WINDOWS COMPATIBILITY
- [ ] Define supported PE/COFF variants.
- [ ] Validate PE headers.
- [ ] Validate sections.
- [ ] Map image with correct permissions.
- [ ] Resolve imports.
- [ ] Resolve DLL dependencies.
- [ ] Define DLL lifetime/refcounting.
- [ ] Define relocation/ASLR policy.
- [ ] Define TLS callbacks if required.
- [ ] Define exception/unwind model.
- [ ] Define Windows thread-local storage.
- [ ] Define Win32 handle model.
- [ ] Define registry translation.
- [ ] Define filesystem/path translation.
- [ ] Define environment variable semantics.
- [ ] Define process creation semantics.
- [ ] Define synchronization primitives.
- [ ] Define virtual memory API mapping.
- [ ] Define file I/O mapping.
- [ ] Define sockets mapping.
- [ ] Define graphics API translation.
- [ ] Define DirectX-to-Vulkan/other translation only where technically viable.
- [ ] Isolate compatibility processes.
- [ ] Prevent compatibility layer from gaining kernel privileges unnecessarily.
- [ ] Build actual application compatibility matrix.
- [ ] Record exact application versions tested.
- [ ] Record unsupported APIs.
- [ ] Test crash containment.
- [ ] Test installer/uninstaller behavior.
- [ ] Do not claim generic Windows compatibility from PE parsing alone.

## 40. ANDROID COMPATIBILITY
- [ ] Define APK/package parsing.
- [ ] Verify package signature.
- [ ] Define APK installation transaction.
- [ ] Define app sandbox.
- [ ] Define app UID/identity mapping.
- [ ] Define filesystem namespace.
- [ ] Define permissions.
- [ ] Define Binder-like IPC/runtime services as required.
- [ ] Define Android lifecycle.
- [ ] Suspend/stop inactive runtime components.
- [ ] Avoid resident Play Services.
- [ ] Avoid resident native Play Store app.
- [ ] Browser may access Play Store web.
- [ ] Document that supported Google web flow may not provide direct APK installation.
- [ ] Define Play-Services-dependent app compatibility limitations.
- [ ] Define notification/background policy.
- [ ] Define network permissions.
- [ ] Define camera/microphone/sensor permissions.
- [ ] Define uninstall state cleanup.
- [ ] Define APK update rollback.
- [ ] Test real APK install/run matrix.
- [ ] Test crash containment.
- [ ] Test malicious APK.
- [ ] Do not claim Android support from package parsing alone.

## 41. GAMING
- [ ] Define game compatibility profiles.
- [ ] Define foreground priority policy.
- [ ] Pause/throttle indexing.
- [ ] Pause/throttle updates/downloads.
- [ ] Suppress unnecessary desktop rendering.
- [ ] Suppress wallpaper under fullscreen/game mode.
- [ ] Define shader/cache storage limits.
- [ ] Define GPU-memory pressure behavior.
- [ ] Define frame-pacing metrics.
- [ ] Define input latency metrics.
- [ ] Define network-latency metrics separately from throughput.
- [ ] Define crash-to-desktop behavior.
- [ ] Define thermal sustained-performance test.
- [ ] Define memory-leak soak.
- [ ] Define game/runtime configuration capture.
- [ ] Build real game workload matrix.
- [ ] Test GTA-V-class workload as a target workload, not as an automatic universal claim.
- [ ] Controller support remains out of scope under current requirements.

## 42. ZERO AI
- [ ] Define ZERO AI as personal ZEROOS AI, separate from Forge AI.
- [ ] Define permission broker.
- [ ] Define capability vocabulary.
- [ ] Define file capabilities.
- [ ] Define application-control capabilities.
- [ ] Define terminal capabilities.
- [ ] Define settings capabilities.
- [ ] Define diagnostics capabilities.
- [ ] Define coding/study capabilities.
- [ ] Define automation capabilities.
- [ ] Require explicit authorization for privileged actions.
- [ ] Log security-relevant actions.
- [ ] Make actions cancellable.
- [ ] Bound action duration.
- [ ] Bound retries.
- [ ] Bound memory/CPU/network use.
- [ ] Keep inference/backend dormant when unused.
- [ ] Define offline behavior.
- [ ] Define network-required behavior.
- [ ] Fail closed on denied permissions.
- [ ] Prevent prompt/content from bypassing capability checks.
- [ ] Isolate AI backend crashes.
- [ ] Define rollback of AI-triggered system changes where practical.
- [ ] Test malicious instructions attempting privilege escalation.
- [ ] Test permission denial.
- [ ] Test cancellation during destructive/long actions.
- [ ] Test audit trail integrity.
- [ ] Test AI operation under low-memory/thermal pressure.
- [ ] Never give unrestricted kernel access to the AI.

## 43. OBSERVABILITY / DIAGNOSTICS
- [ ] Define structured log format.
- [ ] Define log severity.
- [ ] Bound log storage.
- [ ] Rate-limit repetitive errors.
- [ ] Protect secrets from logs.
- [ ] Define counters for CPU/memory/I/O/network/GPU.
- [ ] Define scheduler tracepoints.
- [ ] Define allocation counters.
- [ ] Define page-fault counters.
- [ ] Define filesystem latency counters.
- [ ] Define network latency/packet counters.
- [ ] Define frame-time counters.
- [ ] Define thermal counters.
- [ ] Define power-state counters.
- [ ] Make tracing dynamically disableable.
- [ ] Keep release telemetry overhead low.
- [ ] Preserve crash diagnostics through controlled recovery.

## 44. PERFORMANCE / RESOURCE BUDGETS
- [ ] Measure idle CPU.
- [ ] Measure idle RAM.
- [ ] Measure idle wakeups.
- [ ] Measure process launch.
- [ ] Measure context-switch latency.
- [ ] Measure syscall latency.
- [ ] Measure allocation latency.
- [ ] Measure page-fault latency.
- [ ] Measure filesystem latency.
- [ ] Measure HDD sequential/random throughput.
- [ ] Measure NVMe behavior separately.
- [ ] Measure Wi-Fi throughput/latency.
- [ ] Measure compositor frame time.
- [ ] Measure wallpaper GPU/CPU use.
- [ ] Measure browser memory.
- [ ] Measure app launch memory/I/O.
- [ ] Measure suspend/resume.
- [ ] Measure thermal throttling.
- [ ] Measure long-duration stability.
- [ ] Treat earlier 1.1–2x / 2–4x estimates as hypotheses until measured.
- [ ] Never publish estimated multipliers as universal benchmarks.
- [ ] Compare foreground latency before/after background work.
- [ ] Verify weak hardware degrades predictably.

## 45. TEST MATRIX
For each subsystem:
- [ ] Happy path.
- [ ] Empty input.
- [ ] Minimum input.
- [ ] Maximum input.
- [ ] Invalid input.
- [ ] Malformed input.
- [ ] Resource exhaustion.
- [ ] Timeout.
- [ ] Cancellation.
- [ ] Concurrent access.
- [ ] Interrupt/preemption at sensitive point.
- [ ] Process/thread exit during operation.
- [ ] Device removal during operation where applicable.
- [ ] Power loss/crash where persistent.
- [ ] Recovery.
- [ ] Long-duration soak.
- [ ] Performance baseline.
- [ ] Security/fuzz test where exposed to untrusted input.
- [ ] Regression test after every bug fix.

## 46. STRESS / FAULT INJECTION
- [ ] Randomized scheduler stress.
- [ ] Repeated process creation/exit.
- [ ] Repeated fork/exec equivalent where supported.
- [ ] IPC producer/consumer stress.
- [ ] Memory pressure stress.
- [ ] Filesystem create/delete/rename stress.
- [ ] Journal crash injection.
- [ ] Storage error injection.
- [ ] Network packet corruption/loss injection.
- [ ] Device reset injection.
- [ ] GPU reset injection where possible.
- [ ] Thermal-pressure simulation/test.
- [ ] Update interruption injection.
- [ ] Permission denial injection.
- [ ] AI action cancellation injection.
- [ ] Repeated suspend/resume.
- [ ] Multi-day soak on target hardware.

## 47. CI / CERTIFICATION GATES
- [ ] Clean build gate.
- [ ] Boot smoke gate.
- [ ] Scheduler deterministic trace gate.
- [ ] Scheduler panic-diagnostics gate.
- [ ] Context-register gate.
- [ ] SMP multi-vCPU gate.
- [ ] Userspace Ring-3 gate.
- [ ] Syscall negative gate.
- [ ] IPC/pipe exact-contract gate.
- [ ] Storage crash/replay gate.
- [ ] Driver malformed-input gate.
- [ ] Network parser gate.
- [ ] Graphics compositor gate.
- [ ] Security regression gate.
- [ ] Package signature/rollback gate.
- [ ] Recovery boot gate.
- [ ] Browser isolation gate.
- [ ] Compatibility workload gate.
- [ ] AI permission/dormancy gate.
- [ ] Physical-hardware gate.
- [ ] Long-duration soak gate.
- [ ] Performance measurement gate.
- [ ] Documentation/evidence gate.

## 48. PHYSICAL G560 CERTIFICATION
- [ ] Identify exact Lenovo G560 hardware configuration.
- [ ] Record CPU model/core count.
- [ ] Record RAM.
- [ ] Record storage model/interface.
- [ ] Record display panel/native resolution.
- [ ] Record Wi-Fi chipset.
- [ ] Record Ethernet chipset.
- [ ] Record audio chipset.
- [ ] Record GPU/iGPU/dGPU.
- [ ] Record BIOS/firmware version.
- [ ] Record ACPI behavior.
- [ ] Boot from cold power-on.
- [ ] Reboot repeatedly.
- [ ] Verify 1366x768.
- [ ] Verify HDD persistence if applicable.
- [ ] Verify keyboard/touchpad.
- [ ] Verify Ethernet.
- [ ] Verify tested Wi-Fi.
- [ ] Verify audio.
- [ ] Verify suspend/resume if supported.
- [ ] Verify thermal sensors.
- [ ] Verify 1080p playback where hardware path supports it.
- [ ] Verify recovery.
- [ ] Verify update/rollback.
- [ ] Run long-duration soak.
- [ ] Capture all logs/artifacts.
- [ ] Record unsupported devices explicitly.

## 49. RELEASE DOCUMENTATION
- [ ] Architecture document matches current code.
- [ ] Hardware document matches tested hardware.
- [ ] Validation document lists real evidence.
- [ ] Hardening checklist contains evidence links.
- [ ] Roadmap reflects actual state.
- [ ] Requirements document contains intended behavior.
- [ ] Unsupported-feature list is current.
- [ ] Known-bug list is current.
- [ ] Recovery procedure is tested.
- [ ] Installation procedure is tested.
- [ ] Update/rollback procedure is tested.
- [ ] Performance report uses measured numbers only.
- [ ] Security report identifies limitations.
- [ ] Compatibility matrix lists exact tested workloads.
- [ ] Release notes distinguish implemented, tested, experimental, and unsupported.

## 50. FINAL RELEASE TRUTH
- [ ] Boot is reproducible.
- [ ] Kernel invariants are proven.
- [ ] Scheduler/context lifecycle is proven.
- [ ] SMP is proven for the supported profile.
- [ ] Ring-3/userspace isolation is proven.
- [ ] Syscalls are fuzzed/negative-tested.
- [ ] IPC semantics are exact and stress-tested.
- [ ] Storage survives crash/recovery tests.
- [ ] Drivers fail safely.
- [ ] Networking is measured on real hardware.
- [ ] Graphics/GPU claims have real driver evidence.
- [ ] 1080p claims have sustained workload evidence.
- [ ] Browser claim has a real engine and isolation evidence.
- [ ] Windows claim has actual workload evidence.
- [ ] Android claim has actual APK workload evidence.
- [ ] Gaming claim has actual workload evidence.
- [ ] ZERO AI actions pass permission/cancellation/audit tests.
- [ ] Idle resource use is measured.
- [ ] Thermal behavior is measured.
- [ ] Recovery is independently validated.
- [ ] Update rollback is validated.
- [ ] Security regression is green.
- [ ] Long-duration soak is green.
- [ ] G560 physical certification is complete before Stage 10 release claim.
- [ ] Every remaining unsupported item is explicitly documented.
- [ ] No documentation-only checkbox is treated as implementation evidence.

## HANDOFF RULE
A future engineer/AI must start at section 0 and proceed in order. For every unchecked item, inspect current code/tests first, then implement only what is actually missing, add executable evidence, and update the authoritative evidence documents. Never infer completion from old chat history.


---

# SOURCE 14: docs/ZEROOS_REMAINING_GAP_CLOSURE.md

# ZEROOS — REMAINING GAP CLOSURE INVENTORY
Status: Binding supplemental checklist
Rule: This file covers cross-cutting details that are easy to omit from subsystem checklists. It is not evidence of implementation.

## A. TOOLCHAIN / REPRODUCIBILITY
- [ ] Pin exact compiler, assembler, linker, objcopy, image-builder versions.
- [ ] Record compiler target triple and ABI.
- [ ] Verify reproducible archives/images across clean machines.
- [ ] Verify deterministic link ordering.
- [ ] Verify deterministic filesystem/image creation.
- [ ] Record source revision inside build artifacts.
- [ ] Generate SBOM for release artifacts.
- [ ] Record licenses for source and third-party components.
- [ ] Verify third-party license compatibility.
- [ ] Verify dependency provenance and hashes.
- [ ] Prevent accidental host-library linkage.
- [ ] Test build with network disabled after dependency acquisition.
- [ ] Keep generated files auditable.
- [ ] Verify release symbols correspond exactly to release binary.

## B. BOOT CHAIN / FIRMWARE EDGE CASES
- [ ] Test BIOS/legacy boot path if supported.
- [ ] Test UEFI path if supported.
- [ ] Define Secure Boot behavior.
- [ ] Define unsigned-image failure behavior.
- [ ] Test unusual memory maps.
- [ ] Test low-memory configurations.
- [ ] Test >4 GiB memory.
- [ ] Test large physical-address configurations.
- [ ] Test missing framebuffer.
- [ ] Test framebuffer formats other than the primary target.
- [ ] Test missing ACPI tables and malformed ACPI.
- [ ] Test APIC/x2APIC capability differences.
- [ ] Test systems with one CPU.
- [ ] Test systems where AP startup partially fails.
- [ ] Define watchdog behavior.
- [ ] Define boot timeout/recovery behavior.
- [ ] Preserve panic information across reboot when safe.
- [ ] Verify kernel image cannot overlap boot modules or firmware regions.

## C. CPU / ISA / RAS
- [ ] Define minimum x86-64 ISA baseline.
- [ ] Detect optional ISA extensions before use.
- [ ] Guard AVX/AVX2/other optional instructions.
- [ ] Define XSAVE/XCR0 policy.
- [ ] Test FPU state across threads.
- [ ] Test SIMD state across preemption.
- [ ] Define machine-check/error handling policy.
- [ ] Define corrected-error reporting.
- [ ] Define unrecoverable CPU error behavior.
- [ ] Detect unsupported CPU topology assumptions.
- [ ] Handle invariant-TSC absence if timekeeping depends on it.
- [ ] Validate APIC timer behavior across CPU power states.

## D. KERNEL LOCKING / MEMORY ORDERING
- [ ] Inventory every lock.
- [ ] Document lock rank/order.
- [ ] Detect lock-order inversion in debug builds.
- [ ] Define IRQ-safe locks.
- [ ] Define NMI-safe restrictions.
- [ ] Define sleepable vs atomic contexts.
- [ ] Verify no sleeping while holding forbidden spinlocks.
- [ ] Document memory-ordering requirements for atomics.
- [ ] Use acquire/release/seq-cst only where justified.
- [ ] Test weak-memory-order races on real SMP hardware where practical.
- [ ] Add lock contention metrics.
- [ ] Add deadlock detection/watchdog in diagnostic builds.

## E. RCU / RECLAMATION / DEFERRED WORK
- [ ] If RCU-like mechanisms exist, define grace-period semantics.
- [ ] Ensure readers cannot observe freed objects.
- [ ] Define deferred-free queues.
- [ ] Bound deferred work.
- [ ] Define workqueue concurrency.
- [ ] Define work cancellation semantics.
- [ ] Ensure worker teardown drains/cancels owned work.
- [ ] Test shutdown with pending callbacks.

## F. SYSCALL / ABI COMPATIBILITY
- [ ] Version public ABI where needed.
- [ ] Define structure padding/alignment.
- [ ] Define 32-bit integer width explicitly.
- [ ] Define time structure widths.
- [ ] Define endianness assumptions.
- [ ] Define reserved fields and forward compatibility.
- [ ] Reject unknown mandatory flags.
- [ ] Ignore unknown optional fields only where specified.
- [ ] Prevent kernel ABI leaks of uninitialized bytes.
- [ ] Zero padding before copying structures to user space.
- [ ] Verify syscall restart semantics consistently.

## G. PROCESS RESOURCE ACCOUNTING
- [ ] Define per-process CPU accounting.
- [ ] Define memory RSS/virtual-size accounting.
- [ ] Define file-descriptor accounting.
- [ ] Define IPC accounting.
- [ ] Define child-process resource accounting.
- [ ] Define quotas/limits where needed.
- [ ] Define runaway-process containment.
- [ ] Define per-user resource limits if multi-user mode is supported.
- [ ] Ensure accounting cannot overflow.

## H. CRASH DUMPS / PANIC FORENSICS
- [ ] Define panic reason codes.
- [ ] Include CPU/task/process identifiers.
- [ ] Include register state.
- [ ] Include stack trace where valid.
- [ ] Include scheduler state.
- [ ] Include memory allocator state.
- [ ] Include recent fault context.
- [ ] Validate crash data before parsing.
- [ ] Rate-limit repeated crash persistence.
- [ ] Define sensitive-data redaction.
- [ ] Define crash-dump retention.
- [ ] Test panic while locks/interrupts are held.
- [ ] Test panic after allocator corruption.
- [ ] Test panic from multiple CPUs.

## I. TIME / CALENDAR / LOCALE
- [ ] Separate monotonic time from wall clock.
- [ ] Define RTC initialization.
- [ ] Define timezone storage.
- [ ] Define daylight-saving rules if user-facing calendar support exists.
- [ ] Define NTP/time synchronization policy.
- [ ] Prevent clock adjustment from breaking timers.
- [ ] Define timestamp precision.
- [ ] Define locale fallback.
- [ ] Define Unicode normalization expectations.
- [ ] Test malformed UTF-8.
- [ ] Test RTL text.
- [ ] Test mixed-script text.
- [ ] Test font fallback.
- [ ] Test keyboard layouts.

## J. USER / SESSION / AUTHENTICATION
- [ ] Define user identity model.
- [ ] Define login/session lifecycle.
- [ ] Define session lock/unlock.
- [ ] Define credential storage boundary.
- [ ] Define password/key derivation if passwords exist.
- [ ] Rate-limit authentication attempts.
- [ ] Define recovery path for lost credentials.
- [ ] Define logout semantics.
- [ ] Revoke session capabilities on logout.
- [ ] Ensure background services cannot retain unauthorized user capabilities.
- [ ] Test fast-user-switching if supported.

## K. IPC SECURITY
- [ ] Authenticate IPC peer identity.
- [ ] Validate capability/handle ownership.
- [ ] Prevent confused-deputy behavior.
- [ ] Prevent handle reuse attacks.
- [ ] Bound message size.
- [ ] Bound queue depth.
- [ ] Validate shared-memory offsets/lengths.
- [ ] Define cross-user IPC policy.
- [ ] Define privileged-service exposure.
- [ ] Fuzz IPC parsers and state machines.

## L. FILESYSTEM SECURITY
- [ ] Define ownership and permission bits.
- [ ] Define ACL behavior if supported.
- [ ] Define symlink-following rules for privileged operations.
- [ ] Prevent symlink/path races in security-sensitive operations.
- [ ] Define special-file policy.
- [ ] Define device-node creation authority.
- [ ] Define mount-option security.
- [ ] Prevent untrusted filesystem metadata from causing kernel memory corruption.
- [ ] Fuzz path parsing and directory records.
- [ ] Test malicious filesystem images.

## M. STORAGE RELIABILITY
- [ ] Detect media errors.
- [ ] Distinguish transient from permanent I/O failures.
- [ ] Retry only operations safe to retry.
- [ ] Preserve write ordering across failures.
- [ ] Define flush timeout.
- [ ] Detect device disappearance.
- [ ] Define degraded behavior.
- [ ] Test full-disk behavior.
- [ ] Test metadata exhaustion separately from data exhaustion.
- [ ] Test extremely fragmented files.
- [ ] Test millions of directory entries where practical.
- [ ] Test large files.
- [ ] Test sparse files if supported.

## N. NETWORK SECURITY
- [ ] Define random-source requirements for network security.
- [ ] Define ephemeral-port allocation.
- [ ] Prevent port allocation races.
- [ ] Bound connection tables.
- [ ] Defend against SYN/resource exhaustion where relevant.
- [ ] Validate ICMP/ICMPv6 inputs.
- [ ] Define MTU/PMTU behavior.
- [ ] Define checksum-offload assumptions.
- [ ] Define hardware offload fallback.
- [ ] Define MAC/address-change handling.
- [ ] Define network namespace policy if supported.
- [ ] Ensure firewall rules cannot be bypassed by alternate paths.

## O. RANDOMNESS / ENTROPY
- [ ] Define early-boot entropy source.
- [ ] Do not expose cryptographic randomness before readiness.
- [ ] Mix multiple entropy sources where available.
- [ ] Define entropy failure behavior.
- [ ] Separate fast PRNG from cryptographic RNG.
- [ ] Test repeated boot randomness.
- [ ] Test VM entropy-starvation behavior.
- [ ] Protect RNG state from ordinary logs.

## P. GRAPHICS SECURITY / ROBUSTNESS
- [ ] Validate GPU command/resource handles.
- [ ] Validate buffer sizes.
- [ ] Validate shader/resource references.
- [ ] Prevent untrusted applications from mapping privileged GPU memory.
- [ ] Bound GPU queues.
- [ ] Handle hung GPU.
- [ ] Define GPU reset ownership.
- [ ] Recover compositor after GPU reset.
- [ ] Ensure one bad graphics client cannot corrupt another client's surface.
- [ ] Fuzz image/font/shader parsers exposed to untrusted content.

## Q. DISPLAY / INPUT UX DETAILS
- [ ] Define cursor ownership and hardware/software cursor fallback.
- [ ] Define keyboard layout switching.
- [ ] Define IME/input-method boundary if multilingual text entry is supported.
- [ ] Define touch support if hardware provides it.
- [ ] Define pointer acceleration policy.
- [ ] Define key-repeat timing.
- [ ] Define clipboard format limits.
- [ ] Define clipboard lifetime across application exit.
- [ ] Define selection ownership.
- [ ] Define focus restoration after app crash.
- [ ] Define modal-dialog accessibility.
- [ ] Define reduced-motion behavior.
- [ ] Define high-contrast behavior.
- [ ] Define scaling behavior at 1366x768 and external FHD.

## R. DESKTOP RELIABILITY
- [ ] Shell restart without reboot.
- [ ] Window manager restart/recovery.
- [ ] Compositor restart/recovery where architecture permits.
- [ ] Preserve unsaved user state only when explicitly supported.
- [ ] Prevent notification storms.
- [ ] Prevent runaway search indexing.
- [ ] Prevent runaway thumbnail generation.
- [ ] Bound thumbnail cache.
- [ ] Bound icon cache.
- [ ] Bound font/glyph cache.
- [ ] Handle inaccessible files gracefully.
- [ ] Handle removable-media disappearance gracefully.

## S. PACKAGE / APPLICATION SANDBOX
- [ ] Define package identity.
- [ ] Define version comparison.
- [ ] Define dependency constraints.
- [ ] Define optional dependencies.
- [ ] Define conflicts/replacements.
- [ ] Define per-app writable data directory.
- [ ] Define executable/resource separation.
- [ ] Define application permissions.
- [ ] Define uninstall cleanup.
- [ ] Define orphaned data policy.
- [ ] Define package cache garbage collection.
- [ ] Prevent package scripts from gaining unintended privilege.
- [ ] Define install-time resource limits.

## T. BROWSER / UNTRUSTED CONTENT
- [ ] Define origin identity.
- [ ] Define same-origin isolation.
- [ ] Define storage quotas.
- [ ] Define permission persistence.
- [ ] Define download quarantine.
- [ ] Define executable-download handling.
- [ ] Define popup/background limits.
- [ ] Define renderer process memory limits.
- [ ] Define crash-loop handling.
- [ ] Define browser update rollback.
- [ ] Test malformed HTML/CSS/JS/media/fonts/PDFs.
- [ ] Test renderer escape assumptions with sandbox tests.

## U. WINDOWS / ANDROID RUNTIME OPERATIONAL DETAILS
- [ ] Define runtime startup cost.
- [ ] Keep runtime dormant until workload starts.
- [ ] Define runtime process isolation.
- [ ] Define foreign-code memory permissions.
- [ ] Define syscall/API translation boundary.
- [ ] Define unsupported-call diagnostics.
- [ ] Define runtime shutdown cleanup.
- [ ] Define per-app resource limits.
- [ ] Define graphics translation cache.
- [ ] Define filesystem mapping security.
- [ ] Define network mapping security.
- [ ] Define runtime update compatibility.
- [ ] Build versioned compatibility matrices rather than generic claims.

## V. GAMING / PERFORMANCE ENGINEERING
- [ ] Define performance mode vs balanced/power-save policy.
- [ ] Define CPU frequency/governor interaction.
- [ ] Define GPU power/performance policy.
- [ ] Define background suppression scope.
- [ ] Define shader compilation scheduling.
- [ ] Define asset-cache eviction.
- [ ] Define frame-time percentile reporting (not only average FPS).
- [ ] Record 1%, 0.1% lows where meaningful.
- [ ] Record CPU/GPU utilization separately.
- [ ] Record thermal clock behavior.
- [ ] Record memory pressure and paging.
- [ ] Record load times separately from runtime FPS.
- [ ] Repeat tests after cold boot and warm cache.
- [ ] Keep benchmark configurations reproducible.

## W. ZERO AI SAFETY / AGENT ENGINEERING
- [ ] Define immutable system capabilities AI cannot grant itself.
- [ ] Separate planning from privileged execution.
- [ ] Require capability check at execution time, not only planning time.
- [ ] Re-check authorization after asynchronous waits.
- [ ] Define confirmation policy for destructive actions.
- [ ] Define transaction/rollback for multi-step system changes.
- [ ] Define action provenance.
- [ ] Define user-visible action history.
- [ ] Define secret redaction from model context.
- [ ] Define prompt-injection defenses for files/web/content.
- [ ] Treat external content as untrusted instructions.
- [ ] Define tool-call allowlists.
- [ ] Define network-domain restrictions where appropriate.
- [ ] Define maximum autonomous chain length.
- [ ] Define timeout/deadline.
- [ ] Define emergency stop.
- [ ] Test confused-deputy scenarios.
- [ ] Test malicious document asking AI to execute commands.
- [ ] Test malicious webpage asking AI to expose files.
- [ ] Test privilege-escalation prompts.
- [ ] Test rollback after partial action failure.

## X. PRIVACY
- [ ] Define data collection policy.
- [ ] Default to minimum telemetry.
- [ ] Make diagnostic export explicit.
- [ ] Redact personal paths/secrets where possible.
- [ ] Define browser history/cookie storage policy.
- [ ] Define AI conversation retention policy.
- [ ] Define AI context boundary.
- [ ] Prevent one user's private data from entering another user's context.
- [ ] Define deletion semantics.
- [ ] Test deleted data is not accidentally retained in indexes/caches/logs.

## Y. BACKUP / DATA PORTABILITY
- [ ] Define user-data backup format.
- [ ] Define configuration export.
- [ ] Define restore semantics.
- [ ] Define backup integrity verification.
- [ ] Define incremental backup behavior if supported.
- [ ] Define encryption/key handling.
- [ ] Test restore onto clean installation.
- [ ] Test restore after filesystem corruption.
- [ ] Never imply snapshot is a backup unless independently stored.

## Z. OBSERVABILITY / OPERATIONS
- [ ] Define health states for every long-lived service.
- [ ] Define startup dependency graph.
- [ ] Define restart policy.
- [ ] Define crash-loop backoff.
- [ ] Define watchdog policy.
- [ ] Define liveness/readiness diagnostics.
- [ ] Define resource leak detectors for debug builds.
- [ ] Define latency histograms where useful.
- [ ] Define trace sampling.
- [ ] Define persistent diagnostics size limit.
- [ ] Define support bundle format.
- [ ] Define reproducible bug-report capture.

## AA. TEST INFRASTRUCTURE
- [ ] Every test has a unique identifier.
- [ ] Every test declares prerequisites.
- [ ] Every test declares expected output.
- [ ] Every test declares timeout.
- [ ] Every test emits machine-readable result.
- [ ] Tests distinguish infrastructure failure from product failure.
- [ ] QEMU tests record command line.
- [ ] Hardware tests record machine identity/configuration.
- [ ] Fuzzers retain minimized crashing inputs.
- [ ] Regression corpus is versioned.
- [ ] Randomized tests record seeds.
- [ ] Flaky tests are quarantined with an owner and issue, never silently ignored.
- [ ] CI artifacts have retention policy.
- [ ] Certification tests cannot be bypassed by a documentation-only flag.

## AB. FORMAL INVARIANTS / PROOF-ORIENTED CHECKS
- [ ] Enumerate scheduler invariants.
- [ ] Enumerate allocator invariants.
- [ ] Enumerate page-table invariants.
- [ ] Enumerate reference-count invariants.
- [ ] Enumerate queue invariants.
- [ ] Enumerate filesystem transaction invariants.
- [ ] Enumerate IPC invariants.
- [ ] Assert cheap invariants in debug builds.
- [ ] Turn critical invariants into executable tests.
- [ ] Add model/state-machine tests for complex protocols.
- [ ] Document which properties are tested versus formally proven.

## AC. DOCUMENTATION CONSISTENCY
- [ ] No document claims a feature stronger than its evidence.
- [ ] Architecture and code agree.
- [ ] Hardware document lists tested devices.
- [ ] Roadmap reflects actual stage.
- [ ] Requirements remain separate from certification evidence.
- [ ] Performance numbers have workload/hardware/configuration.
- [ ] Compatibility claims have versioned matrices.
- [ ] Security limitations are explicit.
- [ ] Unsupported features are listed.
- [ ] Every certification item links to evidence.
- [ ] Retired designs are marked historical rather than silently rewritten.

## AD. RELEASE / SUPPLY-CHAIN SECURITY
- [ ] Release artifacts are signed.
- [ ] Signing keys are not stored in source control.
- [ ] CI signing is isolated.
- [ ] Release provenance is recorded.
- [ ] Artifact hashes are published.
- [ ] Build inputs are traceable.
- [ ] Compromised dependency response exists.
- [ ] Emergency update/revocation path exists.
- [ ] Rollback protection does not prevent emergency recovery.
- [ ] Recovery images are independently verified.

## AE. INSTALLER / FIRST BOOT
- [ ] Define disk partitioning policy.
- [ ] Define destructive-operation confirmation.
- [ ] Validate target disk identity.
- [ ] Define bootloader installation transaction.
- [ ] Define filesystem creation transaction.
- [ ] Define interrupted-install recovery.
- [ ] Define first-boot initialization.
- [ ] Ensure initialization is resumable/idempotent.
- [ ] Define default user creation.
- [ ] Define default security settings.
- [ ] Define first-boot resource usage.
- [ ] Do not index entire disk synchronously during first boot.

## AF. SHUTDOWN / REBOOT
- [ ] Define service shutdown ordering.
- [ ] Stop new work before draining.
- [ ] Drain/cancel timers and workers.
- [ ] Flush persistent state according to policy.
- [ ] Flush storage barriers where required.
- [ ] Close network services cleanly.
- [ ] Stop graphics/compositor safely.
- [ ] Stop AI actions and revoke capabilities.
- [ ] Ensure no user process can indefinitely block shutdown without policy.
- [ ] Define forced-shutdown escalation.
- [ ] Test reboot during active I/O.
- [ ] Test reboot during active IPC.
- [ ] Test reboot during active AI action.
- [ ] Test reboot during GPU activity.

## AG. FINAL GAP POLICY
- [ ] Before Stage 10, perform a repository-wide search for TODO/FIXME/stub/panic placeholders/unreachable placeholders and classify every result.
- [ ] Search for empty success paths and ignored return values in privileged code.
- [ ] Search for unchecked user-controlled lengths.
- [ ] Search for unchecked allocation results.
- [ ] Search for missing cleanup on every error branch.
- [ ] Search for lock acquisition without matching release.
- [ ] Search for reference increment without decrement.
- [ ] Search for timer registration without cancellation.
- [ ] Search for DMA mapping without unmapping.
- [ ] Search for device bind without teardown.
- [ ] Search for queue insertion without ownership transfer.
- [ ] Search for background loops without sleep/event wait.
- [ ] Search for busy-polling paths and justify every one.
- [ ] Search for debug-only behavior accidentally required by release.
- [ ] Search for feature flags that bypass security or validation.
- [ ] Search for claims in docs unsupported by tests.
- [ ] Re-run the entire evidence matrix after final integration.

## MASTER COMPLETION RULE
Nothing in this inventory becomes complete because it is written down. For each item: inspect code -> identify owner -> design contract -> implement -> negative test -> stress/fault test -> measure -> document evidence -> integrate -> re-run affected certification gates.


---

---

# ZEROOS UNIFIED PRODUCT + UI + HARDWARE EXPERIENCE SPECIFICATION
Version: 2.0 — Unified expansion of the consolidated master
Status: Binding product/design requirement; implementation status is tracked separately by evidence.

## 1. Canonical purpose

This section is the authoritative product-experience expansion for the consolidated master specification. It incorporates the complete desktop UI concept, Dynamic Capsule, system surfaces, lifecycle/resource rules, accessibility, time/RTC recovery behavior, and user-visible operating-system behavior discussed for ZEROOS.

A written requirement is not implementation evidence. Every item must eventually map to code, tests, measurements, and a certification record.

## 2. ZEROOS product identity

ZEROOS is a native x86-64 operating system with:
- a real kernel, scheduler, memory manager, storage, drivers, networking and userspace;
- an original desktop shell and interaction model;
- native applications;
- isolated Windows and Android compatibility environments;
- recovery, update, security and diagnostics;
- ZERO AI as an OS-specific, permission-brokered service;
- an event-driven resource model.

ZEROOS must not become a Linux skin, visual mockup, simulator, or compatibility-layer-only product.

The UI may use familiar desktop conventions for usability, but its visual language, shell composition, APIs and system architecture remain ZEROOS-owned.

## 3. Universal lifecycle/resource contract

All long-lived UI and system features must expose an explicit lifecycle. The preferred lifecycle is:

STOPPED -> DORMANT -> WARM -> ACTIVE -> THROTTLED -> SUSPENDED -> DORMANT/STOPPED

Definitions:
- STOPPED: no resident service state.
- DORMANT: minimal controller/state exists; no continuous workload.
- WARM: small cached state/resources retained for fast activation.
- ACTIVE: user-visible or requested work is executing.
- THROTTLED: work continues under constrained CPU/GPU/I/O/network/thermal budget.
- SUSPENDED: execution is frozen and resumable.
- STOPPED again when state no longer needs retention.

Hard rule:
Installed != loaded != running != active != maximum resource usage.

No feature may use a permanent polling loop merely because polling is easier to implement. Prefer interrupts, event queues, timers, file/device notifications, compositor damage events, IPC wakeups and explicit work scheduling.

Every resident service must declare:
- minimum resident memory;
- active memory ceiling;
- CPU policy;
- I/O priority;
- GPU budget if applicable;
- network wake policy;
- wake events;
- cache limits;
- suspend/unload conditions;
- crash/restart behavior;
- observability counters.

The engineering target is near-zero unnecessary idle work and bounded resource use, not literal zero resource consumption during active rendering, decoding, execution or transfer.

## 4. ZEROOS desktop shell

### 4.1 Global shell

The desktop consists of:
- ZERO Bar;
- Universal Search;
- Dynamic Capsule;
- workspace manager;
- window system/compositor;
- notifications;
- Quick Controls;
- system status;
- launcher;
- desktop/workspace surface;
- accessibility services;
- optional widgets;
- performance/resource indicators.

The shell must remain functional without ZERO AI.

### 4.2 ZERO Bar

ZERO Bar is the persistent adaptive shell surface.

It contains:
- ZERO launcher;
- search entry;
- active application indicators;
- workspace indicator;
- notification entry;
- network/device state;
- power/battery state;
- clock/calendar entry;
- optional user-configurable shortcuts.

Behavior:
- compress on small displays;
- adapt to DPI/scaling;
- hide or enter minimal mode in fullscreen applications;
- restore predictably after fullscreen;
- never become an input/focus trap;
- expose complete keyboard navigation;
- avoid continuous animation when nothing changes.

### 4.3 Launcher

Launcher requirements:
- keyboard-first activation;
- pointer/touch support where hardware exists;
- recent/favorite applications;
- installed application discovery;
- categorized application discovery;
- search integration;
- app lifecycle indicators;
- uninstall/settings actions where permitted;
- accessibility labels;
- offline operation.

The launcher must not index or render the entire application ecosystem continuously.

## 5. Dynamic Capsule

ZEROOS includes an original Dynamic Capsule: a compact, transient system-status surface near the top-center of the desktop.

It is an event-driven UI surface, not a permanently animated widget.

Supported event classes include:
- downloads/uploads;
- package installation/removal;
- OS update staging/activation;
- media playback;
- recording;
- microphone/camera activity;
- screen sharing;
- AI task progress;
- file copy/move;
- backup/snapshot activity;
- network transfer;
- device pairing;
- recovery operations;
- security alerts;
- RTC/time synchronization;
- important power/thermal events.

Collapsed state:
- minimal icon/status;
- bounded display lifetime;
- no continuous animation when idle.

Expanded state:
- title;
- task identity;
- progress/status;
- relevant controls;
- estimated information only when measured/available;
- cancel/pause/retry where supported;
- error/recovery state.

Lifecycle:
EVENT -> SHOW -> UPDATE ONLY ON STATE CHANGE -> AUTO-COLLAPSE -> DORMANT

Accessibility:
- screen-reader announcement policy;
- keyboard focus only when explicitly opened;
- reduced-motion mode;
- high-contrast mode;
- no information conveyed by color alone.

Resource rules:
- no polling-only progress implementation;
- no full-screen redraw solely for capsule changes;
- damage only the affected region;
- coalesce high-frequency progress updates;
- cap update frequency;
- release transient textures/surfaces after timeout.

## 6. Universal Search

One search surface must find:
- applications;
- files/folders;
- settings;
- devices;
- commands;
- help/documentation;
- diagnostics;
- recent activity;
- installed Windows/Android applications;
- optional ZERO AI results.

Pipeline:
INPUT -> NORMALIZE -> QUERY CLASSIFY -> INDEX LOOKUP -> LIVE SOURCES -> RANK -> PRESENT

Requirements:
- incremental indexing;
- event-driven filesystem updates;
- bounded queues;
- cancellation;
- stale-result invalidation;
- permission-aware filtering;
- per-user indexes;
- encrypted/private data boundary;
- offline operation;
- low-resource mode;
- indexing pause under pressure;
- no full-disk synchronous first-boot scan.

Search results must distinguish:
- local exact result;
- metadata result;
- system action;
- compatibility application;
- AI-generated explanation.

AI must never bypass search permissions.

## 7. Window system and workspaces

Window model:
- create;
- map/unmap;
- focus;
- move;
- resize;
- minimize;
- maximize;
- close;
- snap;
- tile;
- float;
- workspace assignment;
- monitor assignment;
- DPI/scaling state;
- accessibility state.

Focus model must be deterministic:
- one active keyboard focus owner per seat;
- explicit focus transfer;
- crash-safe focus restoration;
- modal surfaces cannot silently steal focus;
- focus must return sensibly after a child process exits.

Workspace model:
- create/destroy;
- rename;
- reorder;
- switch;
- move window;
- remember window placement where permitted;
- suspend inactive application workloads according to lifecycle policy.

## 8. Compositor

Compositor architecture must use:
- retained scene representation;
- damage tracking;
- occlusion culling;
- frame scheduling;
- buffer ownership/lifetime;
- synchronization;
- adaptive refresh/render policy;
- GPU accelerated path where supported;
- software/framebuffer fallback.

Do not continuously redraw an unchanged desktop.

When the screen is static:
- no animation loop;
- no unnecessary GPU submissions;
- no repeated full-screen composition.

When a window is hidden/covered:
- throttle or suspend rendering where semantics permit.

When an application enters fullscreen:
- reduce desktop background work;
- suspend unnecessary widgets/animations;
- preserve notification/security indicators required by policy.

When GPU acceleration is unavailable:
- use bounded software fallback;
- explicitly expose capability state;
- never claim acceleration without hardware/driver evidence.

## 9. Visual design system

### 9.1 Principles
- clarity before decoration;
- fast path before feature discovery;
- consistent interaction grammar;
- keyboard/pointer parity;
- adaptive complexity;
- near-zero idle work;
- recovery is visible;
- accessibility is foundational;
- user data is understandable and controllable;
- motion communicates state but never blocks action.

### 9.2 Surfaces
Use layered surfaces with restrained depth.

Avoid:
- permanent heavy blur;
- always-running particle effects;
- unnecessary transparency;
- continuously animated backgrounds;
- large hidden texture caches.

### 9.3 Motion
Every animation must define:
- trigger;
- duration range;
- cancellation;
- reduced-motion behavior;
- frame-rate behavior;
- resource budget.

Reduced-motion mode:
- removes decorative movement;
- replaces motion with state changes;
- prevents animation from becoming a functional dependency.

### 9.4 Themes
At minimum:
- light;
- dark;
- high contrast;
- reduced motion;
- user accent configuration.

Dynamic wallpaper is optional and must be resource-governed:
- stop when covered/hidden/locked/fullscreen;
- reduce FPS/resolution under pressure;
- release unused GPU resources;
- never compete with foreground applications.

## 10. Quick Controls / Control Center

Quick Controls exposes:
- Wi-Fi;
- Bluetooth;
- audio output/input;
- display brightness where supported;
- night light/color policy where supported;
- airplane/network isolation;
- performance profile;
- battery/power profile;
- screen recording;
- microphone/camera privacy state;
- notification mode;
- accessibility shortcuts;
- device connection state.

Each control must use the real service/driver API. UI toggles must not become fake state.

States:
NORMAL / LOADING / DISABLED / ERROR / OFFLINE / PERMISSION_DENIED / UNSUPPORTED / LOW_RESOURCE

## 11. Notifications

Notification service requirements:
- stable notification identity;
- grouping;
- priority;
- rate limiting;
- deduplication;
- expiration;
- action buttons;
- permission policy;
- quiet hours/DND;
- accessibility;
- persistence policy;
- crash-safe queue handling.

Prevent notification storms with:
- per-source rate limits;
- aggregation;
- backoff;
- duplicate suppression.

Critical security/recovery notifications may bypass ordinary grouping according to policy, but must remain auditable.

## 12. Settings

Settings are schema-driven and versioned.

Categories:
- System;
- Display;
- Sound;
- Network;
- Bluetooth;
- Devices;
- Power;
- Storage;
- Apps;
- Privacy;
- Security;
- Users;
- Accessibility;
- AI;
- Windows Apps;
- Android Apps;
- Developer;
- Updates & Recovery.

Every setting defines:
- key;
- type;
- default;
- validation;
- persistence;
- scope;
- permissions;
- migration;
- reset behavior;
- UI metadata;
- error state;
- capability/unsupported state.

Settings changes requiring elevated privilege must cross the real permission boundary.

## 13. File manager

ZERO Files requirements:
- hierarchical navigation;
- search;
- metadata;
- copy/move/rename/delete;
- progress;
- cancellation;
- conflict handling;
- removable media;
- permissions;
- hidden/system objects;
- thumbnails;
- archive integration where supported;
- drag/drop;
- recent locations;
- storage health indicators.

Large operations must:
- use bounded buffers;
- expose progress;
- support cancellation;
- avoid unbounded memory;
- respect I/O priority;
- throttle during foreground latency pressure.

Thumbnail generation:
- asynchronous;
- cached with bounds;
- canceled when no longer visible;
- disabled/reduced under resource pressure.

## 14. Terminal and developer experience

ZERO Terminal:
- pseudo-terminal service;
- process groups;
- resize;
- UTF-8;
- signal/event semantics;
- job control where supported;
- scrollback with bounded memory;
- copy/paste;
- accessibility;
- secure privilege escalation boundary.

Developer mode:
- diagnostics;
- tracing;
- logs;
- package inspection;
- syscall inspection where permitted;
- service lifecycle tools;
- performance counters;
- QEMU/VM development workflow;
- crash/support bundle generation.

Developer tools must not silently weaken production security.

## 15. Native applications

Initial native application family:
- ZERO Files;
- ZERO Terminal;
- Browser;
- Settings;
- Media Player;
- PDF viewer;
- Notes;
- Study Center;
- Calculator;
- Dictionary;
- Code Editor;
- Diagnostics;
- Performance Center.

All applications use a common lifecycle:
INSTALL -> STOPPED -> LAUNCH -> ACTIVE -> BACKGROUND -> DORMANT/WARM -> SUSPENDED -> EXIT

Every app defines:
- manifest;
- permissions;
- identity;
- storage scope;
- IPC endpoints;
- resource limits;
- lifecycle callbacks;
- crash policy;
- update compatibility;
- accessibility metadata;
- localization.

## 16. Media and recording surfaces

Media player:
- local files;
- playlists;
- subtitles;
- multiple audio tracks;
- playback speed;
- screenshots;
- hardware decode where supported;
- software fallback;
- A/V synchronization;
- codec capability reporting.

Recording:
- screen;
- window;
- audio;
- microphone;
- webcam where supported.

Privacy indicators must be visible whenever microphone/camera/screen capture is active.

Recording must:
- use bounded buffers;
- handle storage-full conditions;
- survive transient device changes;
- expose cancellation;
- flush metadata safely;
- not consume unlimited memory.

## 17. Gaming mode

Gaming mode is a resource-governance profile, not a claim of universal performance improvement.

Possible policy:
- foreground priority;
- reduced background indexing;
- notification suppression;
- adaptive compositor work;
- performance/thermal policy;
- recording controls;
- frame-time telemetry.

Metrics:
- average FPS;
- frame-time distribution;
- 1%/0.1% lows where meaningful;
- CPU utilization;
- GPU utilization;
- temperature;
- clocks;
- memory pressure;
- paging;
- load time.

No benchmark claim without hardware, workload, driver, resolution and configuration.

## 18. Study Center

Study workspace:
- PDF;
- notes;
- annotations;
- highlights;
- flashcards;
- quizzes;
- formulas;
- calculator;
- dictionary;
- focus timer;
- revision planner;
- OCR where supported;
- ZERO AI study assistant.

All AI-generated explanations are clearly distinguishable from source text.

## 19. Browser

Browser architecture:
- UI process;
- isolated renderer/content processes;
- network service;
- storage boundary;
- download service;
- permission service.

Required controls:
- origin identity;
- same-origin boundary;
- storage quotas;
- permission persistence;
- download quarantine;
- executable-download handling;
- popup/background limits;
- renderer resource limits;
- crash-loop handling.

Untrusted web content must never gain direct kernel or privileged-service access.

## 20. Windows compatibility environment

Windows compatibility remains:
- user-space;
- isolated;
- sandboxed;
- resource-governed;
- versioned.

Architecture boundaries:
PE/COFF -> loader -> API translation -> compatibility libraries -> ZEROOS user APIs -> native services

Unsupported APIs must return explicit diagnostics rather than silently pretending success.

Compatibility runtime lifecycle:
STOPPED -> DORMANT -> START ON DEMAND -> ACTIVE -> THROTTLED/SUSPENDED -> CLEAN SHUTDOWN

Runtime must not remain resident when no compatible workload is active.

## 21. Android runtime

Android support is an isolated runtime environment, loaded on demand.

Requirements:
- exact supported Android/AOSP baseline is version-pinned;
- runtime components are isolated;
- resource limits are explicit;
- filesystem mapping is controlled;
- network access is policy-controlled;
- graphics integration is capability-dependent;
- app lifecycle integrates with ZEROOS lifecycle;
- runtime shutdown releases resources.

No Android runtime dependency may be required for native ZEROOS operation.

## 22. ZERO AI

ZERO AI is an OS-native service, not a prerequisite for booting or basic desktop operation.

Architecture:
USER -> ZERO AI UI -> AI ORCHESTRATOR -> PERMISSION/CAPABILITY BROKER -> ZEROOS USER APIs -> SERVICES -> KERNEL

Core functions:
- system search;
- file operations;
- coding/project workflows;
- diagnostics;
- settings assistance;
- study;
- automation;
- troubleshooting;
- application launching;
- system explanation;
- optional voice/vision integrations.

AI action model:
PLAN -> AUTHORIZE -> EXECUTE -> OBSERVE -> VERIFY -> REPORT

Security:
- AI cannot grant itself capabilities;
- authorization is checked at execution time;
- destructive actions require confirmation unless a user policy explicitly permits them;
- external content is untrusted;
- prompt injection is treated as hostile input;
- action history is auditable;
- secrets are redacted;
- autonomous chains have limits/timeouts;
- emergency stop revokes pending capabilities;
- multi-step system changes should be transactional/rollback-capable.

ZERO AI must remain dormant when unused and must never be required merely to display the desktop.

## 23. Search/AI/privacy boundary

AI context is scoped:
- user;
- session;
- application;
- project;
- system diagnostics.

Private data must not cross user boundaries.

Search indexes and AI memory must define:
- retention;
- deletion;
- encryption;
- permissions;
- cache invalidation;
- reindex behavior.

## 24. RTC / CMOS / time recovery

### 24.1 Hardware truth

ZEROOS must distinguish:
- non-rechargeable RTC/CMOS coin cell;
- rechargeable RTC cell;
- RTC supercapacitor;
- motherboard-managed RTC charging;
- externally managed RTC power;
- unknown/unsupported hardware.

ZEROOS must never blindly drive a charging voltage/current into a generic CMOS cell.

A standard non-rechargeable coin cell must be treated as replaceable, not software-rechargeable.

### 24.2 RTC health service

Where firmware/chipset/hardware exposes sufficient telemetry, ZEROOS may report:
- RTC availability;
- RTC validity;
- low-voltage indication;
- clock drift indication;
- last synchronization;
- hardware capability;
- battery/backup source status.

Telemetry not exposed by hardware must be reported as UNKNOWN rather than guessed.

### 24.3 Safe recovery path

RTC invalid/low condition:
1. detect;
2. preserve monotonic time internally;
3. warn user;
4. obtain trusted wall-clock source when available;
5. validate time;
6. set system wall clock;
7. write RTC only through the platform-supported RTC interface;
8. record synchronization result;
9. expose status in Dynamic Capsule/notifications.

Trusted sources may include:
- network time service;
- user-confirmed time;
- paired-device time where explicitly trusted;
- other platform-provided time source.

No time-source claim is valid without capability/validation evidence.

### 24.4 Recharge support

If a future ZEROOS-certified motherboard exposes a documented rechargeable RTC power controller:
- firmware/hardware owns charging safety;
- ZEROOS reads capability/status;
- OS may request a documented safe charging state only through the platform API;
- voltage/current limits remain hardware-enforced;
- charge failures are reported;
- non-rechargeable cells are never charged by software.

### 24.5 UI

Power/clock settings show:
- RTC healthy;
- time synchronized;
- synchronization required;
- RTC capability unknown;
- hardware service unavailable.

No false "CMOS charging" indicator may be shown when the hardware cannot support it.

## 25. Clock architecture

Separate:
- monotonic time for timers/scheduling;
- wall-clock time for user-visible dates;
- RTC hardware time;
- synchronized network time.

Wall-clock changes must not invalidate monotonic timers.

Calendar/locale support must define:
- timezone;
- DST rules where relevant;
- timestamp precision;
- locale;
- Unicode;
- malformed UTF-8 handling;
- mixed-script rendering;
- font fallback.

## 26. Accessibility

Required foundation:
- keyboard navigation;
- screen-reader semantics;
- focus indicators;
- high contrast;
- reduced motion;
- scalable text;
- DPI-aware layout;
- Hindi/English localization;
- logical tab order;
- accessible dialogs;
- non-color-only state;
- captions/subtitles where media provides them;
- configurable input timing where appropriate.

Every UI surface must define:
NORMAL / LOADING / EMPTY / ERROR / OFFLINE / PERMISSION_DENIED / UNSUPPORTED / LOW_RESOURCE / REDUCED_MOTION

## 27. Localization

Initial languages:
- English;
- Hindi.

Requirements:
- UTF-8 throughout user-facing text;
- locale-aware formatting;
- font fallback;
- mixed Hindi/Latin rendering;
- keyboard layouts;
- pluralization;
- date/time formatting;
- translation key versioning;
- missing-translation fallback;
- no hard-coded user-facing strings in privileged service code where localization is expected.

## 28. Device integration

UI must represent real device state:
- displays;
- audio devices;
- network interfaces;
- Bluetooth;
- USB;
- storage;
- cameras;
- microphones;
- printers/scanners where supported.

Device state transitions:
DISCOVER -> MATCH -> PROBE -> RESOURCE ACQUIRE -> INITIALIZE -> REGISTER -> SERVE -> ERROR RECOVERY -> SUSPEND -> RESUME -> REMOVE -> CLEANUP

Unsupported devices must be explicitly reported.

## 29. Power and thermal UI

Power profiles:
- performance;
- balanced;
- power saver;
- thermal constrained.

The resource governor may:
- reduce background work;
- reduce animation;
- lower indexing rate;
- suspend inactive apps;
- reduce media quality only when policy permits;
- adjust scheduling priorities;
- coordinate CPU/GPU policies.

Foreground user intent must remain prioritized subject to safety/thermal limits.

Thermal UI must show:
- normal;
- warm;
- throttled;
- critical/recovery.

No fake temperature or battery values.

## 30. Hardware capability adaptation

ZEROOS must discover capabilities instead of assuming them.

Capability matrix covers:
- CPU ISA;
- RAM;
- display resolution/refresh;
- GPU acceleration;
- video decode;
- audio;
- network speed/features;
- storage type;
- ACPI power controls;
- RTC capabilities.

UI and workloads adapt:
- low-end hardware: minimal effects/software fallback;
- capable hardware: acceleration;
- thermal pressure: throttle background work;
- low memory: reclaim caches and suspend inactive workloads.

## 31. Low-resource desktop contract

When memory/CPU/GPU pressure crosses policy thresholds:
1. stop decorative animation;
2. stop hidden wallpaper rendering;
3. reduce thumbnail generation;
4. pause/reduce indexing;
5. suspend dormant compatibility runtimes;
6. reclaim caches;
7. throttle background downloads if policy permits;
8. reduce compositor work;
9. preserve foreground interaction;
10. expose diagnostics.

The UI must not silently become unusable merely because optional effects are disabled.

## 32. Crash/recovery UX

For every long-lived service:
- health state;
- startup failure;
- retry;
- backoff;
- restart;
- dependency failure;
- recovery path.

Shell recovery target:
- compositor crash must not imply kernel reboot;
- notification/search service crash should restart independently;
- AI crash must not affect desktop;
- compatibility runtime crash must not terminate native applications;
- app crash must return focus safely;
- recovery environment must work without the normal desktop.

## 33. UI state and data ownership

Every visible value has a source of truth:
UI -> service API -> authoritative state.

Do not maintain independent fake copies of:
- battery;
- Wi-Fi;
- Bluetooth;
- download progress;
- installation state;
- update state;
- RTC state;
- device availability;
- process state.

Transient UI caches must carry:
- source version/generation;
- timestamp if meaningful;
- invalidation rule.

## 34. Performance instrumentation

Measure:
- shell startup;
- compositor startup;
- first frame;
- steady-state frame time;
- input-to-frame latency;
- window open/close latency;
- search latency;
- notification latency;
- app launch latency;
- memory resident set;
- cache sizes;
- CPU wakeups;
- GPU submissions;
- power/thermal behavior.

Measurements must record:
- hardware;
- resolution;
- workload;
- build revision;
- configuration;
- warm/cold state;
- test duration.

No "zero overhead" or absolute latency claim without measurement.

## 35. UI test matrix

Each surface should have:
- unit tests for state machines;
- IPC/API tests;
- rendering/layout tests;
- accessibility tests;
- localization tests;
- low-resource tests;
- crash/restart tests;
- input/focus tests;
- multi-monitor tests where supported;
- QEMU tests;
- hardware tests where hardware is required.

Critical UI flows:
- boot -> login/session -> desktop;
- launch -> app -> close;
- search -> result -> action;
- notification -> action;
- download -> progress -> completion/failure;
- update -> restart/recovery;
- RTC invalid -> sync -> confirmed time;
- low-memory -> degraded UI -> recovery;
- compositor restart -> session recovery.

## 36. Security rules for UI

The UI must never be the security boundary.

Examples:
- hiding a button is not permission enforcement;
- disabling a menu item is not privilege enforcement;
- displaying "admin" is not granting admin rights;
- AI confirmation UI is not authorization by itself.

Authorization must occur in the underlying service/kernel boundary.

## 37. Master integration rule

The consolidated master is the canonical planning/design aggregation. Source documents remain useful for subsystem-local navigation and historical traceability, but any conflicting requirement must be resolved explicitly.

When a requirement changes:
1. update implementation contract;
2. update this master;
3. update subsystem document if applicable;
4. update roadmap/stage;
5. update tests/evidence;
6. record migration impact.

## 38. Feature acceptance template

For every future feature, record:

Feature ID
- Name
- User-visible behavior
- Owning subsystem
- API/ABI
- Dependencies
- Lifecycle
- CPU budget
- Memory budget
- I/O budget
- GPU budget
- Network policy
- Security boundary
- Privacy boundary
- Failure modes
- Recovery behavior
- Accessibility
- Localization
- Tests
- Stress/fault tests
- Hardware requirements
- Evidence artifact
- Status

## 39. UI implementation anti-patterns

Prohibited unless explicitly justified:
- permanent busy loops;
- unconditional 60/120 FPS animations;
- full-screen redraw for tiny state changes;
- unbounded notification queues;
- unbounded thumbnail caches;
- synchronous full-disk indexing;
- fake hardware telemetry;
- fake progress;
- UI-only security;
- always-resident Windows/Android runtimes;
- always-loaded AI models;
- silent privilege escalation;
- undocumented background network activity;
- "zero resource" claims without measurement.

## 40. Final product principle

ZEROOS should look feature-rich while remaining architecturally quiet when nothing needs to happen.

The intended experience is:
VISIBLE POWER + DORMANT BACKGROUND + EVENT-DRIVEN WORK + BOUNDED RESOURCES + REAL RECOVERY + MEASURABLE PERFORMANCE.

---


---

## Cross-Cutting Master Contract

See [`ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md`](./ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md) for the mandatory cross-cutting engineering contract and the expanded ZEROOS feature/platform catalog. Applicable requirements cover ownership/lifetime/concurrency, boot/firmware, hardware certification, networking, packages, SDK, observability, accounts, backup/recovery, privacy, supply-chain security, accessibility/i18n, virtualization, power-loss certification, performance/compatibility labs, ZERO AI safety, and additional product features. This is a specification link only; implementation status remains evidence-based.
