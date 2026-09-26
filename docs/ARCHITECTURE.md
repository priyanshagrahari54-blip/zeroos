# ZEROOS — SYSTEM ARCHITECTURE
Version: 1.0 | Master Architecture

## 1. Architectural Model
ZEROOS is a layered, modular x86-64 operating system.

Hardware
-> Boot/firmware interface
-> Kernel core
-> Hardware abstraction/drivers
-> Kernel services
-> Userspace runtime
-> System services
-> Desktop/compositor
-> Native applications
-> Compatibility runtimes
-> Optional AI/cloud/device services

Cross-cutting: security, observability, resource governance, update/recovery.

## 2. Kernel Boundary
The kernel owns:
- CPU mode and interrupt control,
- physical memory,
- virtual memory,
- scheduler,
- processes/threads,
- synchronization,
- timers,
- IPC primitives,
- syscall boundary,
- privileged hardware control,
- core security enforcement.

The kernel should not own desktop policy or application UI.

## 3. Boot Architecture
Expected progression:
Firmware/bootloader -> Multiboot2 -> early CPU setup -> paging -> long mode -> kernel entry -> memory initialization -> GDT/TSS -> VMM -> IDT/interrupts -> timers -> scheduler -> userspace bootstrap.

Each transition has a verifiable milestone.
Boot failures must identify the last completed milestone.

## 4. Memory Architecture
### Physical
Page allocator discovers usable ranges from boot memory information, reserves kernel/boot structures and returns aligned pages.

Future evolution:
bootstrap bitmap -> scalable allocator -> per-CPU caches -> object/slab allocator -> reclaim.

### Virtual
Four-level x86-64 page tables with explicit mapping permissions.
Support planned for:
- user/kernel separation,
- demand paging,
- copy-on-write,
- memory-mapped files,
- shared mappings,
- page reclaim,
- optional swap.

### Ownership
Every physical page and major kernel object needs ownership/lifetime semantics.

## 5. CPU Architecture
`CPU_ARCHITECTURE.md` defines capability discovery, the SSE2/FPU baseline,
NX enablement, invariant-TSC measurement and the per-CPU ownership record.
The current supported matrix is x86-64 QEMU/PC with a capability-probed
legacy PIC fallback or a validated LAPIC/IOAPIC timer path. When valid MADT
processor records and the Local APIC path are available, the SMP boundary
prepares and handshakes bounded AP records; otherwise it selects an explicit
BSP-only recovery mode rather than fabricating online CPUs. Multi-vCPU
scheduling and hardware certification remain later gates.

## 6. Execution Architecture
### Tasks
A task represents schedulable execution state.

### Threads
Threads represent execution contexts belonging to a process/address space.

### Scheduler
Scheduler state transitions must be explicit:
RUNNING -> RUNNABLE -> RUNNING
RUNNING -> BLOCKED
BLOCKED -> RUNNABLE
RUNNING -> ZOMBIE/EXITED where applicable.

Interrupt-return context and voluntary context-switch context must not be conflated without a documented invariant.

## 7. Interrupt Architecture
IDT dispatches exceptions and IRQs.
PIC is the validated rollback path; the current APIC/IOAPIC implementation
owns the validated timer route and the IPI mechanisms used by the SMP boundary.
Full modern multiprocessor support still requires non-timer routing, per-CPU
scheduler execution and complete SMP coordination.
Timer interrupts drive scheduling/timers.
IRQ registration must separate hardware delivery from device-driver work.

## 8. SMP Architecture
The current Stage 1 boundary provides:

- bounded ACPI MADT CPU discovery;
- a low-memory real-mode/protected-mode/long-mode AP trampoline;
- per-CPU GS records and per-CPU GDT/TSS/IDT installation;
- INIT/SIPI startup with a generation-tagged online handshake and explicit
  failure state;
- one bounded INIT/SIPI retry per AP, with late-attempt rejection and BSP-only
  recovery when an AP cannot complete initialization;
- inter-processor TLB shootdown request/acknowledgement plumbing, including
  removal of a failing AP from the target mask;
- AP idle/interrupt dispatch that never borrows the BSP scheduler context.

Per-CPU scheduler queues, AP device-IRQ ownership, FPU state policy, lock
contention instrumentation and full multi-vCPU stress/hardware certification
remain production gates. The AP boundary therefore fails closed during
startup, reports degraded mode, and never fabricates an online CPU.

