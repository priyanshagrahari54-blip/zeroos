# ZEROOS — Validation and Support Matrix
Status: living evidence ledger. The following Stage 5 material is historical
host/CI evidence, not Stage 10 hardware or production certification.  Every row is backed by a command that runs
in this repository or by a CI run of a recorded commit.  Where evidence
does not exist yet, the row says so — no claim is made without it.

## 1. Evidence classes (definition of done, part M)

| Class | How it is exercised | Where it runs | Status |
|---|---|---|---|
| UNIT | Host unit suites: desktop (ZD_CHECK assertions), compat, hardware cores, crypto RFC vectors | `make desktop-check compat-check hardware-core-test` | Green on demand; counts in section 2 |
| INTEGRATION | Full guest boot: kernel + session + storage/init lifecycles on QEMU | CI workflow `build.yml` (push + pull_request) | Green on recorded commits, section 3 |
| NEGATIVE | Per-suites error paths: EINVAL/ENOSPC/EPERM/ESTATE/EBUSY branches asserted, denial stats | desktop/compat suites | Green |
| FAULT | Injected failures: display degraded present, AI hook failure, launch-hook errno, browser CRASHED/RELOAD, automation action failures + deterministic 512-round API-boundary fault suite (settings/notify/clipboard/downloads/snapshot/firewall/sandbox/lifecycle/perfcenter: errno-range invariant, wrong-state sequences, post-fault sanity, seeded) | desktop/compat suites | Green |
| STRESS | Capacity loops (tab/app/rule/DLL caps) plus `test_stress`: 41 fill/drain browser rounds, 100 vault put/get/forget cycles with 20 lock churns, 20 000 firewall decisions, 24 snapshot turnovers, 10 000 frame recordings, 200 terminal fill/scroll rounds, 100 file-manager navigation generations (84 history evictions accounted), 500 formula re-evals, 100 overview set/remove quadruples (500 relayouts), 100 ecosystem pair/grant/queue/flush/unpair generations — exact end-state accounting | desktop suite | Green (host), guest probes in CI |
| SOAK | Long-duration idle residency (AI dormancy, 0 resident bytes idle, event-driven automation) + seeded 64-epoch churn with generation-isolation and steady-state bounds (clipboard, downloads FIFO totals, notify caps, lifecycle storms, settings schema isolation, perfcenter rings) | desktop suites | Green |
| SECURITY | Permission gates (AI grants, automation permission-first ordering), crypto AEAD/ChaCha20 vectors, constant-time MAC compare, denied counters | crypto tests + desktop suites | Green |
| RECOVERY | Browser crash recovery, service watchdog, display attach/detach, init recovery in guest, rollback contracts | desktop suites + CI boot milestones | Green (host), guest init recovery in CI |
| QEMU | Boot certification block in CI: panic detection, session milestones, storage certification | `build.yml` on every push/PR | Green on every push context since the `dedb23f` revert (`c27ad12`, `56a009f`, `c24c6b2`, `69f053c`, `c8b75ae`, `92e5729`, `fecd7a3`, `19ce0fa`, `eaac99c`, `d0bfc0c`, `d294b66`, `ef384d7`, …); PR context green except the documented `78ef9b5`/`0b2b480`/`98ba0ca` boot-flake rows below |
| REAL-HARDWARE | Physical run of the certified ISO on bare metal | Not available in this environment | **Not run — no claim** |

| PERFORMANCE | Frame pacing/vsync accounting (display tests), governor tier/pressure/effects (governor tests), metrics recorder with interval histogram + percentiles wired into `zd_display_service_present`, FPS monitor frame-time percentiles and budget breaches | desktop suite + session link | Green (host); on-device percentiles pending |
### Stage 5 part compliance matrix

Authoritative prompt-part -> evidence map (see also the binding
inventory in `ARCHITECTURE.md` for pending live integrations):

