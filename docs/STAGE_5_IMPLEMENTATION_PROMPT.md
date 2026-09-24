# ZEROOS — STAGE 5 IMPLEMENTATION PROMPT
Version: 1.0 — Production-Grade Graphics, Desktop, Compatibility and AI Ecosystem
Repository: https://github.com/priyanshagrahari54-blip/zeroos

## Mission
Continue from current `main` only after Stage 2–4 contracts are verified. Build the production desktop/platform layer, then integrate the remaining ecosystem capabilities from the ZEROOS master requirements without turning the kernel into desktop policy.

Read docs/MASTER_IMPLEMENTATION_PROMPT.md and all current architecture/UI/security/technical docs first.

## PART A — Graphics and Desktop

### 1. Graphics service
Separate:
- display service
- GPU abstraction
- compositor
- shell
- application UI.

Kernel supplies primitives; desktop policy remains userspace.

### 2. Window system
Implement:
- surfaces/buffers
- ownership
- focus
- stacking
- move/resize
- minimize/maximize
- workspaces
- snapping/tiling
- multi-monitor
- DPI/scaling
- input routing
- accessibility semantics
- buffer lifetime
- application crash isolation.

### 3. Compositor
Implement:
- retained scene model
- damage tracking
- occlusion
- frame scheduling
- frame pacing
- vsync
- buffer lifecycle
- GPU synchronization
- resource caching
- software fallback
- low-power mode
- reduced-motion mode.

Do not redraw unchanged content unnecessarily.

### 4. ZEROOS shell
Original identity, not a Windows/macOS/ChromeOS clone.

Provide:
- ZERO Bar
- launcher
- Universal Search
- workspaces
- window overview
- snapping
- notifications
- quick controls
- system status
- performance center
- update/recovery access
- settings
- file manager
- terminal
- clipboard
- downloads.

### 5. Universal Search
Providers:
apps, files, folders, settings, documents, recent items, commands, diagnostics, optional AI actions.

Pipeline:
input → parser → intent → parallel providers → ranking/merge → UI.

Indexing must be:
incremental, asynchronous, cancellable, low-priority, event-driven and resource-aware.

Never repeatedly scan the entire disk unnecessarily.

### 6. Settings
Schema-driven searchable settings with:
value, scope, dependencies, permission, default, reset, persistence and migration.

### 7. Notifications
Implement:
grouping, priority, deduplication, rate limiting, dismissal, deferral and accessibility semantics.

### 8. Accessibility/localization
Production interfaces for:
keyboard navigation, semantic accessibility tree, screen reader, scaling, high contrast, reduced motion, captions, alternative input, English and Hindi localization.

### 9. Desktop lifecycle
Use:
STOPPED → DORMANT → WARM → ACTIVE → THROTTLED → SUSPENDED.

Keep only tiny controllers warm where justified. Heavy engines are demand-activated.

### 10. Crash isolation
Watchdog/restart/recovery for desktop services. App/renderer/search/notification/compositor failures must not take down kernel, scheduler or memory manager.

Every UI surface supports where applicable:
normal, loading, empty, error, offline, permission denied, low-resource, reduced-motion, keyboard navigation and localization states.

## PART B — Security, Recovery and Updates

Implement/mature:
- privilege separation
- least privilege
- capabilities/permissions
- sandboxing
- firewall integration
- antivirus/security scanning architecture
- encryption
- secure vault/storage
- privacy center
- audit/security logs
- crash recovery
- service restart
- safe/recovery boot
- diagnostics
- filesystem checks
- snapshots
- rollback
- transactional/staged updates
- update verification
- failed-update recovery
- atomic metadata/state transitions.

Never leave a half-applied update without a defined recovery path.

## PART C — Windows Compatibility

Build a Wine-like compatibility architecture, not copied Windows:
- Win32/Win64 API translation
- executable loader integration
- filesystem translation
- registry/config abstraction
- graphics translation
- audio/input translation
- process/thread mapping
- synchronization mapping
- DLL/runtime management
- compatibility diagnostics.

Every unsupported API must fail explicitly and diagnostically.

Runtime lifecycle:
INSTALLED != RUNNING.
Unused compatibility runtime must be dormant/cold.

## PART D — Android Runtime

Build an isolated Android runtime based on AOSP/Linux/ART-compatible architecture.

Define and document a stable Android/AOSP baseline at implementation time.

