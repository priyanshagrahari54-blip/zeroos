# ZEROOS — DEVELOPMENT PHASES
Version: 1.0 | Stage/Substage Execution Plan

## Phase 0 — Governance and Baseline
### 0.1 Repository contract
Freeze documentation locations, branch policy, commit conventions and CI requirements.
### 0.2 Toolchain
Document compiler, linker, assembler, GRUB/QEMU and host dependencies.
### 0.3 Architecture freeze
Define ownership and ABI boundaries before adding large features.
Exit: reproducible build and documented architecture.

## Phase 1 — Kernel Stabilization
### 1.1 Boot
Validate Multiboot2, CPU transition and kernel entry.
### 1.2 Memory
Validate physical allocator, page tables and mapping permissions.
### 1.3 Interrupts
Validate exceptions, IRQ routing, timer and interrupt-frame ownership.
### 1.4 Scheduler
Fix current-task/context/IRQ-frame invariants. Stress creation, switching, blocking, wakeup and timer preemption.
### 1.5 Process/thread
Validate lifetime, PID/TID generation, address-space association and zombie cleanup.
Exit: QEMU boot CI is green and scheduler/process tests are stable.

## Phase 2 — Userspace Core
### 2.1 Ring-3
Create user address space and safe entry.
### 2.2 Syscall ABI
Implement syscall entry/return and argument validation.
### 2.3 Init
Create first userspace process and service manager.
### 2.4 IPC
Pipes, queues, events and shared memory.
Exit: multiple isolated userspace processes communicate without kernel corruption.

## Phase 3 — Storage and Files
### 3.1 Block layer
Disk enumeration and request queue.
### 3.2 VFS
File/directory abstractions.
### 3.3 Filesystem
Select initial filesystem and implement robust mount/read/write.
### 3.4 Page cache
Cache file data without unbounded growth.
### 3.5 Recovery
fsync, consistency checks and recovery utilities.
Exit: persistent userspace filesystem works reliably.

## Phase 4 — Hardware and Networking
### 4.1 PCI/ACPI
Hardware discovery.
### 4.2 Input
Keyboard, mouse/touch foundations.
### 4.3 Display
Framebuffer/basic display.
### 4.4 Network
Ethernet/Wi-Fi driver strategy, IP stack and sockets.
### 4.5 Audio
Playback/capture abstraction.
Exit: practical hardware I/O works on defined test hardware.

## Phase 5 — Graphics and Desktop
### 5.1 Graphics service
Display abstraction and GPU interface.
### 5.2 Compositor
Windows, surfaces, damage tracking.
### 5.3 Shell
ZERO Bar, launcher, notifications, workspaces.
### 5.4 Settings
System configuration service.
### 5.5 Accessibility
Semantic tree, keyboard navigation, scaling and reduced motion.
Exit: desktop session is usable without kernel debugging tools.

## Phase 6 — Core Native Apps
File manager, terminal, browser foundation, settings, package manager, text editor/notes, PDF reader, media player, screenshot/recorder and hardware center.
Each app must have resource lifecycle policy and crash isolation.

## Phase 7 — Security, Update and Recovery
Firewall, permissions, encryption integration, antivirus scanning, privacy center, secure vault, update manager, snapshots and rollback.
Exit: system can recover from controlled update and application failures.

## Phase 8 — Performance/Power
### 8.1 Resource Governor
Budgets and pressure signals.
### 8.2 Thermal
CPU/GPU thermal adaptation.
### 8.3 Battery
Power profiles and suspend.
### 8.4 Storage adaptation
HDD/SSD/NVMe policies.
### 8.5 Browser lifecycle
Active/idle/frozen/discarded tabs.
Exit: background work is demonstrably controlled.

## Phase 9 — Compatibility
### 9.1 Windows
Start with a narrow Win32 API slice, expand through test suites.
### 9.2 Android
Integrate isolated Android runtime using the selected AOSP baseline.
### 9.3 Packaging
Make compatibility runtimes independently updateable.
Exit: defined application compatibility matrix, not vague “supports Windows/Android”.

## Phase 10 — AI and Automation
AI broker, local/remote model adapters, system search, diagnostics, coding assistant, study assistant, automation engine and performance explanations.
AI is optional and event-driven.

## Phase 11 — Ecosystem
Cloud sync, device link, offline maps, smart-home integrations, P2P transfer, themes/widgets, localization expansion, developer SDK and package repository.

## Phase 12 — Release Engineering
Hardware certification matrix, release channels, LTS branch, security advisories, rollback tests, upgrade tests and long-term maintenance.
Major releases should not require users to accept avoidable regressions.

## Cross-Phase Gate
Every phase must pass:
A. functional tests
B. failure tests
C. resource tests
D. security review
E. documentation update
F. migration/upgrade analysis
G. QEMU/hardware validation as applicable.

## Current Recommended Execution
Do not jump from the current kernel foundation directly to UI/AI.
First stabilize scheduler/process/thread invariants, then establish ring-3 and syscall contracts. Everything above those boundaries becomes substantially safer after that.