| Part | Delivered (host-tested unless noted) | Honest limits |
| --- | --- | --- |
| A Graphics/desktop | display/compositor/window/input/wm workspaces+snap+DPI, bar, launcher, Universal Search pipeline + providers, settings schema, notifications, a11y tree, i18n en/hi, lifecycle, watchdog, overview, file manager, terminal core, clipboard, downloads, perf centre, UI condition contract | shell chrome *rendering* and app-framework UI pending; multi-GPU single path |
| B Security/recovery/updates | sandbox (fail-closed), firewall (first-match), capability, vault (AEAD), crypto (RFC vectors), update 9-state + payload verify + rollback, snapshots, privacy centre | kernel enforcement hooks + PKI pending |
| C Windows compat | pe/compat suites (107 checks), loader architecture documented | no Windows claim; runtime pieces pending |
| D Android runtime | AOSP 14 baseline documented | capability absent - not run, no claim |
| E Browser | tab lifecycle + crash recovery + nav/url | renderer binding pending (documented) |
| F AI platform | broker (dormant/permission/demand-driven/wipe-on-drain), separate from Forge AI | no backend linked - injection points only |
| G Study Center | PDF subset, notes, formulas, OCR contract, flashcards, focus sessions, dictionary, AI study assistant | OCR engine pending (-95); PDF filters/encryption -95; dictionary corpus binds later |
| H Media | lawful-source gates, rights, DRM refusal | decoder backends pending; no DRM bypass exists |
| I Gaming | profiles, FPS monitor, overlay policy, controllers contract, low-latency, cooperative yield | controller hardware pending |
| J Cloud/device | pairing grants, offline queue, per-bit permissions | transports pending |
| K Automation | permission-first event/action engine, bounded, auditable | event sources bind in session |
| L Performance/governor | metrics percentiles, pressure ladder, thermal tiers, low-power/reduced-motion | on-device percentiles pending |
| M Validation | section 1 + part M coverage map below | REAL-HARDWARE open |

### Part M checklist coverage map

Each named "Validate" item from `STAGE_5_IMPLEMENTATION_PROMPT.md`
PART M maps to concrete evidence (or an explicit no-claim):

| Prompt item | Evidence |
| --- | --- |
| cold boot | QEMU boot certification block (section 3) |
| desktop session | session milestones: service attached, shell rounds, probe pattern, clean reap (workflow-grepped) |
| multi-window | compositor/window/workspaces suites + boot shell rounds |
| input/display/audio | input router + display service suites (audio: no output path yet — pending, no claim) |
| native app lifecycle | launcher/watchdog/lifecycle suites |
| service crash/restart | watchdog suite (recovery), boot certification |
| Windows runtime lifecycle | compat suites (`compat-check`, 107 checks) + ARCH §18 loader architecture (runtime pending, no running-Windows claim) |
| Android runtime lifecycle | ARCH §18: AOSP baseline 14 documented, capability absent — **not run, no claim** |
| browser tab lifecycle | browser ACTIVE/IDLE/FROZEN/DISCARDED suites + crash/reload fault tests |
| AI activation/deactivation | ai broker dormancy/soak (0 resident idle) suites |
| gaming workload | media+gaming suites (profiles, yield, FPS ring) |
| media workload | media queue/rights suites (lawful sources, DRM refusal) |
| study workload | study flashcard/focus + notes/dictionary/formula suites |
| low-memory/high CPU-GPU | governor pressure-tier + capacity/stress suites |
| thermal pressure | governor `thermal_state` tiers (0/1/2) covered in governor tests; no physical sensor — model-driven only |
| network loss | eco offline queue + permission suites (offline-first) |
| storage errors | filemgr source-errno failed-state suite + guest init recovery in CI |
| update interruption | update suites: cancel pre-activation, verify/download fail-closed, health-fail rollback |
| recovery/rollback | snapshot + update rollback + init recovery suites/CI |
| security boundaries | sandbox/firewall/vault/AI-grant suites (SECURITY row) |
| accessibility | a11y tree/focus/snapshot + keyboard-nav suites |
| English/Hindi localization | i18n suites (en/hi catalogs) |
| resource accounting | metrics recorder + perfcenter percentiles (PERFORMANCE row) |

The authoritative per-core binding inventory (host-tested vs pending live integration vs no-claim) lives in `ARCHITECTURE.md` §14 "Stage 5 binding inventory".


## 2. Measured test counts (host, this branch)

