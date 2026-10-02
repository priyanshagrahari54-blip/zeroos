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
