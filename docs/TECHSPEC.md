# ZEROOS — TECHNICAL SPECIFICATION
Version: 1.0

## 1. Platform
Primary target: x86-64 PCs and VMs.
Initial boot environment: Multiboot2/GRUB-compatible flow.
Primary implementation languages: C with assembly where CPU/ABI entry requires it.
Build system: existing Make-based flow plus CI.
Primary validation environment: QEMU.

## 2. Boot Specification
Required milestones:
B0 boot header valid
B1 32-bit entry
B2 bootstrap page tables
B3 long mode
B4 kernel entry
B5 physical memory discovery
B6 GDT/TSS
B7 virtual memory
B8 IDT/IRQ/timer
B9 scheduler
B10 userspace init

Each milestone must emit a deterministic diagnostic marker in test/debug builds.

## 3. CPU/Interrupt
Long-term requirements:
- exceptions 0–31 handled explicitly,
- external IRQ routing,
- APIC/IOAPIC for modern systems,
- syscall entry mechanism,
- per-CPU state,
- SMP startup,
- FPU/SSE/AVX state policy where required,
- CPU feature detection.

## 4. Memory
Page size baseline: 4 KiB.
Structures:
- physical page allocator,
- page tables,
- address-space object,
- kernel mapping,
- user mappings,
- page-fault handler,
- memory pressure manager.

Future mechanisms:
COW, demand paging, mmap, shared memory, page cache, reclamation, optional swap.

## 5. Scheduler
Initial scheduler may be simple but must have explicit:
- run queue,
- current task,
- task state,
- time slice,
- wakeup path,
- block path,
- context switch,
- interrupt return.
Future: priority classes, SMP load balancing, real-time policy and cgroup-like resource controls.

## 6. Process/Thread
Process owns address space, handles/resources and security identity.
Thread owns CPU execution state.
Identifiers use generation protection to reduce stale-handle ambiguity.
Exit path:
running -> exit -> zombie/resource handoff -> reaped.

## 7. Syscalls
Design principles:
- stable numeric IDs,
- architecture-independent userspace ABI where possible,
- explicit error codes,
- pointer validation,
- length validation,
- copy-in/copy-out,
- cancellation for blocking calls where possible.

Initial syscall groups:
process, thread, memory, file, directory, time, IPC, synchronization, device, network.

## 8. Filesystem/VFS
VFS objects:
superblock, mount, inode/node, directory entry, file object, descriptor.
Requirements:
- permissions,
- open/close,
- read/write,
- seek,
- stat,
- directory iteration,
- mount/unmount,
- fsync,
- error recovery.

Implemented (Stage 3): VFS.md specifies the objects, lock order,
per-operation blocking/error/concurrency/crash semantics and the file
syscall ABI (numbers 25–50, feature bit 7, additive to ABI v1).
ZJFS.md specifies the on-disk format v1, the journal commit/replay
protocol, error behavior and the fsck/repair contract.

## 9. Storage
Block layer provides sector/block I/O and queueing.
I/O scheduler adapts to HDD/SSD/NVMe.
HDD policy: sequential batching and low random background I/O.
NVMe policy: queue depth and parallelism may increase when safe.

Implemented (Stage 3, STORAGE.md):
- priorities FOREGROUND/NORMAL/BACKGROUND with 50-tick aging;
- background I/O capped at 1/4 of the hardware depth and kept out of the
  last 1/4 of the request pool;
- HDD C-SCAN, SSD FIFO, and NVMe FIFO over up to 4 CPU-local hardware
  queues;
- back-merging up to 32 segments;
- up to 2 retries for failed reads and writes;
- 5 s timeouts, with poll-before-reset lost-interrupt recovery;
- the page cache limited to min(4096 pages, free/4), with a 25% dirty
  limit and a 10% background threshold.

## 10. Networking
Required layers:
link -> IP -> TCP/UDP -> DNS/DHCP -> sockets -> network manager.
Firewall rules are evaluated efficiently.
Diagnostics include interface state, address, route, DNS and connectivity tests.

## 11. Drivers
Driver model:
enumerate -> match -> initialize -> register capability -> serve requests -> suspend/resume -> remove.
No driver may assume a fixed device topology.

## 12. Graphics
Display capabilities:
resolution, refresh rate, color depth, acceleration, multi-monitor.
Compositor uses retained scene state and damage tracking.
Fallback path supports software composition.