## 9. Userspace Architecture
Userspace begins with an init/bootstrap process.
Core services are separate processes where practical:
- service manager,
- device manager,
- storage manager,
- network manager,
- security service,
- update service,
- desktop session,
- compositor,
- notification service.

## 10. Syscall Architecture
Syscalls are a versioned ABI.
Required groups:
process/thread, memory, files, IPC, synchronization, time, networking, devices, permissions.
User pointers are validated.
ABI structures have explicit sizes/version fields where extensibility is required.

## 11. IPC
Planned mechanisms:
- message queues,
- shared memory,
- event/notification handles,
- pipes,
- sockets.
IPC must support blocking and nonblocking modes without busy waiting.

## 12. Storage
VFS provides a stable namespace.
Filesystem drivers implement filesystem-specific operations.
Storage stack:
device -> block layer -> cache -> filesystem -> VFS -> permissions -> userspace API.

Snapshots and backups are layered above filesystem primitives.

Stage 3 implementation (details: STORAGE.md, VFS.md, ZJFS.md):
- discovery: PCI class 01 → AHCI (NCQ, MSI) and NVMe (MSI-X, per-CPU
  queues) drivers. Legacy IDE is PARTIAL (reported, not driven);
- block layer: fixed request pools, priority + aging scheduler (HDD C-SCAN,
  SSD FIFO, NVMe multi-queue), merging, flush barriers, retries, an
  event-driven timeout watchdog, and controller reset with a bounded
  drain;
- GPT validation with backup recovery. Only ZEROOS-typed ZJFS partitions
  are mounted, and disks are never auto-formatted;
- bounded page cache (dirty limits, background flusher, sticky writeback
  errors, pinned mmap pages) and the ZJFS journaling filesystem (ordered
  data, checksummed metadata, replay, orphans, fsck/repair);
- VFS with explicit refcounted ownership and uid/gid permissions, exposed
  to Ring 3 through syscalls 25–50 (`ZEROOS_ABI_FEATURE_FILES`);
- one storage manager task that becomes the event-driven storage worker
  (periodic commit, deferred process-exit cleanup).

## 13. Driver Architecture
Drivers should expose capability-oriented interfaces.
Bus enumeration identifies hardware.
Device manager loads only required drivers.
Optional drivers remain dormant.

## 14. Graphics
Display stack:
GPU/display discovery -> kernel/driver interface -> graphics service -> compositor -> shell/apps.
Hardware acceleration is preferred when available.
Software fallback is mandatory for basic operation where practical.

### Stage 5 status (display primitive + desktop platform core)
Implemented (this stage):
- Kernel display primitive (`kernel/fb.c`): Multiboot2 framebuffer discovery
  (info tag 8), physical reservation of overlapping RAM, supervisor MMIO
  mapping with readback verification, and the `DISPLAY_INFO` syscall
  (ID 51, ABI feature bit 8) returning geometry + PRESENT/degraded flags.
  Missing/unusable framebuffers degrade to the serial console milestone and
  never fail the boot. The kernel owns no desktop policy.
- Pixel-mapping syscall (`DISPLAY_PRESENT`, ID 52, feature bit 9): Ring-3
  submits damage rectangles in the native scanout format; validation is the
  host-tested `display_present_request_valid` contract (bounds, overflow,
  format/bpp agreement, stride floor and cap), writes go through
  `fb_write_pixels` row chunks with the whole source range pre-validated,
  and the boot probe verifies Ring-3 writes with a kernel readback
  (`display present contract verified`). Degraded boots fail ENOENT by
  contract; concurrent submitters stay memory-safe via the fb lock, with a
  single display-service writer as desktop policy.
- Input stack (`kernel/input.c` + `scancode_core` + `mouse_core` +
  INPUT_POLL/WAIT): i8042 controller bring-up, IRQ1 (keyboard) and IRQ12
  (mouse) routing on both the IOAPIC and PIC topologies
  (`apic_route_legacy_irq`, `pic_unmask_irq`), set-1 scancode and 3-byte
  mouse-packet decoding into the locked `input_core` queue, event-driven
  wait/wake for Ring-3, and a kernel waiter/waker certification probe
  (`input blocking wait/wake path passed`). Degradation is
  serial-reported per device (keyboard readiness gates Ring-3 start; the
  mouse is best-effort) and never fails the boot. Desktop-side event
  routing (pointer focus, click-to-focus/raise, implicit grab, overlay
  priority, clamped relative motion) lives in `userspace/desktop/` and is
  host-tested.
