# ZEROOS MASTER ROADMAP

## Execution rule

Every milestone follows:
AUDIT -> DESIGN -> IMPLEMENT -> TEST -> STRESS -> MEASURE -> DOCUMENT -> INTEGRATE

Never create a basic version merely to replace it later.

## Current state

### Current main and release acceptance — 2026-11-10
- Code-bearing main merge commit is `100c8e2cf4cdd049c4bf5565b5eebbc0f5284651`; PR #44 is merged. ZJFS flushes ordered data to stable media before journal metadata/commit records and aborts read-only if that barrier fails. Torn journal-superblock recovery validates descriptor, commit record, target ranges and image CRC; valid transactions replay and incomplete slots are discarded safely.
- Main Build #1855 and Stage 1 Scheduler #276 passed. Evidence includes 3/3 QEMU boots with 106 milestones, four-vCPU SMP certification, scheduler panic-diagnostics fault certification, eight power-cut rounds (including a logged torn-JSB fallback), clean fsck after every round, AHCI/NVMe two-boot persistence, production ISO generation and production desktop/network smoke.
- The main Build published the boot-tested code snapshot despite later documentation-only commits. Versioned `zeroos-100c8e2.iso` and stable `zeroos.iso` are both 12,374,016 bytes and byte-identical at SHA-256 `4628a75807bb9a074ce48eaf8e627997fd86b9c33c63201d66dacc785b498e80`; the extracted production ISO independently has the same digest. Release notes identify `100c8e2` and specify a legacy BIOS El Torito live session, not an installer.
- The first candidate Build #1851 exposed torn-JSB recovery failure; descriptor/commit fallback was added and passed Build #1854, then main Build #1855. Preserve this failure/correction history.
- No P0 build/boot/stable-release blocker remains for this snapshot. Remaining P1/P2 gates: 600-second/four-vCPU scheduler soak; physical G560/device testing; physical power-loss recovery; USB/graphics/NIC drivers; clean ACPI S5 shutdown; disk installer if required; then desktop/browser/media/AI/compatibility work. Overall estimate remains ~43%.




### Current main and storage-durability candidate — 2026-11-10
- Main HEAD is `3f42def2a0695c53f6d9b6d37d807ce627feaae0`; PR #43's ordered-writeback error-abort correction is present. PR Build #1837 / Stage 1 #258 passed on tested head `fb1e973b0b41ebeb273c37350bd0bb0d785c1b09`; the main merge commit is one commit ahead with no changed-file delta. This is PR validation, not a verified post-merge main push workflow.
- Build #1837 completed native build/check and reproducible release gate, 3/3 QEMU boots with 105 milestones, two-boot AHCI/NVMe persistence, production ISO and production desktop/network smoke. PR-event release publication was skipped.
- The last release snapshot documented in the repository remains `1a57d5c`; stable ISO publication for `3f42def2a0695c53f6d9b6d37d807ce627feaae0` is not verified.
- Candidate `fix/zjfs-ordered-data-flush-barrier-20261110` adds a pre-journal device FLUSH after ordered data writeback. A failed barrier aborts before journal metadata; a RAM-disk FLUSH-EIO regression checks that the file remains absent after remount. Candidate CI/QEMU is pending.
- Remaining: 600-second/four-vCPU scheduler soak; physical G560/device matrix; physical power-loss recovery; USB/graphics/NIC certification; clean ACPI S5 shutdown. Overall estimate remains ~43%.