| Suite | Command | Assertions | Failures |
|---|---|---|---|
| Desktop modules (display, compositor, window, input, a11y, i18n, search, settings, notify, lifecycle, watchdog, governor, automation, browser, AI, bar, launcher, capability, metrics, update, url, study, fps, snapshot, vault, nav, firewall, sandbox, clipboard, downloads, providers, perfcenter, fault, soak, pdf, media, gaming, eco, snapshot-bind, privacy, term, filemgr, formula, ocr, overview, notes, dict, assist, ui, stress, integration) | `make desktop-check` | 120907 | 0 |
| Windows compatibility core (lifecycle/paths/registry/DLL + PE validator) | `make compat-check` | 107 | 0 |
| Hardware/driver cores (incl. crypto RFC vectors) | `make hardware-core-test` | per-suite PASS | 0 |
| IPv4 ARP parser/builders + solicited-only expiring neighbor cache | `make hardware-core-test` | 85 | 0 |
| Public userspace ABI | `make userspace-abi-check`, `python3 userspace/tests/abi_consistency.py` | PASS | 0 |
| Userspace runtime contract | `make userspace-runtime-check` | PASS | 0 |
| Full guest kernel build | `make elf -j4` | 0 warnings (Werror) | 0 |

Reproduce all host/build checks locally with `make check` (it builds the
kernel and runs the ABI, runtime, hardware-core, desktop, compatibility,
GPT/ZJFS host-image recovery, and SIMD-safety gates). CI runs `make check`
after producing the boot ISO, then adds QEMU boot, SMP, and AHCI/NVMe
persistence certification. A green
`make check` is not a substitute for the guest or real-hardware gates.

## 3. CI/QEMU evidence by commit (branch `arena/01a0d3b0-zeroos`)

| Commit | Evidence | Result |
|---|---|---|
| `599b262` | PR-context full boot with 7 session milestones (service attached, shell rounds, probe pattern, clean reap) | SUCCESS |
| `ccfa30d` | PR-context full boot with CI diagnostics | SUCCESS |
| `f18c857` | PR-context full boot (network/DHCP core) | SUCCESS |
| `c27ad12`, `ec2d1ce` | Both contexts green immediately after reverting the `dedb23f` task.c guard (experiment in section 3) | SUCCESS |
| `0ec6d9d`, `772a923`, others, `78ef9b5` and `0b2b480` (both PR context only; push context same SHA green, PR merge ref identical to push tree because `main` is a strict ancestor, workflow has no event-conditional steps; the `0b2b480` failure was the QEMU "Boot test" step) | Same-code runs failing with `task owned by multiple CPUs`, `blocking IPC send-wakeup self-test failed`, `userspace init lifecycle failed`, storage-cert panics | FAILED — host-load flake family (identical SHA shows divergent outcomes across contexts) |
| `19ce0fa`, `eaac99c`, `d0bfc0c`, `d294b66`, `ef384d7` (both contexts each) | Batch 8–11 wave: roadmap docs, filemgr+formula+OCR, overview model, binding inventory, stress volume, roadmap batch 11 | SUCCESS |
| `98ba0ca` | PR context only (push same SHA SUCCESS): `ZEROOS PANIC: CPU hot-offline evacuation failed` during SMP teardown after all milestones | FAILED — same-code context diverged; this remains a release risk until stress-reproduced or root-caused |
| `a634746` | Push QEMU persistence run: `task owned by multiple CPUs` while storage was active | FAILED — root-caused: `context.S` cleared the outgoing CPU's handoff quarantine before switching RSP to the destination frame; the fix moves the clear after the stack switch and is being re-certified |

The session milestone strings are grepped by the workflow, so a green
run is machine-verified evidence, not a log skim.

Streak analysis (RESOLVED): the last green run before the streak was
`4a4c528` (04:36Z); every run from `dedb23f` onward — a single-file,
8-line change to `kernel/task.c` — failed on BOTH contexts (0 for 24)
with storage-cert/blocking-IPC panic families.  The revert (`4a282bd`,
landed in `c27ad12`) restored BOTH contexts to green on its first
run, and `ec2d1ce` push stayed green — a clean single-variable
experiment proving the guard caused the streak by swallowing wakes
without the runqueue/kick step.  The guard's intent (no remote steal
during the wait transition) remains valid; it must re-land with the
owner-cancel path actually kicking the owner CPU.  Earlier `772a923`
remains on record as proof that both-red *can* be pure flake — 0/24
versus an immediate return to green is what separated the two.

