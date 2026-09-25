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
| STRESS | Capacity loops: tab cap (16), app cap (32), rule cap (32), DLL table exhaustion, input flood probes | desktop/compat suites + guest probes | Green (host), guest probes in CI |
| SOAK | Long-duration idle residency: AI dormancy (0 resident bytes idle), event-driven automation | contracts asserted by unit suites; continuous runs pending | Partial — contract-level only |
| SECURITY | Permission gates (AI grants, automation permission-first ordering), crypto AEAD/ChaCha20 vectors, constant-time MAC compare, denied counters | crypto tests + desktop suites | Green |
| RECOVERY | Browser crash recovery, service watchdog, display attach/detach, init recovery in guest, rollback contracts | desktop suites + CI boot milestones | Green (host), guest init recovery in CI |
| QEMU | Boot certification block in CI: panic detection, session milestones, storage certification | `build.yml` on every push/PR | Green on `599b262`, `ccfa30d`, `f18c857` (PR context) |
| REAL-HARDWARE | Physical run of the certified ISO on bare metal | Not available in this environment | **Not run — no claim** |
| PERFORMANCE | Frame pacing/vsync accounting (display tests), governor tier/pressure/effects (governor tests), measured desktop assertion totals | desktop suite | Green (host); on-device metrics pending |

## 2. Measured test counts (host, this branch)

| Suite | Command | Assertions | Failures |
|---|---|---|---|
| Desktop modules (display, compositor, window, input, a11y, i18n, search, settings, notify, lifecycle, watchdog, governor, automation, browser, AI, bar, launcher, integration) | `make desktop-check` | 4465 | 0 |
| Windows compatibility core | `make compat-check` | 86 | 0 |
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
| `0ec6d9d`, `772a923`, others | Same-code runs failing with `task owned by multiple CPUs`, `blocking IPC send-wakeup self-test failed`, `userspace init lifecycle failed`, storage-cert panics | FAILED — host-load flake family (identical SHA shows divergent outcomes across contexts) |

The session milestone strings are grepped by the workflow, so a green
run is machine-verified evidence, not a log skim.

## 4. Stage 5 part support matrix

| Part | Capability | Evidence | Support status |
|---|---|---|---|
| A | Graphics service split + window system | display/compositor/window/input suites, session CI milestones | Supported (userspace services; GPU beyond scanout not yet) |
| A | Shell (ZERO Bar, launcher, search, notify, settings, overview) | bar/launcher/search/notify/settings suites | Core supported; chrome rendering pending |
| B | Security/recovery/updates | crypto suite, watchdog/lifecycle suites, automation permission model | Partial: crypto+watchdog supported; privilege separation/firewall/vault/snapshots pending |
| C | Windows compatibility | `make compat-check` | Core supported (lifecycle/paths/registry/DLL); PE loader + Win32 slice pending |
| D | Android runtime | Baseline + tested-matrix policy in `ARCHITECTURE.md` §18 | Contract only — **no runtime claim** |
| E | Browser tabs lifecycle | browser suite (16-tab ladder, crash recovery) | Core supported; engine/UI pending |
| F | AI platform (separate from Forge AI) | AI broker suite, ARCHITECTURE §17 | Broker supported; adapters pending |
| G | Study Center | Scope contract in `PHASES.md` Phase 6 | Not implemented — contract only |
| H | Media | Lawful-source/no-DRM policy in `PHASES.md` Phase 6 | Not implemented — policy binding |
| I | Gaming | Evidence-required contracts in `PHASES.md` Phase 11 | Not implemented — contract only |
| J | Cloud/device | Offline-first + explicit-permission contracts in `PHASES.md` Phase 11 | Not implemented — contract only |
| K | Automation | automation suite (32 rules, audit ring, permission-first) | Supported |
| L | Performance/resource governance | governor suite (tiers/pressure/effects) | Core supported; on-device metrics pending |
| M | Validation | This document | Evidence ledger live; real-hardware row open |

## 5. Rules for extending this ledger

- A support status may move to "Supported" only with a green command
  or recorded CI SHA in the same row.
- Never claim literal zero CPU/RAM/latency, universal compatibility, or
  untested matrix entries (Section 4 column 3 must stay honest).
- Real-hardware and soak rows close only with actual runs, not
  extrapolation from QEMU.
