# ZEROOS — ENGINEERING RULES
Version: 1.0

## R1 — Correctness First
No feature is accepted if it knowingly violates a kernel invariant, memory ownership rule, security boundary or data-integrity requirement.

## R2 — No Fake Completion
A stub, mock, screenshot, placeholder API or demo must be labeled as such. Do not describe it as implemented functionality.

## R3 — Real Hardware Boundaries
Hardware support must be separated from generic policy. Use capability detection rather than assuming a device exists.

## R4 — Lifecycle Required
Every service/feature defines:
DORMANT, WARM, ACTIVE, THROTTLED, SUSPENDED and STOPPED behavior as applicable.

## R5 — Event Driven
Prefer:
interrupts, queues, callbacks, timers, file notifications and explicit wake events.
Avoid:
tight polling, repeated full scans, unnecessary periodic timers.

## R6 — Foreground Priority
User-visible work gets scheduler, memory and I/O preference. Background work yields under pressure.

## R7 — No Absolute Performance Claims
Never promise “0 lag”, “0 RAM”, “never crashes” or a universal millisecond latency. Use measurable targets and hardware-specific benchmarks.

## R8 — Memory Discipline
Every allocation has a reason, owner and lifetime.
Every persistent cache has an eviction policy.
Every buffer has a maximum size.

## R9 — Interrupt Discipline
Interrupt context must stay minimal.
Do not perform unbounded work in IRQ handlers.
Do not reuse invalid interrupt frames.

## R10 — Scheduler Discipline
State transitions are explicit and testable.
Current-task ownership and saved-context ownership must never be ambiguous.

## R11 — ABI Discipline
Syscalls, IPC structures and driver interfaces are versioned.
Backward compatibility is preferred.
Breaking changes require migration notes.

## R12 — Security by Boundary
Validate all userspace inputs.
Enforce permissions in privileged code.
Do not rely on UI controls as security enforcement.

## R13 — Failure is a State
Every subsystem defines:
detection -> isolation -> diagnostics -> recovery -> user notification.

## R14 — Updates are Reversible
System updates should have a known rollback path before activation.

## R15 — AI is Not Authority
AI-generated code is untrusted until reviewed and tested.
AI cannot override rules because its output appears plausible.

## R16 — UI Originality
ZEROOS may use familiar interaction concepts but must establish its own visual identity, information architecture and component language.

## R17 — Accessibility
All major UI workflows require keyboard navigation, semantic labels and scalable presentation.

## R18 — Localization
User-facing strings must not be hard-coded into rendering logic.
Hindi and English are first-class localization targets.

## R19 — Compatibility Isolation
Windows and Android support cannot become dependencies of the core kernel.

## R20 — Idle Means Idle
If a service has no work, it should sleep, wait or be unloaded. “Background service” is not a justification for continuous CPU activity.

## R21 — Storage Conservatism
Avoid unnecessary writes, scans and metadata churn, especially on HDD and low-end flash storage.

## R22 — Logs
Logs must have levels and retention policy. High-frequency debug logging cannot be enabled permanently on production configurations.

## R23 — Tests
Each new subsystem needs unit, integration and failure-path tests where meaningful.

## R24 — Reproducibility
Builds should be reproducible as far as the toolchain permits. Pin important tool versions and document host requirements.

## R25 — Change Size
Small changes are easier to verify. Large refactors require a design note and migration plan.

## R26 — Documentation Sync
If implementation changes an invariant or public interface, documentation changes in the same change set.

## R27 — No Silent Resource Escalation
A feature must not silently increase background CPU, memory, disk or network usage beyond its declared policy.

## R28 — Recovery First
Boot, storage, update and authentication paths need recovery behavior before they are considered production-ready.

## R29 — Privacy
Do not collect data merely because it is technically possible. Minimize, explain and control local/cloud data flows.

## R30 — Definition of Done
Code + tests + diagnostics + documentation + resource behavior + recovery path = done.


## R31 — Advanced-First Implementation

Stages define dependency order, not maturity. Do not implement intentionally basic/throwaway versions.

## R32 — Production Gate

A subsystem cannot be called PRODUCTION without appropriate:
- correctness;
- ownership/lifetime;
- concurrency;
- security;
- resource;
- diagnostics;
- failure/recovery;
- negative/stress/fault testing;
- CI;
- QEMU/hardware verification.

## R33 — Final Architecture First

When dependencies block a feature, define its final production contract first and implement the maximum production-complete portion that does not depend on the blocked component.

## R34 — No Placeholder Completion

A stub, partial driver, mock service or demo path must retain PARTIAL/EXPERIMENTAL status. It must never be represented as production functionality.

## R35 — Evidence-Based Stability

“Stable”, “production”, “fast”, “low memory” and similar claims require actual evidence from tests, measurements or supported hardware matrices.

## R36 — Recovery Is Part of Implementation

Failure detection, isolation, diagnostics, recovery and user-visible status are part of the feature itself, not optional post-release work.

## R hardware discovery is not device support
A discovered hardware identifier is not an operational driver. Generic
enumeration must not enable bus mastering, probe BAR sizes by destructive
writes, map unvalidated ranges, or activate interrupts. Device resources need
exclusive ownership and exactly-once cleanup. Unsupported devices remain
unbound and must be reported honestly.