## 13. Audio
Audio graph:
applications -> mixer/session -> device engine -> codec/DSP -> hardware.
Exclusive/low-latency mode is optional.
Hardware acceleration is used when supported.
Proprietary technologies such as Dolby processing require actual licensing; ZEROOS may implement independent DSP features without implying Dolby certification.

## 14. Browser
Browser architecture:
UI -> tab manager -> network process -> renderer process -> sandbox -> GPU process where applicable.
Tab lifecycle is resource-aware.
Renderer crashes must not crash the desktop.

## 15. Security
Security service responsibilities:
policy, firewall, scan scheduling, permissions, encryption status, secure vault, update trust.
Deep scans should be idle-aware and throttled.

## 16. Package Manager
Package metadata includes:
name, version, architecture, dependencies, permissions, files, signatures/checksum, rollback metadata.
Install transaction:
resolve -> download -> verify -> stage -> install -> validate -> commit.

## 17. AI Runtime
Request lifecycle:
idle -> request -> policy check -> context selection -> backend selection -> inference -> action/result -> cleanup.
Backends may be local models, remote models or hardware accelerators according to user policy.
No model is continuously active without work.

## 18. Android
Android support is an isolated runtime.
The supported Android release is selected and pinned per ZEROOS release after development begins.
Runtime components are demand-loaded and can be suspended/unloaded.
Graphics/input/audio/network bridges are explicit.

## 19. Windows Compatibility
Target API family: Win32/Win64.
Architecture:
application -> compatibility API -> translation/runtime -> ZEROOS services.
The project does not depend on a Windows kernel or proprietary Windows source.

## 20. Update and Recovery
Use staged, verified updates.
Preferred strategy:
inactive system slot -> update -> boot health check -> mark good.
Failure:
watchdog/health failure -> previous slot/recovery environment.

## 21. Resource Governance
Budgets:
CPU percentage or scheduler weight, memory ceiling, I/O class, wake frequency, network allowance.
Pressure signals:
CPU saturation, memory pressure, I/O queue depth, thermal state, battery level.
Actions:
throttle, freeze, suspend, unload, resume.

## 22. Diagnostics
Every subsystem emits structured events with:
timestamp, component, severity, event ID, context, correlation ID where needed.
Sensitive values are redacted.

## 23. CI Requirements
At minimum:
build -> image validation -> static checks -> unit tests -> QEMU boot -> serial milestone checks -> scheduler/process tests -> panic detection.
A kernel change that breaks QEMU boot blocks merging.

## 24. Versioning
ZEROOS release has:
kernel ABI version,
userspace ABI version,
driver interface version,
package repository compatibility version,
runtime compatibility versions.

## 25. Resource Performance Targets
Targets are benchmark goals, not guarantees:
- near-zero CPU for idle event-driven services,
- bounded resident memory for dormant controllers,
- no unnecessary polling,
- measurable wake latency,
- foreground workload protected from background maintenance.


## 26. Production-Grade Technical Maturity

Stage order does not permit intentionally simplified technical contracts.

### Kernel

Production technical targets include:
- SMP/per-CPU architecture;
- APIC/IOAPIC and MSI/MSI-X readiness;
- scheduler run queues, priorities, fairness/latency controls, affinity and load balancing;
- robust process/thread/address-space separation;
- demand paging, COW, reclaim and controlled swap where supported;
- hardened user/kernel memory validation;
- structured kernel diagnostics.

### Storage

Production storage targets include:
- queued block I/O;
- HDD/SSD/NVMe-aware scheduling;
- DMA;
- VFS lifetime correctness;
- page cache and writeback;
- crash consistency;
- filesystem recovery;
- snapshots;
- integrity/encryption architecture.

### Hardware

Production hardware targets include:
- PCI/PCIe capabilities;
- ACPI;
- DMA/IOMMU architecture;
- USB;
- HID;
- display/GPU;
- audio;
- networking;
- power/thermal;
- suspend/resume;
- hotplug where supported.

### Desktop

Production desktop targets include:
- isolated graphics service;
- compositor with retained scene/damage tracking;
- frame pacing;
- window ownership/focus;
- multi-monitor/DPI;
- accessibility tree;
- crash isolation;
- adaptive resource policy.

### Testing

Each production subsystem must have an explicit validation matrix covering normal, boundary, failure, stress, resource and recovery behavior. Unsupported hardware must be reported rather than silently treated as supported.