- Cryptographic primitives (`kernel/crypto.c`, linked into the kernel):
  ChaCha20 block/cipher, Poly1305 and AEAD_CHACHA20_POLY1305 exactly as
  specified in RFC 8439, with a streaming Poly1305 context for MAC data
  built from several buffers. Host-verified against the RFC's published
  vectors (block 2.3.2, cipher 2.4.2, MAC 2.5.2, key generation 2.6.2,
  AEAD 2.8.2) plus wrong-key/tampered-tag/tampered-ciphertext/tampered-AAD
  negatives that must fail with authenticated-output zeroing. Policy —
  key storage, nonce generation, who may encrypt what — deliberately
  stays with future callers (vault, updates, privacy centre).
- Automation framework (`userspace/desktop/src/automation.c`): bounded
  event/rule engine (32 rules, 64-entry audit ring) with explicit
  permission grants, per-rule cooldowns, lifetime fire caps, injected
  action ops (notify/settings/callback/log) and drainable audit records
  (rule, event, result, tick) for the privacy center — host-tested.
- Session/shell process (`userspace/session/session.c`, embedded and
  launched by `kernel/session.c`): the first Ring-3 shell process binds
  the display service to the real DISPLAY_INFO/DISPLAY_PRESENT syscalls,
  rasterizes a compositor frame into a staging framebuffer and submits
  the damaged region, pumps INPUT_POLL/INPUT_WAIT through the desktop
  input router, and certifies each contract with serial milestones
  (live and degraded display paths both accepted; reaped cleanly).
  Its launcher maps a 16-page (64 KiB) downward-growing stack
  (`ZEROOS_USER_STACK_PAGES`) because `session_main` reserves >17 KiB of
  frame space in one function — a single mapped page faulted below the
  stack region on the first local store (CI-observed before the fix).
- Desktop platform core (`userspace/desktop/`): fixed-capacity, zero-heap,
  freestanding-clean modules for lifecycle, resource governor, display
  service (scanout submit gating), window
  system, compositor (retained scene, damage, occlusion, pacing, cache,
  software fallback), Universal Search, schema-driven settings,
  notifications, accessibility, i18n (en+hi), and service watchdog —
  gated by `make desktop-check` (3500+ assertions, hosted + freestanding).

- Browser tab lifecycle (`userspace/desktop/src/browser.c`): the
  ACTIVE -> IDLE -> FROZEN -> DISCARDED ladder (Phase 8.5) driven by an
  injected monotonic clock, metadata-survives-discard reloads, crash
  states with explicit recovery (no auto-restart, double-crash and
  early-focus rejected), bounded at 16 tabs with capacity errors —
  host-tested.  Rendering engine and tab UI remain.
- AI broker (`userspace/desktop/src/ai.c`): Part F contracts are in
  section 17; permissions, downgrade, dormancy and wipe semantics are
  host-tested.
- Windows compatibility core (`userspace/compat/`): Part C contracts
  are in section 18; host-tested via `make compat-check`.
- ZERO Bar core (`userspace/desktop/src/bar.c`): applet layout with a
  flexible title area, pointer/keyboard activation with action routing,
  quick toggles with explicit provider denial accounting, notification
  badges, DPI-scaled physical width — host-tested.  Shell chrome
  rendering remains in the not-yet list.
- Launcher core (`userspace/desktop/src/launcher.c`): bounded app
  registry, case-insensitive query with prefix-over-substring ranking
  and recency ties, launch dedup (EBUSY), hook-driven failure states,
  empty-state recents — host-tested.
- Capability gate (`userspace/desktop/src/capability.c`): per-service
  activation with explicit masks, runtime grant/revoke, check-gated
  privileged ops with an audit ring, and crash/stop deactivation that
  drops every grant (no sticky privileges across restarts) —
  host-tested.  Wiring services onto the gate and the rest of Part B
  (firewall, vault, snapshots, transactional updates) remain.  The
  launcher and ZERO Bar are wired to the gate (spawn and
  settings-write capabilities respectively).
- Metrics recorder (`userspace/desktop/src/metrics.c`): named
  counters plus a present-interval histogram with percentile and
  average reads; the display service records presented/refused/failed
  outcomes and inter-present intervals through the optional hook, and
  the module is part of the freestanding session link — host-tested.
