# ZEROOS Master Contract & Feature Expansion

Status: SPECIFICATION ADDITION
This document expands the canonical ZEROOS specification with cross-cutting engineering contracts and additional product/platform capabilities. It does not certify implementation.

## 1. Master Engineering Contract

Every ZEROOS subsystem, service, driver, runtime, UI surface, package, and AI tool must define:

- purpose and scope
- architecture and dependency boundaries
- public API/ABI and versioning
- data structures and ownership
- object/resource lifetime
- concurrency model and lock ordering
- interrupt and execution-context rules
- memory ownership, mapping, allocation and reclamation rules
- DMA/cache-coherency rules where applicable
- CPU/I/O/GPU/network resource budget
- activation, dormant, wakeup, throttling and suspension policy
- failure modes and containment boundary
- timeout/cancellation/backpressure semantics
- recovery and rollback behavior
- security and permission model
- observability, metrics, tracing and diagnostics
- compatibility/capability requirements
- unit/integration/negative/concurrency/fault/stress/soak tests
- performance measurements and regression thresholds
- acceptance criteria
- implementation/verification status

No implementation is considered complete solely because a design document exists.

## 2. Boot, Firmware and Platform Contract

Add explicit architecture for:

- UEFI and legacy BIOS boot policy
- boot-stage ownership and handoff contracts
- kernel/initramfs/module loading
- secure-boot and measured-boot integration where supported
- boot failure classification and recovery
- boot-time tracing and critical-path profiling
- firmware capability discovery
- ACPI/SMBIOS capability reporting
- platform-specific quirks isolated from generic kernel code

## 3. Hardware Compatibility and Certification

Define a machine/device capability matrix covering:

- CPU topology/features
- RAM capacity and memory-map classes
- storage controllers/devices
- GPU/display capabilities
- audio
- Ethernet/Wi-Fi/Bluetooth
- USB/HID
- battery/AC/thermal sensors
- firmware/boot mode

Each device/driver receives a lifecycle and certification status:

DESIGNED -> IMPLEMENTED -> TESTED -> CERTIFIED -> REGRESSION-LOCKED

Unsupported hardware must produce explicit diagnostics rather than silent degradation.

## 4. Production Networking

Expand networking to include:

- Ethernet and Wi-Fi management
- IPv4/IPv6 dual-stack policy
- DHCP, DNS and route management
- VPN abstraction
- captive-portal detection
- network profiles
- connection quality/latency diagnostics
- per-service/per-app network permissions
- network namespaces or equivalent isolation where required
- suspend/resume and roaming handling
- firewall integration
- bounded connection/resource accounting

## 5. Package and Application Platform

Define:

- signed package format
- package metadata and dependency graph
- repository metadata and mirror policy
- install/update/remove/rollback transactions
- per-app permissions and resource limits
- application identity
- application lifecycle
- package quarantine/revocation
- safe uninstall and orphan cleanup
- offline package installation
- compatibility metadata
- reproducible package builds where practical

## 6. ZEROOS SDK and Developer Platform

Define a stable developer platform with:

- native application ABI/API policy
- ZEROOS SDK and headers
- GUI/application toolkit
- build/package integration
- debugger and symbol support
- profiler and tracing tools
- crash dump/symbolication support
- developer mode
- application templates/examples
- API compatibility policy
- deprecation/migration policy
- developer documentation and conformance tests

## 7. Observability and Diagnostics

Create one coherent diagnostics model for:

- kernel logs
- service logs
- crash dumps
- boot diagnostics
- tracing
- CPU/RAM/I/O/GPU/network counters
- wakeup accounting
- thermal/power events
- storage errors
- driver errors
- application failures
- security events

Universal diagnostics should answer, where evidence permits:
"what failed?", "what changed?", "what is consuming resources?", and "what recovery action is safe?"

### Operational reliability