Follow-up (re-land without swallowing wakes): the guard's goal, no remote
pick of a task inside its prepare→commit window, is now met on the picker
side. `task_wake()` is unchanged: it always sets RUNNABLE, enqueues and
kicks. `runqueue_pick_locked()` refuses any candidate that is current on
another CPU, and the owner's existing `task_block_locked()` RUNNABLE
branch dequeues itself and cancels the block. No wake is dropped on any
exit path, and no owner kick is needed because the owner is already
running with IRQs off and reaches the cancel branch. Confound on record:
`dedb23f` ran while the kernel was still built without
`-mgeneral-regs-only`, so its XMM-clobber exposure depended on compiler
code generation around the changed function. Still, the 0/24 streak is
not attributed to that confound without evidence.
Fixed in the same change: an idle AP could acknowledge CPU-offline from
its idle loop before withdrawing TLB/cpu-local/SMP online state, which
is the `98ba0ca` `evacuation failed (stage=7)` row.

## 4. Stage 5 part support matrix

| Part | Capability | Evidence | Support status |
|---|---|---|---|
| A | Graphics service split + window system | display/compositor/window/input suites, session CI milestones | Supported (userspace services; GPU beyond scanout not yet) |
| A | Shell (ZERO Bar, launcher, search, notify, settings, clipboard, downloads, providers, perfcenter, term, filemgr, overview/expose) | bar/launcher/search/notify/settings/clipboard suites | Core supported; chrome rendering pending |
| B | Security/recovery/updates | crypto suite (linked into vault + update verify), watchdog/lifecycle, capability gate, transactional update machine with AEAD payload verification (tamper/version-replay/wrong-key negatives), AEAD vault, snapshot manager, firewall engine, sandbox profiles | + privacy centre aggregation (read-only risk bands over sandbox/firewall/media/eco/clipboard counters, documented thresholds, domain breakdown) Mostly supported: kernel packet-path binding (firewall), enforcement hooks (sandbox) and asymmetric update key distribution pending |
| C | Windows compatibility | `make compat-check` (incl. PE validator) | Core supported (lifecycle/paths/registry/DLL/image validation); API translation slice pending |
| D | Android runtime | Baseline documented (AOSP 14/API 34/arm64-v8a) + tested-matrix policy in `ARCHITECTURE.md` §18; tested matrix empty | Contract + baseline documented — **no runtime, no claim** |
| E | Browser tabs lifecycle | browser suite (16-tab ladder, crash recovery), strict URL parser, navigation controller (history/back/forward/blocking) | Core supported; engine/UI pending |
| F | AI platform (separate from Forge AI) | AI broker suite, ARCHITECTURE §17 | Broker supported; adapters pending |
| G | Study Center | Flashcard scheduler + focus-session suites (PHASES Phase 6) + PDF subset contract (page tree, Tj/TJ text, explicit unsupported: filters/encryption) + formula engine (bounded recursive-descent: precedence, right-assoc integer powers, \frac/\sqrt, variable bindings, explicit div-by-zero/non-integer-exponent/negative-sqrt errors, depth/arena caps) + OCR capability contract (argument validation, -95 no-engine, pluggable engine path tested with a fixture) + Study notes (bounded notebook: case-insensitive search, pin, capacity -28, body/title validation) + dictionary (injected word source, case-insensitive sort/dedup/lookup, prefix suggestions with cap accounting) | Flashcard/focus/PDF-subset/formula/notes/dictionary supported; OCR = contract only (engine pending, no accuracy claims) |
| H | Media | Lawful-source/no-DRM policy in `PHASES.md` Phase 6 + media policy core (origin registry default-deny, PLAY/CACHE/EXPORT/SYNC rights gates, DRM items refused with counted reasons and no bypass path anywhere) | Policy core supported (host-tested); decoder/playback integration binding pending |
| I | Gaming | FPS monitor + performance-profile suites (budgets, percentiles, breaches) in `PHASES.md` Phase 11 + gaming core (per-app perf profiles, button remap table, overlay/low-latency flags, cooperative yield matrix from measured fps + pressure) | Profiles/remap/policy supported (host-tested); controller hardware + live overlay surface pending |
| J | Cloud/device ecosystem | Offline-first sync queue with explicit per-capability permissions (pairing grants nothing, grant/revoke exact bits, revoke drops dependent pending work, offline flush sends nothing, online flush re-checks pairing+perms), bounded devices/queue | Policy core supported (host-tested); transport + real devices pending |
| K | Automation | automation suite (32 rules, audit ring, permission-first) | Supported |
| L | Performance/resource governance | governor suite (tiers/pressure/effects) + metrics recorder (counters, interval histogram, p50/p99 reads) wired into the display present path | Core supported; on-device percentiles pending |
| A-x | UI condition contract | `ui.h` shared NORMAL/LOADING/EMPTY/ERROR/OFFLINE/PERMISSION_DENIED/LOW_RESOURCE + mode flags, i18n `state.*` labels (en+hi, zero fallback), errno-to-condition map, per-surface adoption table (no surface redesign) | `desktop-check` test_ui suite | Green |
| M | Validation | This document | Evidence ledger live; real-hardware row open |

