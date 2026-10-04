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

## Cross-Cutting Master Contract

See [`ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md`](./ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md) for the mandatory cross-cutting engineering contract and the expanded ZEROOS feature/platform catalog. Applicable requirements cover ownership/lifetime/concurrency, boot/firmware, hardware certification, networking, packages, SDK, observability, accounts, backup/recovery, privacy, supply-chain security, accessibility/i18n, virtualization, power-loss certification, performance/compatibility labs, ZERO AI safety, and additional product features. This is a specification link only; implementation status remains evidence-based.