- Define service-level indicators and objectives only for services with a meaningful user-facing reliability target. State the event, numerator, denominator, measurement window, and evidence source; do not invent availability targets for an OS that is not operated as a service.
- Use reliability objectives as engineering decision aids. When a target is missed, prioritize understanding and reducing the failure rate before increasing risky change; do not treat an error budget as a substitute for release gates.
- Alert only on actionable conditions with an owner and a documented response. Keep diagnostic evidence (timestamps, build/commit, configuration, logs, and relevant counters) sufficient to reconstruct impact and sequence of events.
- Maintain concise runbooks for supported operational/recovery paths. They must identify the symptom, safe containment steps, decision owner, evidence to preserve, recovery/rollback reference, and escalation boundary. Keep detailed update, recovery, and release procedures in their existing Stage 6, Stage 10, and validation documents.
- For incidents affecting users, data integrity, security boundaries, or release confidence, record impact and timeline, review contributing system/process factors without blame, and track preventive actions to completion. A small project may combine response roles, but must still name an incident owner.
- Distinguish automated gates from manual procedures, and mark unverified operational capabilities as pending rather than claiming they are operationally ready.

## 8. Identity, Accounts and Sessions

Define:

- local users and groups
- authentication/session lifecycle
- credentials and secret storage
- lock/unlock
- session isolation
- capability/permission inheritance
- process ownership
- login/startup policy
- optional enterprise/domain integration boundary

## 9. Backup, Migration and Disaster Recovery

Add:

- encrypted user-data backup
- configuration backup
- application metadata backup
- incremental/deduplicated backup architecture where useful
- restore verification
- selective restore
- full-machine migration
- recovery after interrupted updates
- disaster-recovery procedures
- backup health/status UI

## 10. Privacy Platform

Define:

- microphone/camera access indicators
- application permission prompts
- sensitive-data classification
- telemetry policy and user controls
- privacy audit trail
- application data isolation
- data export/deletion
- location/device access controls
- ZERO AI context boundaries
- secret/key isolation

## 11. Supply-Chain and Release Security

Add:

- reproducible build goals
- compiler/toolchain provenance
- SBOM generation
- dependency inventory
- artifact signing
- release provenance
- vulnerability intake and response
- package/update revocation
- build/release attestation where supported

## 12. Internationalization and Accessibility Platform

Beyond Hindi/English UI strings, define:

- Unicode correctness
- font fallback
- keyboard layouts and input methods
- locale-aware dates/numbers/currency
- RTL readiness
- translation versioning
- screen-reader APIs
- keyboard-only navigation
- magnification
- high contrast
- captions
- reduced motion
- alternative input/accessibility APIs

## 13. Virtualization and Isolation

Define a native virtualization boundary for:

- VM lifecycle
- virtual CPU/memory
- virtual storage
- virtual networking
- snapshots
- resource limits
- device passthrough policy
- sandbox/developer environments
- isolation between native, Windows, Android and guest workloads

## 14. Power-Loss and Data-Integrity Certification

Every persistent subsystem must have tests for:

- sudden power loss
- interrupted writes
- journal replay
- filesystem recovery
- interrupted package/update transaction
- interrupted snapshot operation
- battery-critical shutdown
- dirty-state detection
- repeated crash/reboot cycles

## 15. Performance Laboratory

Standardize measurements for:

- boot time
- idle CPU/RAM
- application launch
- scheduler latency
- context-switch latency
- syscall/IPC latency
- allocation/page-fault latency
- storage latency/throughput
- network latency/throughput
- UI frame time and missed presents
- GPU frame time
- suspend/resume
- power and wakeups

Performance claims require reproducible measurements and workload/device metadata.

## 16. Compatibility Laboratory

Define a compatibility matrix:

QEMU -> legacy/low-resource hardware -> mainstream hardware -> modern hardware

For each major capability:

DESIGNED -> IMPLEMENTED -> TESTED -> VERIFIED -> CERTIFIED

Certification must record hardware, firmware, driver/runtime version, workload, test result and known limitations.

## 17. ZERO AI Agent Safety Contract

ZERO AI must use a permissioned broker between the model and OS capabilities.

Required controls:

- capability-scoped permissions
- read/write/execute/network separation
- destructive-action confirmation
- secret isolation
- command sandboxing
- filesystem scope
- network scope
- autonomous-task timeout
- task cancellation
- audit trail
- reversible/transactional operations where feasible
- emergency STOP
- failure isolation
- model/tool provenance
- context minimization
- user-visible action history