Support:
- application/package installation
- metadata
- permissions
- filesystem isolation
- lifecycle
- graphics
- input
- audio
- networking
- suspend/resume
- crash isolation.

Do not claim compatibility beyond tested application/device matrix.

## PART E — Browser

Implement/mature browser architecture with:
- tab/process isolation
- navigation
- rendering
- storage
- networking
- permissions
- crash recovery.

Tab lifecycle:
ACTIVE → IDLE → FROZEN → DISCARDED.

Restore retained state when reactivated. Do not keep every tab fully active.

## PART F — AI Platform

ZEROOS AI is separate from Forge AI. Use a clean integration boundary; do not copy Forge AI into this repository merely to provide AI features.

Implement event-driven system AI for:
- system-wide search
- file search
- document summarization
- coding assistance
- study assistance
- diagnostics
- troubleshooting
- performance analysis
- driver/device assistance
- contextual help
- application assistance.

AI service must be demand-driven:
- no unnecessary model execution
- no continuous polling
- minimal resident controller/state
- model/engine cold or dormant when unused
- permission-aware data access
- privacy-aware logging.

AI actions must use existing OS permission/capability boundaries.

## PART G — Study Center

Implement:
- PDF support
- notes
- OCR
- formulas
- flashcards
- dictionary
- focus mode
- study sessions
- AI study assistant.

## PART H — Media

Implement/mature:
- local music library
- playlists
- metadata/artwork
- subtitles
- multiple audio tracks
- playback speed
- screenshots
- hardware decoding
- AV sync
- UHD where hardware supports it
- video playback.

Only lawful/open/licensed media sources and permitted official APIs. Never bypass DRM, ads, authentication or access controls.

## PART I — Gaming

Implement:
- game profiles
- resource profiles
- FPS/performance monitoring
- recording
- overlay
- controller support
- low-latency mode
- game-specific resource policy.

Gaming mode must cooperate with scheduler/resource governor instead of bypassing safety limits.

## PART J — Cloud and Device Ecosystem

Implement secure foundations for:
- cloud sync
- cloud drive
- phone/device integration
- clipboard sync
- notification sync
- P2P sharing
- offline maps
- smart-home integration where supported.

Every service must have offline behavior and explicit permissions.

## PART K — Automation

Event/action framework for:
- file events
- app events
- device events
- timers
- network events
- system state changes.

Automation must be permission-controlled, bounded and auditable.

## PART L — Performance/Resource Governance

Measure:
boot time, idle CPU/RAM, wakeups/sec, scheduler latency, context switches, allocation/page-fault latency, disk/network latency, UI frame time, compositor latency, app launch, suspend/resume, thermal and power behavior.

Under pressure:
1. pause optional work
2. lower background priority
3. reclaim caches
4. freeze/discard inactive features
5. protect foreground work
6. invoke recovery only when necessary.

Never claim literal zero CPU/RAM/latency or universal compatibility.

## PART M — Final Validation

Validate:
- cold boot
- desktop session
- multi-window
- input/display/audio
- native app lifecycle
- service crash/restart
- Windows runtime lifecycle
- Android runtime lifecycle
- browser tab lifecycle
- AI activation/deactivation
- gaming workload
- media workload
- study workload
- low-memory/high CPU-GPU
- thermal pressure
- network loss
- storage errors
- update interruption
- recovery/rollback
- security boundaries
- accessibility
- English/Hindi localization
- resource accounting.

Required evidence where applicable:
UNIT + INTEGRATION + NEGATIVE + FAULT + STRESS + SOAK + SECURITY + RECOVERY + QEMU + REAL HARDWARE + PERFORMANCE.

## Final engineering rules
- No mocks presented as finished functionality.
- No throwaway APIs.
- No polling where event-driven operation is possible.
- No disabled tests to obtain green CI.
- No weakened assertions to hide bugs.
- No proprietary compatibility claims without evidence/licensing.
- Preserve existing kernel invariants.
- Update docs whenever architecture/API/lifecycle changes.
- Use focused coherent commits.
- End every batch with exact status, changes, architecture, tests/results, stress/fault evidence, resource impact, security, recovery, docs, CI, risks and one next priority.

Definition of done: ZEROOS has a verified production desktop/platform layer with isolated graphics/userspace services and the supported compatibility, security, recovery, AI and ecosystem capabilities backed by measurable tests and an explicit support matrix.