- Transactional update core (`userspace/desktop/src/update.c`):
  section 20 pipeline (download → verify → stage → preflight →
  activate → health check → commit) as an explicit state machine with
  injected side-effect hooks, rollback on health/activation failure,
  verify failure that never activates, cancel only before activation,
  and rejected-transition accounting — host-tested.
- Strict URL parser (`userspace/desktop/src/url.c`): http/https/about
  only; javascript:, data:, file: and userinfo never navigate;
  control characters, label rules, port ranges and percent-escapes
  (including control-byte decodes like %00/%0A) rejected with named
  reasons — host-tested.
- PE image validator (`userspace/compat/src/pe.c`): first loader
  stage — MZ/PE signature, x86-64 machine, PE32+ format, section
  table and entry/header bounds against truncation with explicit
  diagnostics (section 18) — host-tested.
- Study Center core (`userspace/desktop/src/study.c`): deck/card
  registry with a bounded spaced-repetition ladder (0/1/3/7/21/60
  days), lapse and ease accounting, due-card selection and
  focus-session timing — host-tested; heavier surfaces attach later.
- FPS monitor (`userspace/desktop/src/fps.c`): clock-injected
  sliding-window FPS, frame-time percentiles, budget-breach counting
  and the 8/16/33 ms performance profiles — host-tested; overlay and
  controller support remain in the not-yet list.
- Snapshot manager (`userspace/desktop/src/snapshot.c`): bounded
  registry with CREATING→READY/FAILED lifecycle, hook-driven restore
  and capacity pruning that refuses when the discard hook fails —
  host-tested.
- Encrypted vault (`userspace/desktop/src/vault.c`): AEAD
  (ChaCha20-Poly1305) wrapping of every secret under an injected
  32-byte master key — no plaintext at rest, explicit auth failure on
  tamper or wrong key, key wiped on lock — host-tested against the
  certified crypto module.
- Navigation controller (`userspace/desktop/src/nav.c`): strict-URL
  gated go/back/forward with bounded history and blocked-scheme
  accounting — host-tested.
- Firewall policy engine (`userspace/desktop/src/firewall.c`): up to
  32 first-match rules (direction/protocol/port+IP ranges/app scope/
  established-only), default deny, explicit flow validation and
  allow/deny counters — host-tested; the kernel packet path binds to
  it later.  Stress suite (`test_stress.c`) drives sustained
  capacity cycles across browser/vault/firewall/snapshot/fps with
  exact end-state accounting — host-tested.
- Sandbox policy core (`userspace/desktop/src/sandbox.c`): named
  profiles with explicit capability masks (fs/net/spawn/display/
  audio/device classes), fail-closed checks for unknown profiles and
  classes, per-profile denial counters and an audit ring —
  host-tested; seccomp/namespace enforcement binds later.
- Clipboard service (`userspace/desktop/src/clipboard.c`): bounded
  11-entry history with owner/format metadata, sensitive clippings
  that become current but never enter history, current-excluding
  depth cycling, eviction of oldest and a clear-all privacy wipe —
  host-tested.
- Downloads manager (`userspace/desktop/src/downloads.c`): bounded
  queue with explicit QUEUED/RUNNING/DONE/FAILED/CANCELED lifecycle,
  single-active-transfer policy (cooperative sharing), monotonic
  progress with regression rejection, start-hook failure accounting
  and terminal-state immutability — host-tested.
- Media policy core (`userspace/desktop/src/media.c`, part H): origin
  registry with explicit rights (play/cache/export/sync), default
  deny for unregistered origins, and an unconditional DRM gate —
  protected items are refused with per-reason counters; the contract
  contains no bypass path by construction.  Host-tested.
- Overview/expose model (`userspace/desktop/src/overview.c`, part
  A): ceil(sqrt) thumbnail grid over a work area with edge-inclusive
  gaps, exact hit-testing (gaps and empty cells excluded), keyboard
  focus cycling with wrap, remove-with-relayout and degenerate-area
  rejection (never zero-size thumbs).  Host-tested with exact grid
  math.
- File manager core (`userspace/desktop/src/filemgr.c`, shell
  surface): injected directory source (session binds
  `zeroos_readdir`; tests bind memory — never a faked filesystem),
  stable name/size/mtime sorts, hidden filtering with visible-index
  selection, bounded 16-deep path history with back/forward and
  forward-drop discipline, capacity truncation flagged (not hidden),
  source errno propagation with explicit failed state, and
  permission-gated remove/mkdir (join sanitation rejects traversal,
  denied actors never touch ops).  Host-tested.
