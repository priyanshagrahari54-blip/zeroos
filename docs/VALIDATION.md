# ZEROOS — Stage 5 Validation and Support Matrix
Status: living evidence ledger for Stage 5 (graphics/desktop/platform +
ecosystem capability) work.  Every row is backed by a command that runs
in this repository or by a CI run of a recorded commit.  Where evidence
does not exist yet, the row says so — no claim is made without it.

## 1. Evidence classes (definition of done, part M)

| Class | How it is exercised | Where it runs | Status |
|---|---|---|---|
| UNIT | Host unit suites: desktop (ZD_CHECK assertions), compat, hardware cores, crypto RFC vectors | `make desktop-check compat-check hardware-core-test` | Green on demand; counts in section 2 |
| INTEGRATION | Full guest boot: kernel + session + storage/init lifecycles on QEMU | CI workflow `build.yml` (push + pull_request) | Green on recorded commits, section 3 |
| NEGATIVE | Per-suites error paths: EINVAL/ENOSPC/EPERM/ESTATE/EBUSY branches asserted, denial stats | desktop/compat suites | Green |
| FAULT | Injected failures: display degraded present, AI hook failure, launch-hook errno, browser CRASHED/RELOAD, automation action failures | desktop/compat suites | Green |
| STRESS | Capacity loops (tab/app/rule/DLL caps) plus `test_stress`: 41 fill/drain browser rounds, 100 vault put/get/forget cycles with 20 lock churns, 20 000 firewall decisions, 24 snapshot turnovers, 10 000 frame recordings — exact end-state accounting | desktop suite | Green (host), guest probes in CI |
| SOAK | Long-duration idle residency: AI dormancy (0 resident bytes idle), event-driven automation | contracts asserted by unit suites; continuous runs pending | Partial — contract-level only |
| SECURITY | Permission gates (AI grants, automation permission-first ordering), crypto AEAD/ChaCha20 vectors, constant-time MAC compare, denied counters | crypto tests + desktop suites | Green |
| RECOVERY | Browser crash recovery, service watchdog, display attach/detach, init recovery in guest, rollback contracts | desktop suites + CI boot milestones | Green (host), guest init recovery in CI |
| QEMU | Boot certification block in CI: panic detection, session milestones, storage certification | `build.yml` on every push/PR | Green on `599b262`, `ccfa30d`, `f18c857` (PR context) |
| REAL-HARDWARE | Physical run of the certified ISO on bare metal | Not available in this environment | **Not run — no claim** |
| PERFORMANCE | Frame pacing/vsync accounting (display tests), governor tier/pressure/effects (governor tests), metrics recorder with interval histogram + percentiles wired into `zd_display_service_present`, FPS monitor frame-time percentiles and budget breaches | desktop suite + session link | Green (host); on-device percentiles pending |

## 2. Measured test counts (host, this branch)

| Suite | Command | Assertions | Failures |
|---|---|---|---|
| Desktop modules (display, compositor, window, input, a11y, i18n, search, settings, notify, lifecycle, watchdog, governor, automation, browser, AI, bar, launcher, capability, metrics, update, url, study, fps, snapshot, vault, nav, firewall, sandbox, clipboard, downloads, providers, perfcenter, stress, integration) | `make desktop-check` | 37819 | 0 |
| Windows compatibility core (lifecycle/paths/registry/DLL + PE validator) | `make compat-check` | 107 | 0 |
| Hardware/driver cores (incl. crypto RFC vectors) | `make hardware-core-test` | per-suite PASS | 0 |
| Public userspace ABI | `make userspace-abi-check`, `python3 userspace/tests/abi_consistency.py` | PASS | 0 |
| Userspace runtime contract | `make userspace-runtime-check` | PASS | 0 |
| Full guest kernel build | `make elf -j4` | 0 warnings (Werror) | 0 |

Reproduce all locally with the six commands above; CI runs the same set
plus QEMU boot certification.

## 3. CI/QEMU evidence by commit (branch `arena/01a0d3b0-zeroos`)

| Commit | Evidence | Result |
|---|---|---|
| `599b262` | PR-context full boot with 7 session milestones (service attached, shell rounds, probe pattern, clean reap) | SUCCESS |
| `ccfa30d` | PR-context full boot with CI diagnostics | SUCCESS |
| `f18c857` | PR-context full boot (network/DHCP core) | SUCCESS |
| `c27ad12`, `ec2d1ce` | Both contexts green immediately after reverting the `dedb23f` task.c guard (experiment in section 3) | SUCCESS |
| `0ec6d9d`, `772a923`, others | Same-code runs failing with `task owned by multiple CPUs`, `blocking IPC send-wakeup self-test failed`, `userspace init lifecycle failed`, storage-cert panics | FAILED — host-load flake family (identical SHA shows divergent outcomes across contexts) |

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

## 4. Stage 5 part support matrix

| Part | Capability | Evidence | Support status |
|---|---|---|---|
| A | Graphics service split + window system | display/compositor/window/input suites, session CI milestones | Supported (userspace services; GPU beyond scanout not yet) |
| A | Shell (ZERO Bar, launcher, search, notify, settings, clipboard, downloads, providers, perfcenter, overview) | bar/launcher/search/notify/settings/clipboard suites | Core supported; chrome rendering pending |
| B | Security/recovery/updates | crypto suite (linked into vault + update verify), watchdog/lifecycle, capability gate, transactional update machine with AEAD payload verification (tamper/version-replay/wrong-key negatives), AEAD vault, snapshot manager, firewall engine, sandbox profiles | Mostly supported: kernel packet-path binding (firewall), enforcement hooks (sandbox) and asymmetric update key distribution pending |
| C | Windows compatibility | `make compat-check` (incl. PE validator) | Core supported (lifecycle/paths/registry/DLL/image validation); API translation slice pending |
| D | Android runtime | Baseline documented (AOSP 14/API 34/arm64-v8a) + tested-matrix policy in `ARCHITECTURE.md` §18; tested matrix empty | Contract + baseline documented — **no runtime, no claim** |
| E | Browser tabs lifecycle | browser suite (16-tab ladder, crash recovery), strict URL parser, navigation controller (history/back/forward/blocking) | Core supported; engine/UI pending |
| F | AI platform (separate from Forge AI) | AI broker suite, ARCHITECTURE §17 | Broker supported; adapters pending |
| G | Study Center | Flashcard scheduler + focus-session suites (PHASES Phase 6) | Flashcard/focus core supported; PDF/OCR/formulas/dictionary pending |
| H | Media | Lawful-source/no-DRM policy in `PHASES.md` Phase 6 | Not implemented — policy binding |
| I | Gaming | FPS monitor + performance-profile suites (budgets, percentiles, breaches) in `PHASES.md` Phase 11 | FPS/profiles supported; overlay/controllers/low-latency device support pending |
| J | Cloud/device | Offline-first + explicit-permission contracts in `PHASES.md` Phase 11 | Not implemented — contract only |
| K | Automation | automation suite (32 rules, audit ring, permission-first) | Supported |
| L | Performance/resource governance | governor suite (tiers/pressure/effects) + metrics recorder (counters, interval histogram, p50/p99 reads) wired into the display present path | Core supported; on-device percentiles pending |
| M | Validation | This document | Evidence ledger live; real-hardware row open |

## 5. Rules for extending this ledger

- A support status may move to "Supported" only with a green command
  or recorded CI SHA in the same row.
- Never claim literal zero CPU/RAM/latency, universal compatibility, or
  untested matrix entries (Section 4 column 3 must stay honest).
- Real-hardware and soak rows close only with actual runs, not
  extrapolation from QEMU.
