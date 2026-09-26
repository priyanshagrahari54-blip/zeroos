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