## 5. Rules for extending this ledger

- A support status may move to "Supported" only with a green command
  or recorded CI SHA in the same row.
- Never claim literal zero CPU/RAM/latency, universal compatibility, or
  untested matrix entries (Section 4 column 3 must stay honest).
- Real-hardware and soak rows close only with actual runs, not
  extrapolation from QEMU.


## 10-stage validation alignment

Validation maps to the 10-stage master plan. Stages 6-10 require explicit evidence for security enforcement, signed and rollback-capable updates, recovery, accelerated GPU/media/browser paths, compatibility runtimes, ZERO AI permission and lifecycle behavior, G560-class hardware, 1080p media where supported, actual network-link performance, thermal/power soak, and release gates.

Every result records implementation status, test type (host/QEMU/hardware), hardware/configuration, commit, command, measured result, and unsupported conditions. Roadmap intent is never validation evidence. Detection is never operational support.

## Stage 10 certification status (2026-09-29)

**Status: BLOCKED — not a production certification.** This evidence update is deliberately separate from implementation/Stage-5 host-suite claims above. A passing host test, QEMU boot, feature detector, or design document is not hardware certification. Release gates that require Lenovo G560-class hardware, real sensors/adapters, real media/runtime workloads, or sustained soak remain open. See `RELEASE_CERTIFICATION.md`, `STAGE_10_REPORT.md`, `HARDWARE.md`, and `PERFORMANCE_BASELINE.md` for evidence and limitations.


## Stage 10 execution record