ZERO AI must never require unrestricted kernel access.

## 18. Additional ZEROOS Product Features

These are product-level additions to evaluate and specify without forcing implementation prematurely:

### 18.1 Universal Device Center
One place for displays, audio, network, Bluetooth, USB, printers, storage, cameras and input devices, with capability-aware controls and diagnostics.

### 18.2 Smart Workspace Profiles
Profiles such as Work, Study, Gaming, Battery Saver and Presentation can change application lifecycle, resource governance, notifications, display, audio and network policies. Profiles must be explicit policy bundles, not arbitrary hidden tuning.

### 18.3 Offline-First System Search
Universal Search should remain useful without network access, using local indexes and clearly marking operations that require network access.

### 18.4 System Restore Timeline
A user-visible timeline for snapshots, configuration changes, package updates and recovery checkpoints, with safe rollback boundaries.

### 18.5 App Health Center
Per-application crash history, resource history, permission state, compatibility information and recovery actions.

### 18.6 Network Health Center
Local diagnosis for DNS, DHCP, route, latency, packet loss, link state and firewall decisions without requiring cloud services.

### 18.7 Storage Health Center
SMART/NVMe health where available, filesystem integrity state, free-space pressure, writeback errors, snapshot state and recovery recommendations.

### 18.8 Hardware Capability Profiles
The compositor, browser, media, gaming and AI stack adapt to detected CPU/GPU/RAM/storage/thermal capabilities. Feature degradation must be deterministic and observable.

### 18.9 Focus and Attention System
Study/Work/Presentation modes with notification suppression, app lifecycle policy, timers and accessibility-aware behavior.

### 18.10 Local Automation Engine
Event -> condition -> action workflows with permission scopes, rate limits, cancellation and audit logging.

### 18.11 Secure Temporary Workspace
An isolated disposable workspace for opening untrusted files, testing applications or running development experiments, with explicit network/storage permissions.

### 18.12 Unified Clipboard History
Bounded, privacy-aware clipboard history with sensitive-content exclusion and per-app restrictions.

### 18.13 Nearby Device Transfer
Local-network/P2P file and clipboard transfer with authenticated pairing, explicit permissions and no mandatory cloud dependency.

### 18.14 Recovery Assistant
A guided diagnostic/recovery workflow that can collect evidence, suggest reversible actions, enter recovery mode and produce a support bundle.

### 18.15 Accessibility Test Mode
A built-in way to inspect focus order, labels, keyboard reachability, contrast, captions, reduced-motion behavior and screen-reader exposure.

## 19. Feature Lifecycle Contract

Every feature must explicitly document:

STOPPED
-> DORMANT
-> WARM
-> ACTIVE
-> THROTTLED
-> SUSPENDED

Transitions must be event-driven where possible. Installed components are not assumed to be running or continuously active.

## 20. Documentation Synchronization Rule

This document is cross-cutting. The canonical master specification, roadmap, gap inventory, micro-requirements, implementation prompts, validation documents, AI handoff and relevant subsystem specifications must reference these additions.

When a subsystem-specific document conflicts with the canonical master contract, the conflict must be recorded and resolved explicitly; it must not be silently ignored.

## 21. Acceptance

This expansion is accepted as a documentation requirement only when:

1. the relevant document set references the contract;
2. roadmap/gap/requirements entries exist for each applicable area;
3. implementation status is tracked separately from specification;
4. tests and measurements exist before verification claims;
5. no new feature is allowed to weaken kernel isolation, resource governance, security or recoverability.


## Implementation synchronization — 2026-10-05
The cross-cutting contract remains specification-only unless backed by code and evidence. Current implementation is **~44% overall** (S1 92%, S2 83%, S3 45%, S4 39%, S5 41%, S6 20%, S7 12%, S8 12%, S9 12%, S10 6%).

Newly enforced cross-cutting evidence: ZERO AI action capabilities are distinct from context grants and are rechecked after queueing, so permission revocation prevents execution. This satisfies part of the AI safety contract; it does not certify the complete Stage 9 platform.

All future status updates must follow the same rule: implementation + test + integration evidence, not documentation volume.