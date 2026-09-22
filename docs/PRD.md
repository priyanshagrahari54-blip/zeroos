# ZEROOS — Product Requirements Document
Version: 1.0 | Status: Master Planning Baseline

## 1. Product Definition
ZEROOS is a real x86-64 operating system intended to evolve from a small, testable kernel into a complete desktop platform. The product is not a visual shell over another OS. The kernel, memory manager, scheduler, drivers, userspace, security model, desktop, native applications, compatibility runtimes and system services are progressively implemented as ZEROOS components.

Current repository reality: the project already contains a real boot/kernel foundation, including Multiboot2 boot, x86-64 long-mode transition, paging/VMM work, physical allocation, GDT/TSS, IDT/ISR, PIC/PIT, task/process/thread primitives and scheduler work. The immediate product requirement is therefore stabilization before feature expansion.

## 2. Product Goals
### G1 — Real, reproducible OS
- Boot reliably in QEMU.
- Maintain deterministic build/test commands.
- Produce machine-verifiable milestones.
- Keep hardware assumptions explicit.

### G2 — Low-overhead platform
ZEROOS follows the rule: installed does not mean running, running does not mean continuously active, and active does not mean maximum resource usage.
Every service has lifecycle states, wake conditions, budgets and recovery behavior.
Literal zero CPU/RAM usage is impossible for active software; the engineering target is near-zero idle overhead and no unnecessary background work.

### G3 — Fast interaction
- Foreground work receives scheduler and I/O priority.
- Frequently used components may retain a tiny warm state.
- Heavy engines are demand-loaded.
- Dormant features preserve enough state to avoid a full cold start.
- Latency targets are measured on defined hardware rather than promised universally.

### G4 — Long-lived architecture
Public interfaces must be versioned. Hardware-dependent code must be isolated. Updates must support staged rollout, rollback and recovery. Major subsystems must be independently testable.

## 3. Target Users
1. Everyday desktop users.
2. Developers and system builders.
3. Students and educators.
4. Power users and gamers.
5. Hardware experimenters.
6. AI-assisted OS developers.

## 4. Product Modes
### Standard Mode
Balanced defaults, secure services, adaptive graphics and ordinary desktop behavior.

### Developer Mode
Debug console, tracing, symbols, profiling, test images, driver diagnostics and controlled experimental features.

### Performance/Gaming Mode
Foreground priority, background throttling, lower visual overhead, frame-time monitoring and optional capture/overlay services.

### Study Mode
Focus sessions, notes, PDF tools, OCR, flashcards, formula tools, dictionary and AI study assistance.

### Recovery Mode
Minimal services, filesystem checks, rollback, snapshot restore, diagnostics and logs.

## 5. Feature Domains
### 5.1 Kernel
Boot, CPU initialization, interrupts, scheduler, processes, threads, synchronization, timers, virtual memory, physical memory, IPC, syscall boundary and kernel diagnostics.

### 5.2 Hardware
PCI/PCIe discovery, ACPI, timers, storage, USB, input, audio, display/GPU, networking, webcam and power management. Hardware capabilities are detected before optional functionality is enabled.

### 5.3 Storage
VFS, filesystem drivers, mount management, caching, journaling where supported, permissions, snapshots, backup metadata and recovery tooling.

### 5.4 Networking
Ethernet/Wi-Fi abstractions, TCP/IP stack, DNS, DHCP, firewall, network diagnostics, connection manager and privacy controls.

### 5.5 Desktop
Original ZEROOS visual language: familiar desktop conventions without copying Windows, macOS or ChromeOS branding/design. Window manager, compositor, launcher, taskbar/dock equivalent, notifications, settings, search, widgets, themes and accessibility are separate layers.

### 5.6 Native Apps
ZERO Browser, file manager, terminal, settings, PDF reader, notes, media player, music, screenshot/recording, hardware center, performance center, package manager and recovery tools.

### 5.7 Compatibility
Windows compatibility targets broad Win32/Win64 APIs through a translation/runtime layer. ZEROOS does not copy Windows source code.
Android support uses an AOSP-compatible Android runtime strategy selected at implementation time. The exact Android release is locked per supported ZEROOS release and updated through the compatibility lifecycle.

### 5.8 AI
AI is an optional, event-driven system service. Forge AI remains a development/developer-agent project; ZEROOS may expose an OS AI service later. AI workloads are dormant when unused, hardware-aware, permissioned and resource-budgeted.