### Current main / release evidence — 2026-11-09
- Latest main HEAD documentation-only main successor is documentation-only; latest code-bearing main commit 1a57d5d (PR #42) passed main Build #1835 and Stage 1 Scheduler #256. The kernel-source baseline remains f20ea855.
- Build #1835 passed all configured regressions, native build/check, kernel-image verification, 3/3 QEMU boots with all 104 milestones, scheduler panic diagnostics, AHCI/NVMe two-boot persistence, production ISO, production desktop/network smoke, artifact retention and release publication.
- Retained serial-disk1.log and serial-disk2.log confirm both storage boots: SATA/NVMe discovery, driver tests, persistent /data counters 1 and 2, file check, Ring-3 fsync probe and Stage 3 completion; neither has a panic/failure marker.
- The versioned zeroos-1a57d5c.iso and stable zeroos.iso assets both have SHA-256 082bd06046aad31f8f9d2d39b20b5361b46d0a44cd39ddda26bff07a321e58cd. Release notes identify 1a57d5c. The post-merge P0 boot/release gate is closed for this code snapshot.
- The 600-second scheduler soak and physical Lenovo G560/device matrix remain unverified. Selectively reconcile stale PR #26/#27; do not merge their divergent branches wholesale.

### Historical evidence snapshot — 2026-10-26 (superseded by 2026-11-05 update)
- The normal `make` image targets a persistent native x86-64 kernel + Ring-3 desktop/session path; the release pipeline has a shared 91-milestone QEMU boot gate.
- PR #16 was merged as `d6b58292df7e00b4b2f15ba22f3a0405035ee549`, adding parallel-build dependency fixes, ISO structure validation/writing, reproducibility checks, shared boot-test logic, and local intent-parser foundations.
- **Fresh main CI evidence:** GitHub Actions run 1543 for `main` HEAD `5d89992d6c78a2a19108385c3fc4979fa3cfe19e` completed successfully on 2026-10-26: https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/40322921742. This is evidence that the configured CI build passed, not proof that every product feature is complete or that physical G560 hardware is certified.
- The PDF audit and adoption matrix is `ZEROOS_REFERENCE_PDF_AUDIT_AND_ADOPTION_MATRIX.md`. Two newly uploaded hardware PDFs still need content-level extraction/review, and all 220 optimization-catalogue entries need an explicit decision ledger. Audit documentation does **not** increase implementation percentages.
- `make check` is a broad gate, but stage scripts and source/ELF markers do not by themselves prove every end-user feature works. Run `tools/release.sh` from a clean checkout and retain its ISO/QEMU logs for reproducible release evidence.
- A bootable ISO requires the GRUB/El Torito toolchain. `ZEROOS_ISO_ALLOW_UNBOOTABLE=1` is explicitly not release evidence.
- ZEROOS is **not 100% complete**. Full dynamic linking, standalone disk installation, production browser/GPU/media, usable Windows/Android runtimes, full ZERO AI tool/backend execution, physical G560 support, recovery UX and performance/soak certification remain open.
- See `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md` for evidence-weighted estimates and the precise current blocker list.

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

## NEXT EXECUTION BATCH — CLOSE REAL BLOCKERS FIRST

Do not start by adding more feature documents or UI mockups. For each batch: inspect current source and tests, implement one coherent change, add regression/fault tests, run the exact local gate, inspect CI/QEMU logs, update evidence, and only then move the estimate.

1. Check main-branch CI after `d6b5829` and the documentation-only `a72c171d`; no status entries were returned for `a72c171d`, so do not assume it passed. Run the clean release gate and fix the first real failure.
2. Run `make -j check` and `tools/release.sh`; preserve the boot milestones and publish the serial/debug logs.
3. Add tests for exec failure cleanup: process creation, ELF load failure, stack allocation/map failure, and user-thread creation failure must release address-space pages and process slots exactly once.
4. Add Ring-3 guest tests for malformed user pointers, argv/envp boundary overflow, invalid ELF segment ranges, W^X, and unsupported PT_INTERP/PT_DYNAMIC rejection.
5. Stress IPC timeout/wakeup/close/revocation races and validate exact partial-write semantics.
6. Stress storage persistence across repeated QEMU boots and inject block I/O failures; verify journal replay/fsck and bounded writeback.
7. Test driver/network receive/transmit in QEMU with explicit hardware models; no claim based on parser-only tests.
8. Build the smallest usable native desktop slice: shell, launcher, Files, Terminal, Settings, input, fonts/text, error/restart paths.
9. Integrate mature licensed browser/media/runtime components only after ABI, driver, sandbox, and memory-budget constraints are documented and tested.
10. Implement ZERO AI tool adapters behind the permission broker: read-only first, then scoped write/execute with confirmation, cancellation, audit and rollback.
11. Implement signed update/package trust, independent recovery boot, snapshot/restore fault tests, and a real disk installer.
12. Run hardware certification on the G560 and publish actual RAM/CPU/I/O/boot/media/thermal measurements before release claims.

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

## Cross-Cutting Master Contract

See [`ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md`](./ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md) for the mandatory cross-cutting engineering contract and the expanded ZEROOS feature/platform catalog. Applicable requirements cover ownership/lifetime/concurrency, boot/firmware, hardware certification, networking, packages, SDK, observability, accounts, backup/recovery, privacy, supply-chain security, accessibility/i18n, virtualization, power-loss certification, performance/compatibility labs, ZERO AI safety, and additional product features. This is a specification link only; implementation status remains evidence-based.


## Live Implementation Snapshot — 2026-10-04
See `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md` for the authoritative current estimate and claim policy. Current estimate: **~44% overall**; S1 92%, S2 82%, S3 45%, S4 39%, S5 41%, S6 20%, S7 12%, S8 12%, S9 12%, S10 6%. Recent hardening includes H2/H3 certification/security work, exec pointer validation, timed-wait overflow/range validation, IPv6 firewall enforcement, and fail-closed rejection of unsupported ELF PT_INTERP/PT_DYNAMIC.

### Immediate execution order
1. Stage 2 process/thread/exec failure cleanup and lifecycle certification.
2. Stage 1 scheduler/SMP/QEMU evidence closure.
3. Stage 3 persistence/recovery correctness.
4. Stage 4 operational hardware/driver coverage.
5. Stage 5 live display/accelerated graphics path.
6. Stage 6 security/update/recovery enforcement.


## Live execution update — 2026-10-05
The current evidence-weighted implementation snapshot is **~44% overall**: S1 92%, S2 82%, S3 45%, S4 39%, S5 41%, S6 20%, S7 12%, S8 12%, S9 12%, S10 6%.

Recent hardening includes Stage 2 stack/ELF/timeout certification and ZERO AI action-capability separation with revocation-before-backend regression coverage. These changes improve correctness but do not by themselves certify the later stages.

### Rule for the lowest stages
Work proceeds from the lowest implementation stages first, but a stage percentage only increases when the real exit gate gains implementation + tests + integration evidence. Host-only modules, contracts, parsers or detection are not counted as a working OS feature until they are bound to the booted userspace/kernel path.

Immediate order: **S9 brokered actions/lifecycle → S8 isolated compatibility execution → S7 accelerated/media/browser integration → S6 kernel-enforced security/update/recovery → S10 real hardware certification**, while maintaining S1–S5 regression gates.

### Release pipeline integrity — 2026-10-24
- [x] Identify that a push-only event condition allowed feature-branch pushes to replace the mutable stable ISO release.
- [x] Candidate: gate stable release publishing on both push event and `refs/heads/main`.
- [x] Add `tools/test_release_publish_guard.py` and invoke it from CI.
- [x] Candidate workflow #1429 and documentation-synchronized head workflow #1435 passed, including the release guard, QEMU boot and storage gates.
- [ ] Merge through reviewed PR and run CI on the resulting `main` commit.
- [ ] Verify the release asset and recorded source commit match that successful `main` commit.
- [ ] Keep implementation estimates unchanged; this release-pipeline fix does not close a kernel or OS product stage.

### Release pipeline evidence update — 2026-10-24
- [x] Candidate workflow #1429 passed on `0f17ee1d8da6ea606bb43070faa6514000ed3a84`, including the policy regression test, clean `make check`, repeated QEMU boots, storage persistence, and production desktop/network smoke test.
- [x] Confirmed the release-upload step is skipped for pull-request events.
- [ ] Merge reviewed PR and verify CI on the resulting `main` commit.
- [ ] Verify stable release asset and source commit after the main workflow publishes.
- [ ] Run physical G560 certification separately; QEMU evidence does not establish hardware compatibility.


### Production boot diagnostics — 2026-10-24
- [x] Add unconditional, bounded-retention upload for production serial/QEMU/debug logs so failed smoke tests preserve evidence.
- [x] Add a regression test for artifact-step order, unconditional execution, missing-file tolerance, and QEMU log destinations.
- [ ] Obtain a green CI/QEMU workflow for the exact commit containing this change.
- [ ] If the smoke test fails, use the retained logs to identify and fix the actual boot milestone failure rather than guessing at a kernel root cause.
- [ ] Keep implementation estimates unchanged; diagnostics do not add kernel functionality.

### Workflow test invocation uniqueness — 2026-10-24
The release-policy regression script now also asserts that CI invokes it exactly once, preventing duplicate or missing guard checks. The production-diagnostic retention check remains separate; CI for the exact corrected commit is pending.

### Stable release artifact integrity — 2026-10-24
- [x] Main-only publishing guard and regression coverage are present on `main` (PR #19 merged; candidate CI reported passing).
- [x] Preserve production serial/QEMU/debug logs on smoke-test failure.
- [x] Add candidate post-upload download, ordered byte comparison of mutable `zeroos.iso` against the boot-tested production ISO, and regression assertions.
- [ ] Obtain green CI for this exact candidate, including QEMU, storage persistence, and production desktop/network gates.
- [ ] Merge the reviewed change, run the resulting `main` workflow, and verify the stable asset against the exact main-run ISO.

### Release integrity evidence update — 2026-10-25
- [x] Merge PR #20: verify stable `zeroos.iso` by downloading it after publication and byte-comparing it with the production ISO built and boot-tested in the same workflow. Merge commit: `34010187ef16b4a71b476a22c782cabf4558dc06`.
- [x] Candidate CI #1464 passed the regression, build/check, QEMU/SMP/NX/fault, AHCI/NVMe persistence, and production desktop/network smoke gates.
- [ ] Confirm the push-triggered workflow for the resulting `main` commit completes successfully.
- [ ] Verify the live release asset comparison ran successfully and the stable `zeroos.iso` matches the exact production ISO from that successful main workflow.
- Do not mark the release-integrity gate complete until both pending main-run checks have evidence.

### Stable release alias rollback — 2026-10-26 (candidate)
- [x] Verify the commit-specific ISO before updating the mutable stable alias.
- [x] Save the prior stable ISO and add rollback/cleanup paths for failed upload, download, or byte comparison.
- [x] Extend regression checks for publication guard, ordering, rollback branches, and shell syntax.
- [x] Obtain green CI for the exact candidate in workflow #1484 (run ID `40263294383`): build/check, QEMU boot certification, storage persistence, and production desktop/network gates passed; release publication was skipped on the PR event.
- [ ] Merge only after review; then verify the main-push publication and live stable asset against that run's boot-tested ISO.
- Do not close release-integrity acceptance until post-merge asset verification is evidenced.


## Full-feature completion contract — 2026-10-26

Canonical execution directive: [ZEROOS_ALL_FEATURES_IMPLEMENTATION_AND_BOOTABLE_RELEASE_CONTRACT.md](./ZEROOS_ALL_FEATURES_IMPLEMENTATION_AND_BOOTABLE_RELEASE_CONTRACT.md).

This directive preserves the full ZEROOS product inventory and requires all 220 entries from the two supplied optimization catalogs to remain individually traceable and dispositioned. It sets the required order: validate clean build/ISO/release first; stabilize boot, scheduler, process/IPC and storage correctness; complete driver/network and interactive desktop paths; implement independent security/update/recovery; integrate actual browser/media engines; then compatibility runtimes, ZERO AI tools and physical-hardware certification.

Do not increase stage percentages for this documentation change. Keep the existing evidence-weighted estimate until implementation, integration, behavior tests and release evidence justify a change. Linux-specific tuning knobs in the catalogs are candidate ideas, not commands to copy blindly into ZEROOS. Preserve data integrity, security mitigations, bounded memory, hardware detection and safe fallback behavior.


## Reference PDF audit — 2026-10-26

Audit report: [`ZEROOS_REFERENCE_PDF_AUDIT_AND_ADOPTION_MATRIX.md`](./ZEROOS_REFERENCE_PDF_AUDIT_AND_ADOPTION_MATRIX.md). The two optimization catalogues are reference ideas, not direct kernel patches. Keep Linux-only sysctls/distro tooling out of the native ZEROOS design unless an explicit compatible userspace component requires them. Prioritize clean build/ISO/QEMU evidence, scheduler/process/IPC correctness and storage integrity before broad upstream imports. Do not weaken durability, watchdog/recovery, privilege isolation, TLS/privacy or thermal safeguards for speed. Two newly uploaded hardware manuals are present in commit `5f0d1a3a012d1f397e9d5561f5081109ced1e521`, but their content-level review is pending. Build a 220-row item ledger; preserve source numbering and record each adopt/adapt/defer/reject/hardware-dependent decision with tests and evidence. Documentation changes do not increase implementation percentages.


## External reference audit — 2026-10-26

The six uploaded PDF files were reviewed as four distinct reference families because the 100-feature and 120-feature catalogs have duplicate copies. See [EXTERNAL_REFERENCE_AUDIT_2026-10-26.md](./EXTERNAL_REFERENCE_AUDIT_2026-10-26.md) for adopt/adapt/reject decisions and the hardware/security gates. No stage percentage changes from this audit alone; it adds a review and implementation order, not runtime code.


## Library research and reuse gate — 2026-10-26

The Library reference PDFs have been catalogued in `EXTERNAL_REFERENCE_AUDIT_2026-10-26.md`; reuse candidates and integration boundaries are in `LIBRARY_REUSE_AND_INTEGRATION_PLAN.md`. Do not copy a PDF recommendation or external archive directly into the kernel. For each candidate, inspect the exact source and current ZEROOS call path, pin upstream revision, record file-level license/provenance, implement a bounded adapter, add negative/fault/fuzz tests, run the relevant QEMU/boot gate, and measure before/after RAM/CPU/idle wakeups/I/O/latency.

**Reuse order:** (1) current-main build/ISO/QEMU release gate; (2) process/exec/timer/IPC/memory lifecycle correctness; (3) storage durability/recovery; (4) native session path; (5) small user-space libraries (compression, fonts/shaping, libc/runtime) behind stable APIs; (6) network/TLS/browser/media; (7) isolated compatibility runtimes and ZERO AI. Candidate shortlist is not an import log. SQLite is for user-space metadata/indexes only, never a kernel dependency. LZ4's library and command-line portions have different licensing. FreeType/HarfBuzz are userspace text components. lwIP is evaluated against existing ZEROOS networking before deciding whether any portion is useful.

**Unreviewed artifacts:** the two hardware/kernel PDFs whose full text is not yet extracted and the contents of RECOVER-X/G560 ZIPs must be inventoried before their code or prescriptions can be adopted. Their presence in the Library is not evidence of compatibility, quality, or license clearance.

### Release metadata integrity — 2026-10-27
- [x] Identify that final success notes are edited before mutable stable ISO byte verification.
- [x] Candidate patch uses pending-verification notes for newly created releases and defers final notes until stable ISO comparison succeeds.
- [x] Add regression assertions for pending notes and final-note ordering.
- [ ] Obtain green CI for the exact candidate, including build, `make check`, QEMU boot, storage persistence and production desktop/network smoke.
- [ ] Merge through review, then verify the push-triggered `main` workflow and live `zeroos.iso` bytes/notes against the exact run.
- This is release-integrity work only; it does not add kernel/desktop functionality or increase the overall implementation estimate.

## Evidence update — 2026-10-27
- Merged PR #24: release notes are now deferred until after stable `zeroos.iso` byte-verification; newly created releases use an explicit pending-verification note. Merge commit: `1cd514b641d148b4094c0de524925dbd323b92eb`.
- Candidate workflow #1553 passed the build, clean check, QEMU boot, scheduler fault, AHCI/NVMe persistence, production ISO and desktop/network smoke gates. Actual release publication was skipped on the PR event.
- Next release gate: inspect the post-merge `main` workflow and verify the live stable ISO against that run's production ISO. No OS completion increase claimed.


## Execution update — 2026-10-28: fail-closed boot evidence

**Highest-priority current task:** prevent QEMU boot certification from accepting an unexpected emulator exit or stale serial output.

- Candidate branch: `fix/boot-gate-fail-closed-20261028`.
- Shared `tools/boot_test.sh` now clears per-iteration logs, requires QEMU timeout status 124, fails on missing serial output, and requires both boot-entry and long-mode debug markers for every iteration.
- `.github/workflows/build.yml` applies explicit status checks to 4-vCPU SMP, NX-disabled, failed-IPI recovery, scheduler panic, storage persistence and production desktop/network boots. Regression coverage is in `tools/test_boot_gate_fail_closed.py`.
- Current main HEAD `7c45183332f895ad778f08b98458f580b18a0067` has no returned CI result. Workflow #40491492164 passed for older SHA `b1c848f193940bfd18691f942daf6e32d0dae210` and verified its release ISO; that evidence is not transferred to current HEAD.
- **Gate before merge:** exact candidate workflow must pass the new regression, clean build/check, QEMU boot matrix, storage persistence, production smoke and release guard. The fix strengthens test evidence; it does not earn kernel feature-completion credit. Hardware/soak and the remaining Stage 1 scheduler certification work remain open.

## Execution update — 2026-10-28: affinity update during CPU hot-offline

**Priority:** kernel scheduler correctness before broader desktop/browser/media work.

- Main baseline: `731e9e8856976beb2e717eef169ded72f4917741`; exact-head CI status is not available from the current connector lookup.
- Candidate branch `fix/scheduler-affinity-offline-race-20261028` revalidates an affinity mask under `task_lock` before mutation. This prevents a RUNNING task from accepting a mask whose only target has already published a CPU-offline request while its online flag is still true.
- Regression evidence added: pure offline-mask policy self-test, serial marker required by shared QEMU boot milestones and Stage 1 certification, and `tools/test_task_affinity_offline_race.py` wired into CI.
- Exact candidate head `8fe3ee0a97788b762e990ef253238eb51c49b008` passed ZEROOS Build workflow #1610 and Stage 1 Scheduler CI #31, including build, clean `make check`, repeated QEMU boot matrix, scheduler panic diagnostics, two-boot AHCI/NVMe persistence, production ISO and desktop/network smoke. Release publication was skipped on the PR event. Next gate: review/merge the candidate, then verify the post-merge `main` workflow and live stable ISO. The 600-second Stage 1 soak and physical hardware matrix remain open. No completion percentage increase.

## Priority update — 2026-10-28: running-task affinity owner

- Next kernel correctness item: ensure a remote affinity update requests rescheduling based on the CPU actually executing the RUNNING task, not the CPU issuing the setter. Candidate branch `fix/affinity-remote-owner-reschedule-20261028` adds an owner lookup protected by `task_lock`, a fail-closed consistency check, and regression coverage.
- Exact baseline `main` SHA `218cbc43eaeba24612a91838ee381460aa2212e8` has no returned status entries from the connected lookup; current-main CI is unverified. Candidate SHA `f6450de619a4ea052b84d9394fe0d4dd58d98236` passed Build workflow #1626 and Stage 1 Scheduler CI #47, including clean `make check`, QEMU boot matrix, storage persistence, and production desktop/network smoke. The fix remains unmerged; post-merge `main` workflow and live stable ISO verification remain open.
- After candidate checks pass: review/merge, verify the post-merge main workflow, then re-check release ISO integrity. The Stage 1 600-second soak and physical-hardware certification remain open. No completion percentage increase.

## Post-merge roadmap evidence — 2026-10-28

- Merged PR #30 at `acae3eba7b2f9c6cf2c52038d7b5606c28f4a077`: affinity updates now test the actual running task owner CPU when deciding whether to set `need_resched`.
- Candidate Build workflow #1626 and final PR-tip Build workflow #1632 passed build, clean `make check`, QEMU boot matrix, scheduler fault, two-boot AHCI/NVMe persistence, production ISO and desktop/network smoke. Stage 1 Scheduler CI #47 and #53 passed. PR runs skipped actual release publication.
- Next gates: verify the post-merge `main` workflow and live stable ISO, complete the optional 600-second Stage 1 soak, and gather physical-hardware evidence. Overall completion estimate unchanged.

## Kernel lifecycle follow-up — 2026-10-29 (candidate, unverified)
- [ ] Validate and merge process-parent provenance guard: reject pointers outside/alignment-incompatible with the process table before any dereference; retain a boot-time bad-pointer regression probe and source-order CI test.
- [ ] Run the exact candidate workflow: regression scripts, clean `make check`, QEMU boot milestones, SMP/feature variants, storage persistence, production ISO and desktop/network smoke tests.
- [ ] After merge, verify the resulting `main` workflow and live stable ISO separately. This patch does not close browser/media/AI, physical-hardware, or recovery milestones and does not change the overall completion estimate.

## Kernel lifecycle follow-up — evidence update 2026-10-29
- Candidate code head `f767841d9e6dd98df07005d44ebaf086472512e0` passed Build workflow #1651 and Stage 1 Scheduler CI #72, including invalid-parent regression, clean `make check`, QEMU boot milestones, scheduler fault, two-boot AHCI/NVMe persistence, production ISO and desktop/network smoke. Release publication was skipped for the PR event.
- [ ] Re-run required CI for the final documentation tip; review and merge PR #31 only when those checks pass.
- [ ] Verify post-merge `main` workflow and live stable ISO against the exact production image. Stage 1 long soak and physical-hardware certification remain open; no completion estimate increase.

### Evidence snapshot — 2026-10-29
- Latest main commit observed: `b99e5705fa56558637b1983f5f45992d7ae509cb`; exact-main CI remains unverified by the available status lookup.
- Next code-first hardening candidate: validate process-pointer provenance before any field dereference across public process lifecycle, wait, limits, address-space and counter APIs. Regression and boot-time negative probe are wired in branch `fix/process-api-pointer-provenance-20261029`; candidate CI is pending.
- Required sequence: candidate regression -> clean `make check` -> repeated QEMU boot milestones -> scheduler/fault gates -> AHCI/NVMe persistence -> production desktop/network smoke -> merge review -> post-merge main workflow -> live stable ISO comparison. Do not treat a PR pass as a post-merge release pass.
- Physical G560 certification, Stage 1 soak, recovery UX, production browser/media, Windows/Android runtimes and full ZERO AI backend remain separate roadmap work. Completion estimate unchanged.

## Process lifecycle hardening — merged evidence, 2026-10-30

- [x] Reject invalid `process_create(parent, ...)` pointers before parent-field dereference (main commit `b99e5705fa56558637b1983f5f45992d7ae509cb`; candidate Build #1651 passed).
- [x] Extend process-table pointer provenance checks across 20 public process lifecycle, child wait/query, thread reservation, resource-limit, address-space and counter APIs. PR #33 merged as `4e3691c64079d6a50f3a0cf5230336b49dfb0bee`.
- [x] Add source-order regression coverage and a boot-time invalid-pointer certification probe. Candidate Build workflow #1677 and Stage 1 Scheduler workflow run ID `40813814986` passed; QEMU boot, scheduler fault, AHCI/NVMe two-boot persistence, production ISO and desktop/network smoke gates passed.
- [ ] Verify the push-triggered workflow for the exact post-merge `main` SHA and compare the live stable `zeroos.iso` byte-for-byte with that run's production ISO. The PR run skipped GitHub Release publication, and the connected post-merge status lookup returned no entries.
- [ ] Complete the optional 600-second Stage 1 soak and physical-hardware/device matrix (including Lenovo G560 where feasible). Later recovery, usable desktop breadth, browser/media/AI and compatibility milestones remain separate requirements; this merge does not close them.
- Overall OS completion estimate unchanged (~43%).


## Execution update — 2026-10-31

1. **Current main baseline:** `9f7b715ee9c15506514c4e563ea78801d9c77e9d`. No combined CI status entries were returned for this exact SHA; do not infer a pass from the successful PR candidate workflow #1686.
2. **Highest-priority candidate fix:** serialize user-memory range validation, page translation, and copy under `process_lock`. The previous syscall and exec helpers had a TOCTOU window between validation and physical-page access. Candidate branch `fix/atomic-process-user-copy-20261031` adds process-owned copy APIs, routes syscall/exec paths through them, and adds cross-page/unmapped-tail guest checks plus a source regression test.
3. **Candidate validation is pending:** Python regression, clean `make check`, repeated QEMU, SMP/NX/fault boots, storage persistence, production ISO and desktop/network smoke must pass on the exact PR head before merge. No progress estimate change.
4. **Release/Stage 1 blockers:** verify post-merge main CI and live stable ISO identity; execute the 600-second scheduler soak; certify physical G560/device behavior. Open PRs #26 and #27 need rebase/review against current main before any integration claim.
5. Continue the full product sequence unchanged: process/runtime and IPC, storage integrity/recovery, drivers/security, usable native desktop, then browser/media/AI/Windows/Android compatibility and performance. Documented plans and research catalogues do not count as implemented features.

## Evidence update — 2026-11-01: process user-copy serialization

- Merged PR #35: process-owned user-copy operations now hold `process_lock` across whole-range validation, translation and bounded copy, preventing concurrent process unmap/remap/destroy from invalidating a page during syscall/exec copies. Main merge SHA: `6521dcf3e2b7d0caef25cb47515b8572f0358fcc`.
- Candidate Build #1702 and Stage 1 Scheduler #123 passed. Verified gates include the new five-test source regression, native build, clean `make check`, repeated QEMU boot/SMP4/NX-off/failed-IPI, scheduler fault diagnostics, two-boot AHCI/NVMe persistence, production ISO build and desktop/network smoke.
- Next release/acceptance gates remain ordered ahead of lower-priority product work: post-merge main workflow; live stable ISO byte comparison and final release-note confirmation; 600-second scheduler soak; physical G560/device certification. Reconcile PR #14 overlap and rebase/review #26/#27 before integration. Browser/media/AI/compatibility requirements remain preserved in later roadmap stages.
- No overall completion estimate increase.


## Evidence update — 2026-11-03: P0 stable release ordering

- Latest main `28112603` passed Build #41265715805 and Stage 1 Scheduler #41265715839, including repeated QEMU, SMP4/NX-off/failed-IPI, scheduler diagnostics, two-boot AHCI/NVMe, production ISO, and desktop/network smoke.
- Release publication is not currently aligned with latest main: older run #41265707574 for `3ba0dbe` finished after the latest-main run and left stable `zeroos.iso` at digest `c80f4270...`; latest-main `zeroos-2811260.iso` is `eea7d77...`, and release notes name the older commit. Treat this as a P0 release blocker despite both individual workflows reporting success.
- Candidate `fix/serialize-stable-release-publish-20261103` serializes build/release runs per ref without cancelling active rollback operations and skips stable-alias mutation when `GITHUB_SHA` is no longer current `main`. Regression assertions were added. Merge only after candidate CI passes; then run a fresh main workflow and verify the live stable asset and notes against that exact build.
- Next priorities remain: close the stable-release race; complete the separate Ring-3 GS ownership fix (PR #37) and scheduler soak; then storage recovery, drivers/security, native desktop integration, browser/media/AI/compatibility. Physical G560 certification remains separate. No completion estimate increase.


## Follow-up roadmap evidence — 2026-11-03

1. **P0 release integrity:** PR #38's release-policy and build checks passed, but full QEMU remains in progress. Merge only after all candidate gates pass, then verify a fresh main release run fixes the stable ISO and release notes to its exact commit.
2. **P0 kernel entry correctness:** PR #37's full CI passed, but `context_switch_ex` in `kernel/context.S` has a direct frame `iretq` path without GS restoration. Correct and regression-test that path before merging PR #37.
3. **P1 networking:** current QEMU serial logs explicitly report no supported network device; production smoke success does not certify a NIC driver.
4. Continue the 600-second scheduler soak, physical G560/device certification, storage recovery, drivers/security, native desktop hardware integration, then browser/media/AI/compatibility.

No overall completion increase is claimed.

## Final evidence update — 2026-11-04: release ordering blocker closed

- **P0 release ordering:** PR #38 merged as `84f8a5cf23e03098419d92231a25908174e3c21f`. Per-ref workflow concurrency (`cancel-in-progress: false`) and a stale-main SHA guard protect the mutable release alias and final notes. Candidate and main CI passed.
- **Post-merge acceptance:** Build #41766774251 and Stage 1 Scheduler #41766774267 both succeeded for `84f8a5cf...`. The Build completed clean `make check`, repeated QEMU boot, SMP4, NX-disabled, failed-IPI recovery, scheduler fault diagnostics, two-boot AHCI/NVMe persistence, production ISO and production desktop/network smoke.
- **Release proof:** The main workflow downloaded and byte-compared `zeroos.iso` with the boot-tested production image and logged SHA-256 `297ac5218d9c4e9b4b59cb85638a087fb2633a569c91bd40b8dc23e0e7a6016e`. Release API metadata gives the same digest for versioned `zeroos-84f8a5c.iso` and stable `zeroos.iso`; notes identify `84f8a5c`. The earlier out-of-order stable-pointer defect is closed for this release.
- **Next P0 kernel review:** PR #37's GS ownership fix passed its prior Build #1740/Stage 1 #161, but source review identified a direct `context_switch_ex` frame-resume `iretq` path in `kernel/context.S` that still needs GS restoration coverage. Correct and regression-test that path before merging PR #37.
- Continue PR #36 range-unmap review, Stage 1 600-second soak, physical G560/device certification, storage recovery, drivers/security, native desktop hardware integration, then browser/media/AI/compatibility. QEMU RTL8139 DHCP evidence is not physical NIC certification. No overall completion percentage increase.

## Latest release evidence — 2026-11-04

- The current documentation-tip main snapshot `9d77ad3` passed Build #41767825881 and Stage 1 Scheduler #41767825887.
- The Build passed clean `make check`, native kernel/ISO validation, repeated QEMU boot variants, scheduler fault certification, two-boot AHCI/NVMe persistence, production ISO, production boot smoke and release publication.
- Stable `zeroos.iso` and versioned `zeroos-9d77ad3.iso` are verified identical by the workflow's byte comparison and both report SHA-256 `cebb728732f1cd04e2058182842e53c5d988cc0463fc7bb2ac4b8217314da413`. Release notes identify `9d77ad3`.
- PR #38's release-ordering defect is closed for this published image. Remaining priorities: PR #37's GS restoration gap in the direct interrupt-frame resume path; PR #36 range-unmap validation; 600-second scheduler soak; physical G560/device matrix; storage recovery; driver/security; desktop hardware integration; browser/media/AI/compatibility. QEMU RTL8139 DHCP does not equal physical NIC certification.
- No overall completion percentage increase. This final documentation-only commit skips CI; it does not modify the tested OS image.

## Evidence update — 2026-11-05: complete GS ownership on direct frame resume (candidate)

- Current `main` HEAD is `49eb15d2aad3e96bb67efe9808c25ac40937e756`, a documentation-only `[skip ci]` commit. The latest OS-bearing snapshot is `9d77ad3befb6c782b548020d67d6af38401533b2`; Build #1748 / run `41767825881` and Stage 1 Scheduler #169 / run `41767825887` both completed successfully.
- Build #41767825881 passed existing source regressions, native x86-64 build, clean `make check`, kernel/ISO verification, repeated QEMU boot, SMP4, NX-disabled and failed-IPI boots, scheduler fault diagnostics, two-boot AHCI/NVMe persistence, production ISO, production desktop/network smoke, diagnostic preservation and GitHub Release publication.
- Release API metadata reports versioned `zeroos-9d77ad3.iso` and stable `zeroos.iso` with the same SHA-256 `cebb728732f1cd04e2058182842e53c5d988cc0463fc7bb2ac4b8217314da413` and size 12,369,920 bytes. The main Build log confirms the stable asset was downloaded and byte-compared with the production ISO; release notes identify `9d77ad3`.
- Existing PR #37 candidate `cb92e9b9aa944bfaaa5be8dbce48800bc193494e` passed Build #41573450851 and Stage 1 Scheduler #41573450849. It establishes GS ownership in `boot/isr.S:isr_common`, but source review found that its separate cooperative `kernel/context.S:.Lresume_frame` path can also resume a saved Ring-3 interrupt frame via `iretq` without selecting the target GS state.
- Candidate branch `fix/complete-gs-frame-resume-20261105` ports the reviewed GS entry/return changes onto current main and adds `cpu_interrupt_gs_prepare_frame()`: it verifies that kernel per-CPU GS is active and selects user GS only for a Ring-3 target immediately before the direct frame restore. No C call follows the Ring-3 `SWAPGS`.
- Added source regressions for the direct `.Lresume_frame` call order and fail-closed target-CPL selection; the Build workflow runs `python3 tools/test_interrupt_gs_entry.py` before toolchain installation. The exact new candidate head has not yet completed CI/QEMU; no new boot pass is claimed.
- PR #36 range-unmap preflight remains a separate unmerged candidate. The 600-second scheduler soak and physical Lenovo G560/device matrix remain outstanding. QEMU RTL8139/DHCP evidence does not certify a physical NIC. Browser/media/AI/compatibility remain roadmap work.
- Overall completion estimate is unchanged; source assertions and documentation do not earn feature-completion credit.


## Candidate validation correction — 2026-11-05

- First exact-head Build #41960677907 stopped at `Verify Ring-3 interrupt GS ownership`: the inherited regression expected `timeout --kill-after=5s`, but current-main `tools/boot_test.sh` used plain `timeout`. This was a test/harness contract mismatch, not a compiler or guest-kernel failure; build and all later QEMU steps were skipped in that run.
- The candidate now ports the bounded QEMU termination behavior into `tools/boot_test.sh`: `timeout --kill-after=5s` and explicit acceptance of timeout status 124 or forced-kill status 137. This prevents a paused `-no-shutdown` QEMU process from hanging the boot gate and makes the existing regression match the actual harness.
- The updated exact-head Build/QEMU run must still complete. No native build, QEMU boot, storage or production-smoke pass is inferred from the failed first run.


## Candidate validation correction — second CI attempt (2026-11-05)

- Build #41960835677 stopped in `Verify QEMU boot gate fails closed`, before native build or QEMU. The newly bounded timeout command added `--kill-after=5s`, but the test's fake `timeout` executable treated that option as the duration and attempted to execute `1s`; the regression then saw the wrapper's traceback instead of the expected exit diagnostic.
- Updated `tools/test_boot_gate_fail_closed.py` to strip the optional `--kill-after=...` argument before invoking its fake child, match the revised 124/137 diagnostic, and add a case proving forced-kill status 137 is accepted only as a timeout outcome while boot milestones are still mandatory.
- The next exact-head Build must rerun both the fail-closed boot-gate suite and the GS ownership suite before the compiler/build/QEMU stages. No kernel build or QEMU result is claimed for Build #41960835677.

## Evidence snapshot and next boot-priority fix — 2026-11-05

- Latest reviewed `main`: `c3c4eb6ba1a508ff8258d4cf96864317de2da6b4`. The GS frame-resume correction and process range-unmap preflight correction are included in this baseline.
- Candidate Build #41963710252 passed its native build, clean `make check`, repeated QEMU boot, scheduler fault diagnostics, AHCI/NVMe two-boot persistence, production ISO generation, and production desktop/network smoke steps. Stage 1 Scheduler #41963710086 passed. Both are candidate workflow evidence; the PR-event release publication step was skipped.
- The post-merge main workflow and stable ISO byte identity for `c3c4eb6ba1a508ff8258d4cf96864317de2da6b4` remain unverified. Close these release gates separately; do not infer them from PR CI.
- P0 boot fix in candidate branch `fix/multiboot-info-window-20261105`: remove the incorrect 16 MiB start-address ceiling on the Multiboot2 information block. Validate 8-byte alignment and the 1 GiB boot identity map; apply the 16 MiB ceiling to total block size; reject a block that crosses the identity map; call `reserve_range()` unconditionally so only the portion inside the allocator's 512 MiB window is reserved. Add a runtime boundary self-test, host/source regression, Build workflow step, and required boot milestone.
- Candidate code is implemented but its new CI/QEMU run is pending. Merge only after the regression, native build, clean checks, repeated QEMU boot, storage, and production desktop gates pass.
- Next after this fix: verify post-merge main CI and stable ISO; rebase/reconcile PR #26 (6 ahead/30 behind) boot-test integrity and PR #27 (1 ahead/29 behind) scheduler certification work; review PR #14 (19 ahead/321 behind) as a separate broad integration queue; run the 600-second scheduler soak; then continue process/runtime, storage recovery, drivers/security, native desktop hardware integration, browser/media, AI, and Windows/Android compatibility.
- Physical Lenovo G560 and device-matrix certification remain separate from QEMU. QEMU RTL8139/DHCP is not physical NIC evidence.
- Preserve the complete canonical product requirements and all Stage 0-10 workstreams. Overall implementation estimate remains unchanged.

## PR #40 validation correction — 2026-11-05

- First Build #1770 stopped at the new Multiboot2 range regression before compiler installation, clean `make check`, or QEMU. The source test expected a 17-byte total-size rejection that the first version of the runtime self-test had omitted.
- The runtime self-test now includes the misaligned 17-byte case, and the Python regression normalizes whitespace. Candidate head is `c2010a2175ea6f73b0057143def8d4172fc54d65`.
- Stage 1 Scheduler #203 passed on PR tip `7043444b45c5cd068651b7a2bb054851ae5d18cb`. Build #1782 for that tip remains pending with no job started; Build #1770 is the earlier source-regression failure.
- Exact-head full Build/QEMU/storage/production validation remains pending. Keep PR #40 unmerged until it passes. Main release verification and the stable ISO for the current main SHA remain separate gates.
## Latest PR #40 CI state — 2026-11-05

- Stage 1 Scheduler #203 passed native kernel build and Stage 1 scheduler certification on PR tip `7043444b45c5cd068651b7a2bb054851ae5d18cb`.
- Initial Build #1770 failed in the new Multiboot2 regression before build/QEMU because the 17-byte misaligned-size runtime case was missing; the candidate source/test were corrected.
- Build #1782 was cancelled during PR close/reopen; latest Build #1789 for docs tip `beaba6139cc06426ce5dc0243f6978a48e842e1f` is pending without a job. Exact-head full QEMU/storage/production validation remains blocked.
- Do not merge PR #40 until the corrected regression, clean build, QEMU, storage and production smoke gates pass. Main stable ISO and post-merge workflow verification remain separate.
## Final candidate-head validation snapshot — 2026-11-05

- Final Multiboot2 boot-range code/test head: `9c14a4a4fed9d0f3a8e21e3f5c2ecbf45a819a99`. It accepts aligned metadata below the 1 GiB identity-map limit, caps and aligns total size at 16 MiB, and reserves only the portion within the allocator's first 512 MiB.
- Build #1810 was pending with no job started; Build #1809 was cancelled. Build #1770's first regression attempt failed before build/QEMU due to the omitted 17-byte negative case, which has since been added.
- Stage 1 #230 completed native kernel build and scheduler-certification steps successfully but the overall workflow was cancelled; Stage 1 #231 was pending. Do not claim a green Stage 1 workflow for the final tip.
- Full Build/QEMU/storage/production validation for the final candidate remains blocked. Keep PR #40 open until exact-head checks complete; post-merge main and stable ISO verification remain separate.
## PR #40 latest CI result — 2026-11-05

- Stage 1 Scheduler #231 passed on code/test head `9c14a4a4fed9d0f3a8e21e3f5c2ecbf45a819a99`; native kernel build and Stage 1 scheduler certification both passed.
- Build #1810 was cancelled; latest Build #1816 for docs tip `9c1d3b1d5d9fc04c0857056b2fe5dea6a0cd9928` remained pending with no job started.
- The corrected Multiboot2 regression, full QEMU boot matrix, storage persistence, production ISO and desktop smoke remain unverified. Keep PR #40 unmerged until the full Build workflow succeeds.

## Evidence update — 2026-11-06 (Multiboot2 boot handoff)

- Code-bearing main merge for this review: `f20ea855fc30baf54cdc5d45915d357266f2ae6e`; subsequent commits in this documentation refresh are documentation-only and use `[skip ci]`. PR #40 merged to main as `f20ea855fc30baf54cdc5d45915d357266f2ae6e`. The fix separates the 1 GiB boot identity-map address limit from the 16 MiB information-block size limit and keeps allocator reservation clipped to the first 512 MiB.
- Candidate Build #1822 / run ID `41968388714` passed the Multiboot2 source regression, native build, clean `make check`, kernel/ISO checks, repeated QEMU boot, scheduler fault diagnostics, two-boot AHCI/NVMe persistence, production ISO and production desktop/network smoke. Release publication was skipped for the PR event.
- Stage 1 Scheduler #243 / run ID `41968388659` passed native build and Stage 1 certification.
- **Still required:** post-merge main push workflow evidence and publication/byte-comparison of the stable ISO against the exact production image from that main run. The earlier stable asset evidence for `9d77ad3` must not be attributed to `f20ea85`.
- Next gates remain the 600-second scheduler soak, physical G560/device certification, and continued kernel/process, storage recovery, driver/security and native desktop work before browser/media/AI/compatibility expansion. Product requirements are preserved; no completion estimate increase.


## 2026-11-07 — immediate P0 release action

1. Fix the stale-main publication predicate: compare the boot-tested build commit to fetched current main, excluding only documentation paths (`docs/**` and Markdown files). Documentation-only commits must not strand a verified bootable ISO; any kernel, source, test, build, boot configuration or workflow change must continue to block an obsolete publisher.
2. Run `tools/test_release_publish_guard.py` on PR CI, including temporary-Git-history cases for docs-only acceptance and kernel-change rejection. Then require the full native build, clean `make check`, QEMU boot matrix, storage persistence and production desktop smoke before merging.
3. After merge, verify the push-triggered main Build, compare the immutable versioned ISO and stable `zeroos.iso` byte-for-byte against that run's production ISO, and ensure release notes identify the same code-bearing commit.
4. Resume the 600-second scheduler soak and physical G560/device certification. Continue process/runtime hardening, storage recovery, driver/security, native desktop hardware integration, then browser/media/AI/compatibility. Preserve all Stage 0–10 and canonical product requirements; no completion percentage is raised by this CI/release correction.


## 2026-11-08 — release freshness fix merged

1. **Completed:** PR #41 merged as `fff32e9b35fa61f1911d43d45fc1b99aabfbdd57`. The release guard now permits a boot-tested image when main advances only through `docs/**` or Markdown changes; it still blocks publication after any release-relevant source, test, build, boot-config, or workflow change.
2. **Candidate evidence:** Build #1826 (`42332516298`) and Build #1825 (`42332496757`) passed the release regression, native build/check, three QEMU boots with 104 milestones each, SMP4, NX-disabled, failed-IPI, scheduler diagnostics, two-boot AHCI/NVMe persistence, production ISO and desktop/network smoke. Stage 1 #247 and #246 passed.
3. **P0 still open:** post-merge main Build #1827 (`42442854152`) was in progress at last inspection; Stage 1 #248 (`42442854128`) passed. Verify the main Build's exact QEMU/storage/desktop steps and release-guard decision.
4. **P0 still open:** the stable release API still showed the older `zeroos.iso` digest `ef092a8c...` and notes identifying `c3c4eb6` at the last query. After the main run, verify the immutable versioned and stable ISO assets byte-for-byte against its production ISO, and confirm notes identify the same build commit.
5. Continue the 600-second scheduler soak, physical Lenovo G560 boot/device matrix, physical SATA/NVMe/NIC/USB/graphics validation, storage recovery/fault injection, process/runtime hardening, driver/security and native desktop integration. Browser/media/AI/compatibility remain later roadmap work. Preserve every Stage 0–10 and canonical product requirement; no completion percentage is raised by this release-pipeline change.


## 2026-11-09 — release aligned; QEMU deadline evidence hardening

1. **Release integrity:** Main Build #1827 (`42442854152`) and Stage 1 Scheduler #248 (`42442854128`) passed on `fff32e9`. The release log confirms the docs-only freshness decision and stable ISO byte comparison. GitHub metadata reports identical SHA-256 `18cfe872ab7c946d6e4adc75e411043971d5e3878eb4a4df499e4320b94af15a` for `zeroos-fff32e9.iso` and `zeroos.iso`; release notes identify the same code-bearing commit.
2. **Boot-test correctness candidate:** Main's `tools/boot_test.sh` accepted QEMU exit statuses 124/137 without verifying that GNU `timeout` sent TERM at the requested deadline. A timeout-like exit before the observation window could therefore be confused with a sustained boot if all serial milestones had already been emitted. Candidate branch `fix/boot-timeout-duration-guard-20261109` uses `timeout --verbose`, requires the explicit deadline TERM marker, and retains `timeout.log` as an artifact. Its regression simulates early 124/137 exits. Do not claim this candidate passes until its exact Build workflow completes.
3. **Next P1 gates:** execute the optional 600-second/four-vCPU scheduler soak; reconcile PR #26 boot-test/soak changes and PR #27 scheduler-certification checks against current main; complete physical G560, SATA/NVMe, NIC, USB and graphics validation; expand storage crash/recovery fault injection.
4. Continue kernel/process lifecycle and memory-safety work, then drivers/security/recovery and usable native desktop integration, followed by browser/media, AI, and Windows/Android compatibility. Preserve every canonical product requirement and all Stage 0–10 workstreams. No completion percentage is increased for CI or documentation work.



## 2026-11-09 — first boot-timeout candidate CI correction

- Candidate PR #42 Build #1829 (run `42558109355`) and PR Build #1830 (run `42558124850`) stopped before compiler installation/native build/QEMU at the existing `tools/test_interrupt_gs_entry.py::test_qemu_boot_timeout_force_kills_paused_emulator` source assertion.
- The failure was an outdated test expectation for the exact command string. The candidate correctly added `LC_ALL=C timeout --verbose` and the deadline TERM-marker check, but the cross-suite GS/boot regression still asserted the previous unprefixed `timeout --kill-after=5s` command.
- Updated the cross-suite regression to assert the new verbose command, the retained timeout log, and that the deadline marker is checked before serial milestones. Removed an unused test-module import and clarified fresh-log comments.
- This is a test-contract mismatch, not a kernel compile, guest boot, or QEMU failure. No native build or QEMU result is claimed for Build #1829/#1830. A new exact-head Build is required.


## 2026-11-09 — QEMU timeout evidence guard merged

1. Merged boot-test integrity fix: PR #42 merged as 1a57d5cebe0e1eff87fd10450be841708de4e01d. The shared QEMU gate now requires GNU timeout's explicit deadline TERM marker before accepting timeout-like exit status 124/137, clears stale timeout logs, and preserves the fresh log in the boot diagnostics artifact. Regression tests cover early 124/137 exits and full-window timeouts. This is test-harness hardening, not a kernel feature.
2. Candidate evidence: Build #1833 (push) and #1834 (PR) both passed on exact head 3f36bb5; Stage 1 Scheduler #254/#255 passed. All three repeated QEMU boots reached all 104 milestones, with 32-bit entry and long-mode markers. AHCI/NVMe two-boot persistence and production desktop/network smoke passed on the candidate.
3. Post-merge gate: Stage 1 Scheduler #256 passed on main merge 1a57d5d. Main Build #1835 is still in progress at the reproducible release gate. Require its own boot, storage, production desktop and release-publication steps before recording a post-merge pass.
4. Release baseline: stable and versioned ISO assets remain byte-identical for previously release-tested snapshot fff32e9 (SHA-256 18cfe872ab7c946d6e4adc75e411043971d5e3878eb4a4df499e4320b94af15a). Publication for 1a57d5d is pending.
5. Next priorities: finish Build #1835; execute the 600-second/four-vCPU scheduler soak; selectively reconcile PR #26/#27; harden process lifecycle/VFS mount lifetime; expand storage crash/recovery tests; certify physical G560, SATA/NVMe, NIC, USB and graphics. Continue native desktop integration before browser/media/AI/compatibility. Preserve all canonical requirements and Stage 0–10 workstreams; no completion percentage is raised for test-harness work.

## 2026-11-09 — post-merge boot/release gate closed

1. Main Build #1835 (run 42560302805, code commit 1a57d5d) and Stage 1 Scheduler #256 (run 42560302640) completed SUCCESS.
2. Build #1835 boot gate passed all three QEMU iterations; each reached all 104 milestones and reported 32-bit entry and long-mode markers. The retained timeout.log confirms GNU timeout sent TERM at the configured observation deadline.
3. Storage persistence step passed. The retained serial-disk1.log and serial-disk2.log each contain SATA/NVMe discovery, successful destructive driver tests, /data boot counters 1 and 2, persistent-file validation, Ring-3 fsync probe and Stage 3 completion, with no panic/failure marker. The log contains a transient ERR-trap diagnostic between boots; retained serial evidence and the successful step conclusion establish both boot results.
4. Production QEMU smoke reached the interactive desktop session; RTL8139 attached, DHCP bound 10.0.2.15, and the first-hop gateway resolved. This is QEMU-only device evidence.
5. Release publisher observed only documentation changes after the tested SHA and published the 1a57d5d code snapshot. GitHub Release metadata confirms versioned zeroos-1a57d5c.iso and stable zeroos.iso are 12,369,920 bytes with identical SHA-256 082bd06046aad31f8f9d2d39b20b5361b46d0a44cd39ddda26bff07a321e58cd. Release notes identify 1a57d5c. The P0 post-merge boot/release gate is closed.
6. Remaining roadmap gates: 600-second/four-vCPU scheduler soak; physical G560 boot and device matrix; physical SATA/NVMe, NIC, USB and graphics; power-cut/crash recovery on physical storage. Continue process lifecycle/VFS mount lifetime hardening, then security/recovery and native desktop; browser/media/AI/compatibility remain later priorities. Preserve all Stage 0–10 and canonical product requirements; no completion percentage increase.

### 2026-11-10 — storage integrity: ordered-data writeback error handling

- Main baseline reviewed: code-bearing `1a57d5c`, documentation HEAD `e1392c0`. Main Build #1835 and Stage 1 #256 passed; the stable ISO is checksum-matched to the production image for `1a57d5c`.
- Highest-priority source defect found: `zj_commit_locked` ignored errors returned by `pc_writeback_mapping` for ordered data and proceeded to commit metadata. This could publish a file mapping whose data write failed.
- Candidate fix aborts ZJFS before writing the journal when ordered-data writeback fails. The runtime regression scopes injected EIO to the new file's allocated data-block LBA, verifies no metadata commit, and verifies the file is absent after remount.
- Candidate host regression and boot milestone are wired into CI. **Do not mark this milestone validated or merged until the exact candidate Build/QEMU/storage gates pass.**
- Continue scheduler soak, kernel/process lifecycle, crash/power-loss recovery, physical drivers and desktop. Keep browser/media/AI/compatibility behind those dependencies. Overall estimate remains ~43%.

## 2026-11-10 — PR #44 merged; main CI and stable ISO verified

- PR #44 was squash-merged as `100c8e2cf4cdd049c4bf5565b5eebbc0f5284651`. The latest code-bearing main commit is `100c8e2cf4cdd049c4bf5565b5eebbc0f5284651`; subsequent documentation updates do not change executable code.
- Main push Build #1855 (`42752361937`) completed successfully. Main Stage 1 Scheduler #276 (`42752361843`) completed successfully. The Build job passed the source regressions, native x86-64 build, reproducible release gate, kernel-image verification, QEMU boot test, scheduler panic-diagnostics fault certification, two-boot AHCI/NVMe persistence, production ISO generation, production desktop/network smoke, artifact preservation and GitHub Release publication.
- The main Build log records that main had advanced only through documentation files and that it published the boot-tested code snapshot `100c8e2cf4cdd049c4bf5565b5eebbc0f5284651`. It ends with: `stable release ISO matches the boot-tested production ISO`.
- The main boot-diagnostics artifact was downloaded and inspected: 3/3 normal QEMU boots reached all 106 required milestones; the new ordered-data FLUSH-EIO self-test marker is present; the four-vCPU log reports 4 CPUs online and scheduler certification passed.
- The eight-round crash/power-cut matrix passed. Round 2 explicitly exercised the torn journal-superblock fallback (`journal superblock invalid; checking descriptor/commit`) and recovered with fsck clean. All eight rounds report recovered/fsck clean.
- AHCI/NVMe two-boot persistence passed: the /data counter advanced from 1 on boot 1 to 2 on boot 2; both runs emitted the persistence-check and Stage 3 completion markers. Production desktop/network smoke passed.
- Release assets `zeroos-100c8e2.iso` and `zeroos.iso` are both 12,374,016 bytes and have identical SHA-256 `4628a75807bb9a074ce48eaf8e627997fd86b9c33c63201d66dacc785b498e80`. The Build release artifact's extracted production ISO independently hashes to the same value. Release notes identify commit `100c8e2` and describe a legacy BIOS El Torito live session; there is no disk installer.
- Candidate Build #1851 initially failed in crash round 2 because a torn journal-superblock write caused recovery to return `-SE_UCLEAN`. Descriptor/commit fallback recovery was added; Build #1854 passed the complete candidate matrix, and main Build #1855 then passed and published the verified image. Preserve this failed-run/correction history.
- No P0 build/boot/stable-release blocker remains for this snapshot. Remaining engineering gates: 600-second/four-vCPU scheduler soak; physical Lenovo G560 and device matrix; physical power-loss/recovery; USB/graphics/NIC certification; clean ACPI S5 shutdown; and a disk installer if installation (rather than live boot) is required.
- Overall completion estimate remains ~43%. No completion credit is assigned for CI or release-pipeline work.


## 2026-11-10 — current open engineering queue

Open PRs remain, but their branches are stale and not merge-ready. Ahead/behind counts below are measured against the code-bearing main commit 100c8e2; later documentation-only commits are excluded:

| PR | Workstream | Ahead | Behind | Current action |
|---|---|---:|---:|---|
| #26 | Stage 1 QEMU boot evidence | 6 | 52 | Rebase; retain only still-missing evidence |
| #27 | Scheduler certification | 1 | 51 | Reconcile with main; do not merge stale snapshot |
| #9 | Repeated-boot SMP/storage stress | 4 | 244 | Rebase selectively |
| #14 | Kernel/runtime hardening | 7 | 137 | Isolate individual fixes and retest |
| #23 | Release-note ordering | 16 | 84 | Reconcile; main Build #1855 already published verified notes/assets |
| #4 | Storage/device/desktop stack | 10 | 343 | Do not merge wholesale; review per subsystem |
| #32 | Process-parent documentation | 4 | 34 | Documentation reconciliation only |
| #25 | Current-main evidence docs | 3 | 52 | Superseded documentation; close or rebase after review |
| #8 | Shell/platform activation | 147 | 244 | Major staged rebase required |
| #6 | Pipe semantics | 19 | 343 | Rebase and rerun IPC tests |

The release pipeline itself is no longer a P0 blocker for code snapshot `100c8e2cf4cdd049c4bf5565b5eebbc0f5284651`: main Build #1855 verified and published the stable/versioned ISO pair at SHA-256 `4628a75807bb9a074ce48eaf8e627997fd86b9c33c63201d66dacc785b498e80`. The open #23 branch is therefore a reconciliation item, not evidence that publication is currently broken.

## 2026-11-10 — post-merge main build and stable ISO verification

- Latest code-bearing main merge is `100c8e2cf4cdd049c4bf5565b5eebbc0f5284651` (PR #44, ZJFS ordered-data FLUSH barrier and torn journal-superblock recovery); subsequent main commits in this review are documentation-only.
- Post-merge main Build #1855 (run `42752361937`) completed **successfully** on exact code-bearing commit `100c8e2`. Completed steps include source regressions, native x86-64 build, reproducible release gate, kernel image verification, QEMU boot test, scheduler panic diagnostics, two-boot AHCI/NVMe storage persistence, production ISO build, production desktop/network smoke, production artifact upload, and GitHub Release publication. Main Stage 1 Scheduler #276 (run `42752361843`) also completed successfully on the same commit.
- The `zeroos-bootable` release now contains `zeroos-100c8e2.iso` and stable `zeroos.iso`, each 12,374,016 bytes with identical SHA-256 `4628a75807bb9a074ce48eaf8e627997fd86b9c33c63201d66dacc785b498e80`. Release notes identify `100c8e2`, GRUB/El Torito legacy BIOS boot, and a live session; there is no disk installer. This closes the post-merge CI and stable-ISO P0 blockers for the current code-bearing main snapshot.
- Candidate PR #46 (`fix/block-flush-inflight-barrier-20261110`) addresses a block-layer ordering defect: a dispatched FLUSH was removed from `flush_waiting`, allowing later requests to become eligible while it remained in-flight. Current PR head is `4b7e8a6215fd03da62faa99e0c473c7cbd46a48c`. Stage 1 Scheduler #301 passed on this exact head; Build #1880 (run `42754005097`) is pending, while the earlier pre-rebase Build #1872 (run `42753158241`) reached QEMU boot but has not produced a final result. The source regression passed on the earlier code-equivalent head before one additional source assertion was added; exact-current-head source/build/QEMU/storage/production validation remains pending. Do not merge until Build #1880 completes successfully.
- Remaining release/engineering gates: 600-second/four-vCPU scheduler soak; physical Lenovo G560 boot and physical SATA/NVMe/NIC/USB/graphics matrix; physical power-loss/recovery; clean ACPI S5 shutdown. Existing broad PRs #26/#27/#14/#9/#8/#6/#4 remain heavily diverged and require selective rebase/review rather than blind merge. Overall estimate remains ~43%; no completion percentage increase for CI or documentation work.