- Formula engine (`userspace/desktop/src/formula.c`, part G):
  documented math subset — precedence, right-associative INTEGER
  powers (non-integer exponents are explicit errors, never
  silent NaN), unary minus above power so `-2^2 = -4`, \frac and
  \sqrt (Newton iteration, negative argument errors), variable
  bindings across parses, 128-char input / 64-node arena / depth-16
  caps all failing with positions.  Host-tested.
- AI study assistant (`zd_study_assist_request/submit`, part G):
  builds a SUMMARIZE broker request from study context (deck + due
  card, SELECTION context bit only, payload truncated safely) and
  submits through the AI broker — permission denial happens in the
  broker up front, so an ungranted request never reaches a backend.
  Fixed an include-guard mismatch in `ai.h` (define did not match
  ifndef) exposed by the new include path; all desktop headers are
  now guard-audited.
- Study notes + dictionary (`notes.c`/`dict.c`, part G): bounded
  notebook (64 notes, 256-byte bodies, pin, case-insensitive
  substring search over title+body with documented cap semantics)
  and a dictionary loaded from an injected word source (case-
  insensitive sort/dedup, binary-search lookup, prefix suggestions
  with total-vs-written accounting; load errors leave the dict
  unreadable at -95).  No bundled corpus is claimed — the word list
  binds later, tests inject fixtures.  Host-tested.
- OCR contract (`userspace/desktop/src/ocr.c`, part G): honest
  capability gate — no engine linked in this build, availability 0,
  every call fails -95 AFTER full argument validation; pluggable
  engine registration path tested with a test fixture.  No text is
  ever fabricated and no accuracy is claimed until an engine lands.
- Terminal core (`userspace/desktop/src/term.c`, shell surface):
  bounded 24x80 (max 120x240) cell grid with streaming parser —
  CSI cursor/erase/SGR (colors + bold/underline/inverse), LF/CR/BS/
  TAB, column wrap, 128-line scrollback ring (oldest evicted, order
  preserved), UTF-8 collected across writes with invalid bytes
  counted, OSC titles consumed (BEL and ESC \ terminators), ESC
  intermediates such as `ESC ( B` drained so their final bytes never
  leak into the screen, oversized CSI counted once and drained.
  Unknown sequences are consumed + counted, never shown as text.
  No PTY yet: child binding comes later through zd_term_write.
- Privacy centre (`userspace/desktop/src/privacy.c`, part B):
  read-only aggregation of denial/refusal counters from sandbox,
  firewall, media, ecosystem and clipboard engines into a domain
  breakdown (filesystem/network/content/sync) plus a documented risk
  ladder (none/watch/review/elevated/act-now with explicit
  thresholds 1/25/50/200 and review-over-elevated precedence),
  sensitive-clip events surfaced as protected, optional sources
  contribute zero rather than failing — host-tested against real
  engine counters.
- Update<->snapshot production binding (`zd_snapshots_bind_update`,
  part B): the update machine's `stage_apply` hook captures a fresh
  "update" snapshot (capture errors abort staging atomically) and
  `rollback` restores the newest READY one (none available -> -2,
  which the machine converts to FAILED — never a silent half-
  rollback).  Wiring fix: `stage_apply` and the snapshot `capture`
  hook were documented but never invoked; both are now called during
  their transitions with fail-closed semantics and regression tests.
- Cloud/device ecosystem core (`userspace/desktop/src/eco.c`, part
  J): offline-first sync queue — pairing grants zero permissions,
  grants/revoke operate on exact bits, enqueue demands the paired
  device + that bit, revoke/unpair drop dependent pending entries,
  offline flush transmits nothing while keeping the queue intact,
  and online flush re-validates pairing and permissions per entry
  before delivery (refusals counted per reason).  Host-tested.
- Gaming core (`userspace/desktop/src/gaming.c`, part I): per-app
  profiles (eco/balanced/perf, target fps, overlay + low-latency
  flags), physical-to-logical button remapping with unmapped
  accounting, and a cooperative yield matrix (degrade / overlay-off /
  target-floor) decided from measured fps and memory pressure —
  decisions only; sampling stays in the fps ring.  Host-tested.