### 5.9 Security
Secure boot integration where hardware permits, privilege separation, process isolation, capability/permission controls, encryption, firewall, antivirus/malware scanning, secure vault, privacy dashboard, update verification and recovery.

## 6. Core Functional Requirements
Each feature must define:
- owner subsystem
- API contract
- lifecycle state
- CPU/RAM/I/O budget
- dependencies
- startup policy
- wake events
- shutdown/suspend behavior
- failure behavior
- telemetry/diagnostics
- tests
- compatibility requirements

## 7. Non-Functional Requirements
### Performance
- No mandatory busy loops for ordinary services.
- Event-driven waits preferred.
- Hardware acceleration used when available.
- Background work is throttled under foreground pressure.
- HDD systems use sequential/batched I/O where practical.

### Reliability
- Watchdogs for critical services.
- Panic diagnostics for kernel faults.
- Crash isolation for userspace.
- Recovery path for failed updates.
- Safe shutdown and filesystem consistency procedures.

### Security
- Least privilege.
- Explicit permission boundaries.
- No hidden persistence.
- Signed/verified system updates where the trust chain exists.
- Audit logs must avoid unnecessary sensitive data.

### Accessibility
Keyboard-first navigation, screen-reader foundations, high-contrast modes, scaling, reduced motion, captions, input alternatives and localization.

## 8. Acceptance Model
A feature is not complete merely because it compiles. It must pass:
1. Unit tests.
2. Integration tests.
3. Failure-path tests.
4. Resource-budget tests.
5. QEMU/hardware tests where applicable.
6. Recovery tests.
7. Documentation/API review.

## 9. Definition of Done
A milestone is complete only when:
- implementation exists,
- tests exist,
- CI passes,
- failure behavior is documented,
- performance/resource behavior is measured,
- APIs are documented,
- upgrade/recovery impact is understood,
- no known invariant is intentionally weakened to hide a failure.

## 10. Product Priorities
P0: boot, memory, interrupts, scheduler, process/thread correctness, ring-3, syscalls, IPC, storage and basic drivers.
P1: graphics, compositor, input, networking, desktop shell and native core apps.
P2: security center, package manager, browser, media, backup/update/recovery.
P3: Windows/Android compatibility, AI integration, gaming/study ecosystems and advanced device/cloud integration.

## 11. Explicit Non-Goals
- Claiming universal hardware support without tests.
- Guaranteeing zero resource usage.
- Guaranteeing every Windows/Android application.
- Shipping a copied macOS/Windows/ChromeOS UI.
- Making AI mandatory for normal operation.
- Using proprietary codec/licensing claims without actual licenses.

## 12. Product Success
ZEROOS succeeds when it can boot reproducibly, run isolated userspace applications, manage hardware and storage safely, provide a responsive desktop, recover from faults/updates, and progressively add compatibility and ecosystem layers without destabilizing the kernel foundation.


## 13. Advanced-First Product Maturity Contract

The product is not allowed to accumulate intentionally throwaway “basic” subsystems. Stage order exists because subsystems have dependencies; it does not define a low maturity level.

For every product subsystem, the intended mature architecture must be established before implementation begins. Where dependencies prevent complete activation, the implementation must maximize production-complete scope while preserving the final contract.

### Production Feature Gate

A user-visible feature is production only after:
- implementation;
- API/ABI contract;
- lifecycle and ownership;
- security boundary;
- resource policy;
- failure isolation;
- recovery behavior;
- diagnostics;
- negative/failure testing;
- stress testing where appropriate;
- QEMU/hardware validation where applicable;
- CI;
- documentation.

### Product Quality Rule

ZEROOS must not ship a collection of demos that later require architectural rewrites. Advanced capabilities such as SMP-aware scheduling, mature virtual memory, crash-consistent storage, isolated drivers, accelerated graphics, accessibility and resource governance are treated as production architecture concerns when their dependencies permit them.

### Stage Meaning

Stage 0–5 are dependency gates:

Stage 0 = production engineering/reproducibility contract  
Stage 1 = production kernel  
Stage 2 = production userspace  
Stage 3 = production storage  
Stage 4 = production hardware/networking  
Stage 5 = production graphics/desktop

Completion of Stage 5 does not imply the entire ZEROOS product is complete; later ecosystem, compatibility, AI, update and broader security systems remain separately gated.
