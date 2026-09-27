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
  explicit failure (section 20). The shell exercises the full pipeline
  in the guest: it provisions the key through the filesystem (write,
  read back, then use), seals a bundle with the same RFC 8439 core the
  kernel crypto self-test validates, and drives
  download→verify→stage→preflight→activate→health→commit with hooks
  that really write `/ram/shell/update.staged`, `.active` and `.good`;
  a second run failed at the health check proves the rollback hook
  removes the activated slot (`-ENOENT` afterwards).

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
| File manager | source-injected listing/sort/select/ops suites + live VFS binding in the session (`OPEN`/`READDIR`/`STAT`/`UNLINK`/`MKDIR`/`RENAME`/`READ`/`WRITE`) with boot milestones and a host replay of the same sequence; preview, copy, rename and move are certified against real files (a copy larger than `ZD_FM_COPY_MAX` is refused, never truncated); batch remove/copy/move walk a **name snapshot** of the selection (each op refreshes the listing and clears the selection), report per-entry results, skip directories unless explicitly allowed, and fail fast when an op is missing | no undo journal for destructive batch ops; copies are bounded by `ZD_FM_COPY_MAX` rather than streamed |
| Terminal | parser/scrollback/SGR suites + live pipe binding in the session: output bytes travel through a real kernel pipe pair (`PIPE_CREATE`/`PIPE_WRITE`/`PIPE_READ`) before the VT parser sees them, with the SGR colour asserted on the resulting cell and an empty-pipe `-EAGAIN` check, plus an interactive line discipline: keystrokes are edited (printable append, BS/DEL erase, CR/LF complete, full-line refusal) and every echo byte travels the same kernel pipe into the same parser, so the rendered row equals the edited line | shell grammar and history (the line discipline and transport are bound; no command parser yet) |
| Shell child processes | the shell fetches the embedded child ELF (`CHILD_IMAGE`, ID 56), spawns it with `SPAWN` passing a real argv and envp, asserts the console lines the child prints for both vectors, its exit status through `WAIT`, and that a second reap returns `-ECHILD` | pipe/fd inheritance into the child (the child still writes to the console, not to a parent-supplied descriptor) |
| Workspaces / snapping / overview | the shell snaps two live windows to the left and right halves and asserts each one's rectangle equals `zd_wm_snap_geometry()` for the monitor the kernel reported (halves side by side and no wider than the monitor), shows that a manager starts with one workspace and refuses the move until the set is grown through `zd_wm_set_workspace_count()`, then moves a window to workspace 1 and follows it (`active_workspace` and `workspace_switches` both checked), then drives the overview from the live window ids: two items, focus cycling that wraps, and removal that relayouts | multi-monitor spanning and drag-to-snap from real pointer input |
| Navigation policy | every URL is validated before the browser sees it: an https and an http URL with an explicit port are parsed and opened as real tabs, while a script scheme, a data scheme, a `file:` URL pointing at a document the shell genuinely has, a hostless URL and an out-of-range port are each refused with the specific reason kept for the user -- a smuggled scheme is not even recorded as a scheme -- and the refused URLs produce no tab at all, which the tab count is checked against afterwards | Actual network fetching, redirects, cookies and origin isolation -- the ABI exposes no networking syscalls |
| Egress policy | the firewall decides whether the AI broker may leave the machine and the shell enforces that verdict as a capability: with no rule in place the broker's outbound flow is denied by default, an outbound L4 flow with no port is rejected as unjudgeable and counted, a template with an inverted port range is refused rather than normalized, and once an allow rule for that one application sits ahead of a catch-all denial the same flow is allowed while both rules are shown to match it -- only the first decides. Denied means the egress grant is withheld and a remote-preferring request is answered locally as a counted downgrade; allowed means the same request keeps its remote backend; removing the rule takes the allowance away and the grant with it | A real packet path -- the ABI exposes no networking syscalls, so the flows are the shell's own outbound intents rather than captured packets -- connection tracking state, and per-process attribution beyond the application id |
| Secret vault | the vault holds real key material -- the very key the update pipeline signs with -- wrapped by the kernel's own certified AEAD rather than a stand-in cipher; the shell asserts the stored blob is nonce, ciphertext and tag of exactly the length that implies, scans the blob to prove no run of the plaintext survived into it, decrypts it back and compares every byte, refuses an over-long secret and an empty name with both counted, wipes the key on lock while the entries stay as ciphertext, and shows that a wrong key and a single flipped ciphertext byte each fail authentication explicitly instead of returning garbage | A key-derivation or key-escrow service, hardware-backed storage, and biometric unlock |
| Recovery snapshots | the snapshot points are real files the shell wrote under `/ram/shell`, captured by hooks that copy the live user-data file and restore it byte for byte: creation reserves a CREATING slot and only the capture hook can promote it to READY, its existence and size are then read back through STAT, a mutated user-data file is restored and its size verified again, a capture the hook refuses leaves a FAILED point carrying the hook's own error instead of a half-registered one, and discarding removes the snapshot file rather than just forgetting the slot; the update pipeline's staging and rollback hooks come from this same manager, so staging captures a snapshot and a rollback restores the newest ready one with the file checked afterwards, while slot activation and commit are deliberately left to the block layer | A/B slot flips, whole-device imaging, and scheduled or incremental capture |
| Media library | the origins are real directories and every item is a real file the shell wrote, verified through STAT before the library lists it; an empty origin and a rights mask outside the defined bits are refused and counted; playing, caching and exporting are gated per origin, so the same title under an origin that holds no rights is refused; protected content is listed but never played and the refusal is counted rather than bypassed; an unregistered origin is refused before rights are even considered; and unregistering an origin takes its rights away with it | playback itself -- no audio device exists in the ABI -- artwork, subtitles and hardware decoding |
| Accessibility + localization | the a11y tree is built from the manager's live stacking order, each node taking its window's own declared role, its real title, and a bound window id, with FOCUSED and HIDDEN derived from the window's actual state; the snapshot is checked against the manager's own keyboard target, focus traversal is walked to the wrap, the announcement's urgency is whatever `zd_notify_a11y_policy()` returns for a notification that is genuinely visible -- critical interrupts, low stays quiet, the rest is announced when idle, checked against that notification's real priority -- the announced text is the focused window's real title, with no screen reader enabled the shell accepts an announcement and queues nothing rather than announcing into the void, and once the reader is on a high-urgency localized announcement is shown to preempt a pending low-urgency one; the shell's own labels come from the catalog with full Hindi coverage asserted for every key and the two locales shown to differ | screen-reader speech output, switch and voice input devices, and more locales than the catalog carries |
| Gaming mode + FPS monitor | every frame in the ring is a real present-and-pace cycle on the kernel clock, so the average, the 100th percentile and the budget-breach count describe this machine -- a tier budget above the quality profile's budget shows up as a breach on every frame rather than being hidden; profile validation, controller remapping and unmapped-button lookups are refused and counted; the cooperative decision is taken from the measured throughput and the governor's real memory pressure, with both ends of the ladder pinned; and gaming mode is asserted not to bypass the governor, since the budget in force is still the one the governor assigned to this tier | real controller input, recording, and an on-screen overlay renderer |
| Study centre | the deck is built from the directory the file manager is really listing -- each card's front is a live entry name and its back that entry's byte size -- a duplicate front is refused rather than silently replaced, the bounded ladder is walked with the day counter the app injects (0 to 1 day on GOOD, two rungs to 7 on EASY, and AGAIN counted as a lapse that resets the interval and lowers the ease), focus mode refuses an overlapping block, counts its block exactly once and refuses ticks after it ends, and the study assistant's request travels through the same AI broker with the shell's backend answering the deck's real card count | PDF, OCR and formula attachments, and a wall-clock day source the ABI does not expose |
| Browser tab lifecycle | the tick is the kernel's own monotonic time in milliseconds measured from the moment the browser is initialised, so IDLE, FROZEN and DISCARDED are each reached only after that much real time has actually passed, and the resident document is a file the shell really wrote -- its bytes are released on discard and restored on reload; a healthy tab that tries to recover, a rewound clock, focus or explicit discard against a crashed renderer, and unknown or closed tabs are each refused with the documented code and every refusal is counted | real navigation, a rendering engine, and network fetches the ABI does not expose |
| AI platform | the broker's backend is the shell's own: a summarize request carries a path, the hook reads that file through the kernel and answers with its size, so the permission gate is proven by a refusal that counts the denial and never reaches the backend; once context selection and persistence are granted the same request is answered `64` -- the size of the document the file-manager step really wrote -- and the retained payload and answer are read back with the resident accounting equal to exactly those bytes; a remote-preferring request without the egress grant is downgraded and still answered locally; the ninth queued request is dropped and counted; and with nothing resident the broker returns to dormant | a real inference backend, streaming output, and remote egress the ABI does not expose |
| System status | the readout is assembled from live sources only: counters are read back out of the metrics recorder and asserted equal to their sources -- the input router's own pointer and button counts, the launcher's launches, and the capability denials from both the launcher and the bar; the interval histogram holds the same eight measured present cycles the performance centre consumed, so the average is non-zero, the percentiles are checked for ordering, and the 100th percentile is asserted to bound the recorded maximum; the hardware figures come from a fresh SYSTEM_INFO read with the version checked, uptime monotonic against the performance step's clock, and free memory never above total | a rendered status panel, per-process CPU accounting, and temperature or battery telemetry the ABI does not expose |
| ZERO bar + quick controls | the strip is sized from the monitor the kernel reported and its layout is asserted to tile it exactly, with the flexible title taking the remainder and the last applet ending at the monitor edge; quick controls pass through the real capability gate -- without `ZD_CAP_SETTINGS_WRITE` for `ZD_SVC_BAR` a toggle is refused with EPERM and the shell hook is never reached, and after the grant the same toggle is applied through the hook, while a control the image cannot honour (no radio) is refused by the shell and recorded as denied; do-not-disturb mirrors into the bar's state; the workspace indicator refuses an index it cannot show, follows the manager after a click is honoured on it, the title is read from the focused window, the badge is the notification engine's own visible count, clicks resolve to real applets, an off-strip click is a miss rather than an action, and keyboard focus walks the ring and wraps | rendered applet painting, per-toggle hit-testing inside the quick cluster, and real pointer delivery into the bar |
| Performance centre | frame samples are measured, not fabricated: each of eight samples is the kernel-clock duration of a real present-and-pace cycle against the governor tier's frame budget, and the assessment runs on those percentiles with the tier budget and the real free-memory percentage as inputs; the assessment's precedence is pinned on both sides -- at 60 fps critical pressure owns the verdict with CLOSE_BG and CHECK_MEMORY, while at this machine's real 20 fps the throughput issue outranks memory by design and still carries the memory suggestions -- a zero frame duration is refused, and under four samples cannot be assessed at all | thermal and power-throttle inputs (no sensor source in this build, so both are reported 0) |
| Settings | the schema-driven registry persists through the filesystem: a changed value is exported to the versioned blob, written to the ramdisk, read back and imported into a fresh registry that recovers it, while a blob corrupted to a non-numeric value aborts the whole import with `-EINVAL` (`import_failures` counted, store untouched) and a value that parses but falls outside the schema range is refused when the staged write is applied, leaving the default in place | multi-user profiles and a schema-version migration path beyond the v1 `ui.scale` rewrite |
| Launcher | launching is a real process creation: the hook spawns the embedded child image and the shell waits for its exit status, `ZD_CAP_LAUNCH_APPS` is checked *before* any process is created (`EPERM` and `cap_denied` without it), a second request in flight is deduplicated with `EBUSY`, a reported run takes the recents slot the empty query returns first, and keyword search returns nothing for a miss | desktop-window placement for launched apps (the child writes to the console rather than into a window) |
| Notifications | the shell drives the whole lifecycle against the kernel monotonic clock from `SYSTEM_INFO`: posts reach the listener callback with their priority, a repeat inside the dedupe window folds into the same id, a critical alert sorts above the rest of the visible set, deferral hides it until the due instant and restores it after, dismissal drops it and is counted, grouping reports a non-empty group, and the accessibility policy is derived from priority | per-app user-configurable quiet hours (the rate limits are the compiled defaults) |
| Downloads | the shell binds the queue's start hook to the VFS: the enqueued source path is streamed into the destination in bounded chunks with progress reported as bytes move, the single-active policy refuses a second transfer with `-EBUSY`, progress regression is rejected, the finished file is stat'ed and byte-compared against its source, and a missing source fails the transfer with the VFS `ENOENT` rather than reporting success | network transports (the source is a local path until a socket ABI exists) |
| Cloud/device sync | offline queue + permission suites | transport over kernel sockets; real device pairing |
| Firewall engine | first-match/default-deny suites | kernel packet-path hook (userspace policy today) |
| Sandbox profiles | fail-closed gates + audit suites, and the session enforces a denial in the kernel: the confined profile denies writes, the shell drops its identity with `SETCRED`, and the VFS then refuses the owner-only file with `EACCES` while a world-readable file still opens — with `SETCRED` proven one-way for an unprivileged caller | namespaces/cgroups-style isolation and per-mount profiles |
| Update payload verification | AEAD vectors + tamper/replay suites, and the session runs the whole pipeline live: the key is provisioned through the filesystem (written, read back, then used), the bundle is sealed with the RFC 8439 AEAD core and verified with the version bound as AAD, and the stage/activate/commit/rollback hooks perform real VFS writes that the step reads back | platform/PKI key injection (the key is provisioned by the filesystem path, not sealed by hardware) |
| OCR | capability gate + pluggable-engine path (fixture only) | licensed engine; until then `-95`, no accuracy claims |
| PDF subset | hand-authored fixtures (plain streams) | filtered/encrypted docs stay `-95` (explicit) |
| Media/gaming/ecosystem policy | gate/yield/permission suites | decoder backends, controller hardware, transports |
| Search providers | commands/diagnostics/files/settings/apps live in the session (files walks the same VFS source as the file manager, bounded depth + per-query directory budget, `full_scans` pinned at 0; apps reads the live window-manager list); pipeline suites; and a **content** provider returns `DOCUMENT` results from a live note store — the store's own case-insensitive matcher decides what matches, a second matching token strengthens an existing row instead of duplicating it, pinned notes outrank unpinned ones, and a body-only match carries the matched window into the label because the ranking core scores label and path. The note-store operations are injected like the files provider's directory source, so an unbound store reports unavailable rather than inventing results | media result kinds once a decoder backend exists |
| Privacy centre | real-counter aggregation suites; the session aggregates a live sandbox profile — allowed reads/writes, denied spawn and device access, and an unknown profile that fails closed — into `zd_privacy_assess`, asserting the denial counts, the risk band and a non-empty audit ring | the clipboard is live too (a sensitive clipping kept out of history is reported as a protected event), while network/content/sync still have no live engine and are passed as NULL (documented "unavailable", never reported as zero-risk) |
| Automation/watchdog/lifecycle/governor | suites + in-session activation on real kernel state: the monotonic clock (syscall 55) drives present pacing, automation cooldowns and watchdog heartbeats; the governor reads real CPU/memory topology and measured free memory; the watchdog restarts the real search service; each step has a boot milestone | AI and browser cores still have no in-session activation |
| Ring-3 time source | `SYSTEM_INFO` (ID 55) exposes `timer_monotonic_ns()` plus topology; the session proves the clock advances and that pacing refuses/accepts around it | wall-clock (RTC) time is still kernel-only |
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
DMA/IOMMU, USB, display, audio, and complete network services are not implemented. The networking foundation now includes Ethernet framing, bounded IPv6 extension parsing, IPv4 firewall/UDP validation, a host-tested netif→UDP endpoint callback path, and IPv4/UDP egress with checksums when the caller supplies a resolved next-hop MAC; it still has no concrete NIC, integrated ARP/route resolution, system sockets, or userspace network service. A standalone IPv4 ARP parser/builder and expiring neighbor-cache helper are host-tested; cache learning rejects unsolicited replies and requires a matching live request, but the helper is not wired to packet ingress/egress. PS/2 keyboard AND mouse input ARE wired: `kernel/input.c` configures the i8042 controller, routes IRQ1 and IRQ12 on IOAPIC/PIC topologies, decodes scancodes and mouse packets into the `input_core` queue (which the driver serializes with its own lock), and serves the INPUT_POLL/INPUT_WAIT syscalls; USB HID, mouse wheel/extended aux packets remain unwired, and event delivery to Ring-3 is proven by boot certification rather than live keystrokes in CI. Ethernet/ARP, IPv4 and bounded IPv6 extension-header parsing, UDP/TCP/DHCP/DNS parser/state helpers, bounded route/flow tables, the host-tested `netif`→IPv4/UDP dispatcher→owner-bound UDP endpoint queue, an owner-scoped DMA callback contract, overlap-checked typed resource registry, and generic driver lifecycle/resource-cleanup state machine are testable foundations, not integrated kernel device-service implementations.
The authoritative detected-versus-operational support matrix is in
[`HARDWARE.md`](HARDWARE.md). Detection must not be represented as support.
