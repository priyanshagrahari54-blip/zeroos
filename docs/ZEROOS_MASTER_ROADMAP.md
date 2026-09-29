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


## Stage 10 actual execution status (2026-09-29)

**BLOCKED.** On source SHA `91e30eb7eae80b832f8daecdd1dee8e43424a2f9`, local `make check` and the ASan/UBSan host suites passed after fixing an E0-prefixed scancode table out-of-bounds bug. The exact-SHA GitHub Actions run 36610649336 passed its build, host gates, QEMU boot/SMP/NX-off and AHCI/NVMe persistence jobs. Local ISO creation could not run because `grub-mkrescue` is absent and package mirrors were unreachable; workflow dispatch was denied (HTTP 403), but a branch push triggered the passing run. No physical target, hardware profile, measured performance baseline, thermal soak or long-duration operational soak was available. This does not satisfy the Stage 10 exit evidence above. Gate-by-gate results are in `VALIDATION.md` and `STAGE_10_REPORT.md`; Stage 10 must not be marked complete.