- Source code commit: `91e30eb7eae80b832f8daecdd1dee8e43424a2f9` (contains the locally discovered and corrected scancode bounds defect).
- Local `make check`: **PASS**, exit 0, 2026-09-29. GCC 12.2.0. Includes freestanding kernel/ELF build and SIMD safety check, userspace ABI/runtime checks, host hardware-core tests, desktop suite (**120,907 checks, 0 failures**), compatibility core (**107 checks, 0 failures**), and storage image/fsck host self-test (PASS). This is host evidence only; test counts are not hardware workloads.
- Local `make`: **BLOCKED at ISO packaging**, exit 2: `grub-mkrescue: No such file or directory`. The ELF had been produced by `make check`; this environment lacks `grub-mkrescue`, `qemu-system-x86_64`, and `xorriso`, so no local guest boot was attempted.
- GitHub Actions: workflow `ZEROOS Build`, run [36610649336](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36610649336), source commit `91e30eb7eae80b832f8daecdd1dee8e43424a2f9`, completed **SUCCESS**. Its steps report success for build, `make check`, image verification, QEMU boot (2-vCPU repeated boots, 4-vCPU SMP, NX-disabled compatibility), and AHCI/NVMe storage persistence certification. This is emulated CI evidence, not Lenovo or bare-metal evidence.
- The documentation-only follow-up commit `fd118ae1e2a141fe25e9df54bc949ac34d06ac3c` also passed [run 36611709655](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/36611709655), including the same build, release-check, QEMU and emulated storage jobs; kernel/test sources are unchanged from `91e30eb`.
- GCC `-fanalyzer`: `make BUILD=build-analyze EXTRA_CFLAGS=-fanalyzer elf` passed, including kernel/hardware-core compilation, final link, and SIMD check. Source-only analyzer checks passed for desktop, compatibility and scancode decoder C files. Attempting full hosted suites with analyzer `-Werror` stopped on aggregate-return uninitialized-value diagnostics at call sites in `userspace/desktop/tests/test_input.c`; the called API implementations initialize all delivery fields. With only that analyzer diagnostic downgraded, desktop (120,907 checks), compatibility (107 checks) and hardware-core suites all passed. These are static-analysis/test results, not proof of absence of defects.
- Attempted to dispatch a fresh run on `arena/01a0ee2b-zeroos`; GitHub returned HTTP 403 `Resource not accessible by integration`. No new run was created. A push to the session branch triggered the current-source run above despite the workflow-dispatch API restriction.
- ASan/UBSan host follow-up initially found an out-of-bounds access in the E0-prefixed scancode table (index 200 into 128 entries). `kernel/scancode_core.h` now has 256 entries and the test asserts this capacity. Re-run of `desktop-check compat-check hardware-core-test` under ASan/UBSan passed with no diagnostics; this fix is included in `91e30eb7eae80b832f8daecdd1dee8e43424a2f9` and the linked CI run validates it.
- A package-install retry could not reach Debian mirrors, so GRUB/QEMU were not installed locally.
- The local environment exposes a KVM virtualized Linux host, 2 logical CPUs, approximately 3.85 GiB `MemTotal`, and a 21.8 GB virtual `vda` marked rotational by the guest. This is not the target machine and does not constitute the 2 GB/HDD/G560 profile. GPU, panel, adapters, firmware, battery and physical thermal data were not available.

### Stage 10 release gate disposition

| Gate | Evidence/result | Disposition |
|---|---|---|
| Build + host release checks | Local `make check` PASS; exact-SHA GitHub build PASS | Tested (host/CI), reproducible artifact comparison not shown |
| Guest boot / kernel / scheduler / SMP | Exact-SHA CI run 36610649336 on `91e30eb7eae80b832f8daecdd1dee8e43424a2f9`: QEMU boot, 2-vCPU repeated + 4-vCPU assertions PASS | Tested in emulation; not hardware-certified |
| Storage crash/recovery | Host ZJFS/GPT recovery PASS; CI AHCI/NVMe persistence step PASS | Tested in host tools/emulation; physical HDD and power-loss certification open |
| 2 GB memory pressure / OOM / reclaim | No 2 GB target run or measured process workloads in this environment | Not tested |
| Network adapters / throughput / packet loss | Host protocol-core suites only; no actual NIC/link measurement | Not hardware-tested |
| Graphics / media / GPU / display | Host policy/core suites and QEMU display milestones only; no physical GPU, panel, decoder or media soak | Not hardware-tested |
| Security enforcement / signed updates | Host policy/crypto tests; architecture records kernel enforcement/PKI gaps | Partial; production gate open |
| Update interruption / rollback / recovery | Host state-machine and snapshot suites; guest init recovery milestone in CI | Partial; installed-system update lifecycle not hardware-tested |
| ZERO AI lifecycle | Host broker policy/dormancy suites; backend not linked | Partial; production integration not tested |
| Compatibility / native apps | Core host tests only; see compatibility matrix | Runtime support unproven |
| Thermal, battery, suspend/resume, long-duration soak | No sensors or physical target available; host soak loops are bounded unit tests | Not tested |
| Performance baseline and regression | No certified physical baseline or comparable previous baseline | Not available |

Passing evidence must not be interpreted as all release gates passing. Stage 10 remains blocked until the open rows have repeatable target evidence or are explicitly taken out of the release support matrix.