- PDF document contract (`userspace/desktop/src/pdf.c`, part G):
  bounded zero-heap subset parser over caller-owned bytes — header
  and version check, `/Type /Page` walk with object-number recovery,
  `/Contents N 0 R` resolution to content streams, Tj/TJ literal
  extraction with escapes and rollback of non-show operands, 64-page
  and 1 KiB-per-page caps, and explicit unsupported failures (`-95`
  for FlateDecode/encryption, `-22` malformed) with status names for
  UI surfaces.  Hand-authored fixtures cover both pages, filtered,
  encrypted, truncated, escaped, over-cap and contentless cases.
- Fault-injection suite (`userspace/desktop/tests/test_fault.c`):
  seeded LCG drives 512 rounds of invalid inputs across settings,
  notify, clipboard, downloads, snapshots, firewall, sandbox,
  lifecycle and the performance center; every return must stay in
  the errno range, caps must hold, state machines must reject
  wrong-state sequences, and known-good paths must still succeed
  afterwards — part M FAULT evidence, reproducible from a fixed seed.
- Performance center (`userspace/desktop/src/perfcenter.c`): ring of
  frame-time samples with p50/p95/worst percentiles and a health
  ladder (good/fair/poor/critical) driven by measured thresholds —
  p95 vs frame budget, FPS floor, memory-pressure bands, thermal
  throttle, eco governor and staged-update flags — emitting
  combinable, concrete suggestions (close background, lower detail,
  reduce motion, cool down, wait for update, check memory).
  Host-tested across every band plus ring-wrap.
- Built-in search providers (`providers.c`): the commands provider
  (register/execute with prefix scoring and duplicate rejection) and
  the diagnostics provider (bounded health lines), both honoring the
  pipeline cancellation contract; apps/files/settings providers bind
  at the shell layer — host-tested.
- Update payload verification (`zd_update_verify_payload`): AEAD
  with producer nonce, injected update key and the version bound as
  AAD — tamper, cross-version replay and wrong-key all return the
  explicit failure (section 20).

Not yet implemented (contracts defined, explicit in PHASES.md):
- Shell UI chrome (ZERO Bar, launcher, overview surfaces) on top of the
  session process and its consumers for the automation rules, browser
  rendering engine and tab UI, GPU driver beyond scanout, mouse
  wheel/extended aux protocols, USB HID devices, cloud/device services
  (offline-first behavior, explicit per-device permissions).

### UI condition contract (part A cross-cutting)

`ui.h` owns the shared condition vocabulary mandated by the prompt:
NORMAL / LOADING / EMPTY / ERROR / OFFLINE / PERMISSION_DENIED /
LOW_RESOURCE, plus REDUCED_MOTION / KEYBOARD_NAV / LOCALIZED overlay
modes that never replace the condition.  Labels are `state.*` catalog
keys resolved through the i18n layer (English + Hindi, zero
fallbacks enforced by tests), and negative `ZD_E*` returns map to
conditions with a documented table.  Working per-surface state
machines are NOT redesigned - they map into the contract:

| Surface state machine | Maps to conditions |
| --- | --- |
| filemgr `hist_state` 0/1/2/3 | EMPTY / LOADING / NORMAL / ERROR |
| nav `ZD_NAV_*` | EMPTY / LOADING / NORMAL / ERROR |
| display `ZD_DISPLAY_*` | ATTACHING->LOADING, LIVE->NORMAL, DEGRADED/SUSPENDED->ERROR |
| downloads `ZD_DL_*` | QUEUED/RUNNING->LOADING, DONE/CANCELED->NORMAL, FAILED->ERROR |
| launcher `ZD_LAUNCH_*` | PENDING/RUNNING->LOADING, IDLE->NORMAL, FAILED->ERROR |
| snapshot `ZD_SNAP_*` | EMPTY->EMPTY, CREATING->LOADING, READY->NORMAL, FAILED->ERROR |
| notify lifecycle ACTIVE/DEFERRED | NORMAL while visible (domain lifecycle stays) |
| eco connectivity offline | OFFLINE |
| term parser `pstate` | internal parser phase - not a UI condition |

Surfaces adopt incrementally; the contract + mapping is tested
(`test_ui.c`: transitions, mode flags, interactivity matrix, errno
mapping, localized labels for all seven conditions).

### Stage 5 binding inventory (production status)

Every desktop core introduced in Stage 5 is host-tested with real
assertions (no mocks presented as functionality).  This inventory is
the authoritative list of what still connects those cores to live
subsystems — each pending binding is a documented next step, never a
claim:

