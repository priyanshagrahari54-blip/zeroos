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
- Validation source: docs/ZEROOS_MASTER/VALIDATION.md
- AI handoff source: docs/ZEROOS_MASTER/ZEROOS_AI_HANDOFF.md
- Core roadmap: docs/ZEROOS_MASTER/ZEROOS_MASTER_ROADMAP.md
- Blueprint: docs/ZEROOS_MASTER/ZEROOS_MASTER_BLUEPRINT.md
- Architecture: docs/ZEROOS_MASTER/ARCHITECTURE.md
- Hardware: docs/ZEROOS_MASTER/HARDWARE.md
- Boot specification: docs/ZEROOS_MASTER/BOOT_SPEC.md
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

For the granular implementation/validation checklist from boot through release, see `docs/ZEROOS_MASTER/ZEROOS_MICRO_REQUIREMENTS_FROM_START.md`. It is intentionally checklist-based and does not mark requirements complete merely because they are documented.

## 48. REMAINING GAP CLOSURE

Cross-cutting details that are easy to miss are tracked in `docs/ZEROOS_MASTER/ZEROOS_REMAINING_GAP_CLOSURE.md`, including toolchain/reproducibility, firmware edge cases, CPU/RAS, locking/memory ordering, crash forensics, time/locale, users/sessions, IPC/filesystem/network security, randomness, graphics robustness, UX reliability, package sandboxing, browser untrusted-content handling, compatibility runtime details, gaming measurement, ZERO AI agent safety/privacy, backups, observability, test infrastructure, formal invariants, documentation consistency, supply-chain security, installer/first boot, shutdown/reboot, and final repository-wide gap searches.

## End of exhaustive retained context.


---

## Cross-Cutting Master Contract

See [`ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md`](./ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md) for the mandatory cross-cutting engineering contract and the expanded ZEROOS feature/platform catalog. Applicable requirements cover ownership/lifetime/concurrency, boot/firmware, hardware certification, networking, packages, SDK, observability, accounts, backup/recovery, privacy, supply-chain security, accessibility/i18n, virtualization, power-loss certification, performance/compatibility labs, ZERO AI safety, and additional product features. This is a specification link only; implementation status remains evidence-based.
