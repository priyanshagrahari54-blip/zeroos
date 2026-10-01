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