| Core | Host evidence | Pending live binding |
| --- | --- | --- |
| Display service + compositor + input router | session process wired to `DISPLAY_PRESENT`/`INPUT_*` + boot certification | multi-GPU beyond the single present path |
| File manager | source-injected listing/sort/select/ops suites | session binds `zeroos_readdir` (ABI file calls 25–50) |
| Terminal | parser/scrollback/SGR suites | PTY or pipe + child spawn for shell output |
| Cloud/device sync | offline queue + permission suites | transport over kernel sockets; real device pairing |
| Firewall engine | first-match/default-deny suites | kernel packet-path hook (userspace policy today) |
| Sandbox profiles | fail-closed gates + audit suites | OS enforcement (seccomp/namespace) hooks |
| Update payload verification | AEAD vectors + tamper/replay suites | key provisioning (OS-injected key; no PKI yet) |
| OCR | capability gate + pluggable-engine path (fixture only) | licensed engine; until then `-95`, no accuracy claims |
| PDF subset | hand-authored fixtures (plain streams) | filtered/encrypted docs stay `-95` (explicit) |
| Media/gaming/ecosystem policy | gate/yield/permission suites | decoder backends, controller hardware, transports |
| Search providers | commands/diagnostics live; pipeline suites | apps/files/settings providers bind to shell services |
| Privacy centre | real-counter aggregation suites | session wiring over live engines |
| Automation/AI/browser/watchdog/lifecycle/governor | suites + CI boot milestones | in-session activation (already exercised in guest CI where noted) |
| UI condition contract | `test_ui.c` (7 conditions, modes, errno map, hi/en labels) | per-surface adoption is a mapping (table above), no surface redesign |
| Real hardware | — | **not run — no claim** (VALIDATION row stays open) |

## 15. Networking
Network stack is independent of desktop UI.
Network manager handles links/configuration.
Firewall enforces policy near the packet path.
Diagnostics expose DNS, route, link and latency information.

## 16. Resource Governor
Every service declares:
priority, memory budget, CPU budget, I/O class, wake policy and suspension policy.

Lifecycle:
DORMANT -> WARM -> ACTIVE -> THROTTLED -> SUSPENDED -> STOPPED.

The governor reacts to:
foreground workload, memory pressure, battery, thermal state, I/O pressure and user mode.

## 17. AI Architecture
AI service has:
request broker -> permission check -> model/backend selector -> context provider -> inference -> action executor.
Model execution may use CPU/GPU/NPU when available.
No model remains actively generating or polling when no request exists.
Status: the local broker (`userspace/desktop/src/ai.c`) implements
request -> permission check -> backend select -> run with explicit
grants (context bits, remote egress, persistence), automatic
remote-to-local downgrade when the egress grant is absent, a bounded
queue with drop counters, injected backend hooks (a missing hook counts
a failure, never a fake success), payload wipe on drain and
dormant-until-submit residency — host-tested in `make desktop-check`.
Model adapters, context providers and action executors attach on top of
these contracts later.  This ZEROOS platform is deliberately separate
from Forge AI.

## 18. Compatibility Architecture
Windows:
Application -> Win32/Win64 API layer -> compatibility runtime -> POSIX-like/native ZEROOS services -> kernel ABI.
Status: the compatibility core (`userspace/compat/`, `make
compat-check`) is implemented and host-tested: explicit process
lifecycle where INSTALLED != RUNNING (start/stop/crash with full
residency release when stopped — dormant when unused), drive-letter and
registry-hive translation with named rejections for UNC paths, NTFS
alternate streams and unknown hives, REG_SZ/DWORD/BINARY validation,
and DLL refcount bookkeeping with case-insensitive singleton loading.
The PE loader and API translation layers build on these contracts;
unsupported constructs fail with explicit diagnostics, never silent
emulation.

Android:
Android application -> Android framework/runtime -> graphics/audio/input/network adapters -> ZEROOS services.
Runtime is separately managed and loaded on demand.
Documented baseline (chosen now, before any claim): AOSP 14
(Android 14, API level 34), primary ABI arm64-v8a, security
bulletin level tracked at integration time; the runtime executes in
an isolated userspace compartment with its own lifecycle and no
privileged ZEROOS IPC by default.
Claims policy: public compatibility statements may list only
devices/versions exercised by the tested matrix.  Current tested
matrix: **empty — no Android application or device is claimed
supported** until CI/real-hardware rows in `VALIDATION.md` record
them.

## 19. Security Architecture
Boot trust, kernel privilege separation, userspace isolation, permissions, process capabilities, encrypted storage (symmetric foundation: RFC 8439 ChaCha20-Poly1305 primitives in `kernel/crypto.c`), firewall, application sandboxing, update verification and recovery.

Security services must remain available under ordinary load and become more conservative under suspicious activity.

## 20. Update Architecture
Use staged updates:
download -> verify -> stage -> preflight -> activate -> health check -> commit or rollback.
System-critical updates should use an A/B or equivalent atomic strategy where storage permits.

## 21. Observability
Unified event model:
boot milestones, kernel events, service events, driver faults, resource pressure, crash reports and update status.
Telemetry must be opt-in where it leaves the device. Local diagnostics should be useful without cloud access.

## 22. Architectural Invariants
- Kernel never trusts userspace.
- Drivers cannot bypass ownership rules.
- Foreground work cannot be starved by background maintenance.
- A feature must have a lifecycle.
- A public ABI cannot change silently.
- Recovery paths are part of the feature, not post-processing.


## 22. Advanced-First Architecture Maturity

The architecture is designed for production maturity from the outset. Stage ordering is dependency ordering, not a justification for simplistic subsystem implementations.

### Kernel

The production kernel architecture must be able to evolve toward:
- SMP/per-CPU execution;
- robust scheduling classes;
- scalable memory allocation;
- demand paging/COW/reclaim;
- robust object lifetime;
- hardened user/kernel boundaries;
- structured diagnostics;
- driver isolation.

### Storage

The production storage boundary must support:
- queued I/O;
- device-specific scheduling;
- page cache/writeback;
- filesystem consistency;
- recovery;
- snapshots;
- encryption/integrity;
- safe update integration.

### Drivers

Drivers are capability-oriented, lifecycle-managed and failure-aware. DMA, IRQ, hotplug, suspend/resume and error recovery are first-class concerns.

### Graphics/Desktop

Graphics must separate hardware, graphics service, compositor, shell and application UI. Frame scheduling, damage tracking, resource ownership, accessibility and crash isolation are architectural requirements.

### Production Boundary Rule

No compatibility runtime, AI subsystem, desktop component or native application may become a hidden dependency of the kernel. Conversely, kernel contracts must be sufficiently mature that userspace does not depend on undocumented implementation details.

## Stage 4 hardware boundary
Legacy PCI mechanism #1 discovery is an observation-only service. It scans
segment 0 and publishes bounded device identity, BAR snapshots and conventional
capability offsets, and sizes BARs (display-class functions excepted). Only
drivers that claim a function (AHCI and NVMe, Stage 3 §12) enable decoding
and bus mastering, map BARs and activate MSI/MSI-X; BARs are never
reassigned and other devices stay unbound. ACPI remains validated
RSDP/root/MADT topology discovery; AML and power management are not present.
DMA/IOMMU, USB, display, audio, and complete network services are not implemented. The networking foundation now includes Ethernet framing, bounded IPv6 extension parsing, IPv4 firewall/UDP validation, a host-tested netif→UDP endpoint callback path, and IPv4/UDP egress with checksums when the caller supplies a resolved next-hop MAC; it still has no concrete NIC, ARP/route resolution, system sockets, or userspace network service. PS/2 keyboard AND mouse input ARE wired: `kernel/input.c` configures the i8042 controller, routes IRQ1 and IRQ12 on IOAPIC/PIC topologies, decodes scancodes and mouse packets into the `input_core` queue (which the driver serializes with its own lock), and serves the INPUT_POLL/INPUT_WAIT syscalls; USB HID, mouse wheel/extended aux packets remain unwired, and event delivery to Ring-3 is proven by boot certification rather than live keystrokes in CI. Ethernet/ARP, IPv4 and bounded IPv6 extension-header parsing, UDP/TCP/DHCP/DNS parser/state helpers, bounded route/flow tables, the host-tested `netif`→IPv4/UDP dispatcher→owner-bound UDP endpoint queue, an owner-scoped DMA callback contract, overlap-checked typed resource registry, and generic driver lifecycle/resource-cleanup state machine are testable foundations, not integrated kernel device-service implementations.
The authoritative detected-versus-operational support matrix is in
[`HARDWARE.md`](HARDWARE.md). Detection must not be represented as support.
