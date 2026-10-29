# ZEROOS — Validation and Support Matrix

Updated: 2026-10-29. This is the evidence policy for Stages 1-10; it must not overstate support.

The binding acceptance checklist is [ZEROOS_10_STAGE_HARDENING.md](./ZEROOS_10_STAGE_HARDENING.md). Current estimates and known limitations are in [ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md](./ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md).

## Evidence classes

| Class | What it proves | Current limitation |
|---|---|---|
| UNIT | Host code path exercised | Does not prove kernel execution |
| BUILD | Freestanding objects and kernel link | Does not prove the ISO boots |
| ISO-STRUCTURE | ISO/El Torito/Multiboot structure validated | Does not prove guest execution |
| QEMU | Guest reached every required boot milestone | Does not prove physical hardware support |
| NEGATIVE / FAULT | Denial, malformed input and failure recovery | Must be recorded per subsystem |
| STRESS / SOAK | Repeated/concurrent/long-duration behavior | Coverage is still incomplete |
| REAL-HARDWARE | Physical machine and adapter behavior | G560 matrix not certified here |
| PERFORMANCE | Measured CPU/RAM/I/O/GPU/latency/thermal | No blanket performance claim without workload data |

## Current release status

- ZEROOS has a native x86-64 kernel, Ring-3 session path, build targets, host tests and QEMU boot-test infrastructure.
- PR #16 was merged as `d6b58292df7e00b4b2f15ba22f3a0405035ee549`; its PR workflow passed before merge. Post-merge `main` CI must be checked separately.
- The release gate is `tools/release.sh`; it requires GCC/binutils, GRUB/xorriso and QEMU, runs `make check`, reproducibility checks, ISO structure checks and boot milestones.
- An ISO built with `ZEROOS_ISO_ALLOW_UNBOOTABLE=1`, or a run with `--skip-boot-test`, is not a boot-certified release.
- Physical Lenovo G560 certification is pending. Hardware-dependent claims (Wi-Fi, GPU acceleration, fan control, Bluetooth and sustained 1080p) must remain conditional until measured.
- Standalone disk installation, full dynamic linking, production browser/media stacks, Windows and Android runtime compatibility, and a complete ZERO AI tool/backend execution stack remain incomplete.

## Stage completion rule

No stage is 100% complete until its hardening checklist has executable evidence at the required class. A host test cannot close a QEMU gate; QEMU cannot close a real-hardware gate. Detection, parsing, UI, architecture docs, source-marker checks and mocked APIs do not establish operational support.

## Release blockers

1. Post-merge CI is green on the exact main SHA.
2. `make -j check` is green and deterministic.
3. `tools/release.sh` completes with no boot-test skip.
4. All required boot milestones pass over repeated SMP QEMU boots.
5. Storage persistence and recovery survive repeated guest boots and injected failures.
6. Security, package trust, update rollback and independent recovery have negative/fault evidence.
7. The physical hardware support matrix and performance/soak results are published.
8. Unsupported features and installation limits are explicit in release notes.

## 2026-10-24 — Stable bootable-release publishing guard
- Baseline reviewed: `main` commit `4eb68e4fbfeb02d51fc114f82a05ac641e1acd56`; the workflow allowed release publication on any push.
- Candidate change: require `github.event_name == 'push' && github.ref == 'refs/heads/main'` for the mutable bootable release and run `python3 tools/test_release_publish_guard.py` in CI.
- Regression script checks there is exactly one bootable-release publishing step and that its only `if:` condition exactly matches the main-only policy.
- Prior PR candidate workflow #1420 passed its build, `make check`, QEMU certification, storage persistence, production ISO/desktop smoke test, and the earlier release-policy check. The exact candidate passed workflow #1429, and the documentation-synchronized head `1dbee68abde71e2570d8008f35b0034199d55df0` passed workflow #1435. Neither is a post-merge `main` run.
- Pending release gates: green CI for this exact branch, reviewed merge, successful post-merge `main` workflow, and verification that the stable release asset corresponds to that exact `main` commit.

### Result update — workflow #1429
- Candidate head: `0f17ee1d8da6ea606bb43070faa6514000ed3a84`.
- **PASS:** release guard regression; `make`; clean `make check`; ISO checks; repeated QEMU boot gate; scheduler panic diagnostics; AHCI/NVMe two-boot persistence; production ISO build; production desktop/network smoke test.
- Workflow URL: https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/40054853969
- The release upload step was skipped on the PR event as intended. Merge, post-merge `main` workflow and stable asset/source-commit verification remain pending. The exact `tools/release.sh` wrapper and physical G560 were not separately run in this review.


## 2026-10-24 — Preserve production boot diagnostics on failure
- The production smoke test writes `build-production/serial.log`, `qemu.log`, and `debug.log`, but the normal artifact step is skipped when an earlier smoke assertion fails. The historical failed candidate run #1418 did not retain those production logs.
- Candidate correction: an `if: always()` upload-artifact step captures those three logs with `if-no-files-found: ignore` and 14-day retention, before the success-only release artifact step.
- Regression test: `tools/test_production_boot_diagnostics.py` asserts the artifact step is unique and ordered after the smoke test, runs unconditionally, tolerates absent logs, and matches all three QEMU output paths.
- The script is wired into CI. **The exact commit's workflow and QEMU results remain pending**; prior candidate passes (#1429/#1435) do not validate this new change.

### Workflow test invocation uniqueness — 2026-10-24
The release-policy regression script now also asserts that CI invokes it exactly once, preventing duplicate or missing guard checks. The production-diagnostic retention check remains separate; CI for the exact corrected commit is pending.

## 2026-10-24 — Stable release asset byte verification
- Candidate workflow now downloads the mutable `zeroos.iso` asset after upload and compares it byte-for-byte with `build-production/zeroos.iso`, the ISO produced and boot-tested in the same CI run.
- The release-policy regression script requires both the download and `cmp --silent` checks and asserts download-before-compare ordering, preventing a future workflow edit from silently dropping or reordering stable-alias verification.
- **Pending:** run the exact candidate workflow and observe all QEMU/storage/production smoke gates; after merge, verify the resulting `main` workflow and release asset. This edit alone is not evidence that the remote asset is currently correct.

## 2026-10-25 — Stable release ISO integrity fix merged
- PR #20 was merged as `34010187ef16b4a71b476a22c782cabf4558dc06`.
- Candidate workflow #1464 / run `40058802277`: **SUCCESS**. Steps confirmed successful include the release-policy regression test, production-diagnostic retention test, `make`, clean `make check`, repeated QEMU boot certification, 4-vCPU SMP, NX-disabled boot, failed-IPI recovery, scheduler panic-diagnostic exercise, two-boot AHCI/NVMe persistence, production ISO build, and production desktop/network smoke test.
- In that PR run, `Publish bootable image as a GitHub release` was **SKIPPED by design**, because publishing is restricted to pushes on `refs/heads/main`.
- The regression script verifies the release step has the exact main-only condition and downloads the mutable `zeroos.iso` before running `cmp --silent` against the boot-tested production ISO.
- **Still pending/unverified in this review:** post-merge push workflow result and live release asset comparison for the resulting `main` commit. No claim is made that the mutable release asset has already been refreshed or verified.

## 2026-10-26 — Stable ISO update rollback (candidate)
- The candidate workflow verifies the versioned ISO against `build-production/zeroos.iso` before mutating the stable alias.
- It preserves the prior `zeroos.iso` and attempts restoration if stable upload, download, or byte comparison fails. If no prior stable alias existed, cleanup removes the unverified alias and verifies absence.
- The release regression script checks publication gating, operation ordering, rollback branches, and `bash -n` syntax for the inline shell.
- **Exact candidate CI:** workflow #1484 (run ID `40263294383`) passed the regression checks, `make`, clean `make check`, kernel/ISO verification, repeated QEMU boots, scheduler fault certification, two-boot AHCI/NVMe persistence, production ISO build, and production desktop/network smoke test. Release publication was skipped on the PR event by design. **Still pending:** exercise rollback on an authorized main push if a failure occurs, and verify the live stable asset against the exact post-merge production ISO.

## 2026-10-27 — Release-note ordering guard (candidate)
- Latest observed `main` SHA: `fa6fb6e750843ee14dc0b1f9151c20052741f337`; no combined status entries were returned for that exact SHA, so its status is not inferred.
- Main workflow #1543 (`40322921742`) passed for ancestor SHA `5d89992d6c78a2a19108385c3fc4979fa3cfe19e`. The job-step record shows `tools/release.sh`, QEMU boot test, storage persistence certification, production desktop/network smoke, and release publication all succeeded in that run. It does not certify later commits.
- Identified defect: final release notes were edited after versioned-asset upload but before stable `zeroos.iso` byte verification.
- Candidate patch defers final notes on an existing release until after stable ISO verification; a newly created release uses explicit pending-verification notes. Regression test checks that pending path exists and the final edit occurs after the verified boundary. The inline release shell remains syntax-checked by the existing test.
- **Pending:** exact candidate CI, QEMU/storage/production boot result for candidate, reviewed merge, and post-merge main publication/asset verification. No new test pass is claimed in this entry.

## Evidence update — 2026-10-27 (PR #24 merged)
- Release-note ordering fix merged to `main` at `1cd514b641d148b4094c0de524925dbd323b92eb`.
- Candidate workflow #1553 / run ID `40440843398` passed the release-policy and diagnostic-retention regression steps, build, clean `make check`, kernel/ISO verification, repeated QEMU boots, scheduler fault certification, two-boot AHCI/NVMe persistence, production ISO build, production desktop/network smoke test, and diagnostic preservation.
- Release publication was skipped on the PR event as intended. The post-merge `main` workflow and live stable `zeroos.iso` comparison must still be checked separately. This candidate result is not a claim of post-merge publication.


## Evidence refresh — 2026-10-28

- Current `main` reviewed at `7c45183332f895ad778f08b98458f580b18a0067`; no status/workflow result was returned for this exact SHA. Treat current-head CI as unverified.
- Latest completed main workflow inspected: #40491492164 for older SHA `b1c848f193940bfd18691f942daf6e32d0dae210`. Its job passed build, clean `make check`, repeated QEMU boots, 4-vCPU SMP, NX-disabled, failed-IPI recovery, scheduler fault, two-boot AHCI/NVMe persistence, production desktop/network smoke and release publication. Stable ISO comparison passed with SHA-256 `9d9c5594a4669936e2ac37320a025365018b785768f73d6d52aebfb56a818529`. Do not attribute this run to current HEAD.
- Candidate boot-gate fix on `fix/boot-gate-fail-closed-20261028` removes swallowed QEMU exits, clears logs before every boot, requires debug-port `B`/ `L` markers each iteration, and adds regression coverage. Exact candidate CI has not yet been verified.
- Required candidate checks: `python3 tools/test_boot_gate_fail_closed.py`, `python3 tools/test_release_publish_guard.py`, `python3 tools/test_production_boot_diagnostics.py`, clean `make check`, all QEMU boot/fault gates, two-boot storage persistence, production desktop/network smoke, and release artifact integrity. Physical-hardware certification remains a separate gate.

## Evidence refresh — 2026-10-28: affinity/hot-offline regression

- Current `main` is `731e9e8856976beb2e717eef169ded72f4917741`; the connected status lookup returned no check entries for this exact SHA. Treat current-head CI as unverified.
- Candidate `fix/scheduler-affinity-offline-race-20261028` adds an in-lock online-target revalidation in `task_set_affinity`, a deterministic policy self-test, a required boot milestone, and `tools/test_task_affinity_offline_race.py`.
- Required exact-candidate checks: `python3 tools/test_task_affinity_offline_race.py`, `python3 tools/test_boot_gate_fail_closed.py`, `python3 tools/test_release_publish_guard.py`, `python3 tools/test_production_boot_diagnostics.py`, clean `make check`, repeated 2-vCPU QEMU boots, 4-vCPU SMP, NX-disabled boot, failed-IPI recovery, scheduler panic diagnostics, two-boot AHCI/NVMe persistence, production desktop/network smoke, and stable-ISO release integrity.
- Exact candidate head `8fe3ee0a97788b762e990ef253238eb51c49b008` passed ZEROOS Build workflow #1610 (run ID `40628325243`) and Stage 1 Scheduler CI #31 (run ID `40628325242`). Passing checks include the new affinity regression, release-policy and diagnostic-retention tests, clean `make check`, repeated QEMU boot matrix including the new required affinity/offline marker, scheduler panic fault, two-boot AHCI/NVMe persistence, production ISO and desktop/network smoke. Release publication was skipped because this was a PR event. The current `main` workflow remains unverified; workflow #40491492164 is older evidence for `b1c848f`, not current HEAD. Physical hardware and the optional 600-second scheduler soak remain separate evidence gates.

## Candidate validation note — 2026-10-28: remote affinity owner

- Baseline `main`: `218cbc43eaeba24612a91838ee381460aa2212e8`. The connected exact-SHA status/workflow lookup returned no entries, so current-main CI cannot be classified from that lookup.
- Candidate `fix/affinity-remote-owner-reschedule-20261028` fixes the RUNNING-task affinity decision to use the actual current-task owner CPU instead of the caller's CPU. It also fails closed if a RUNNING task has no owner under `task_lock`.
- Added host regression assertions to `tools/test_task_affinity_offline_race.py` and deterministic policy checks in the kernel scheduler self-test. Exact candidate head `f6450de619a4ea052b84d9394fe0d4dd58d98236` subsequently passed ZEROOS Build workflow #1626 (run ID `40630820065`) and Stage 1 Scheduler CI #47 (run ID `40630819947`).
- Candidate Build workflow #1626 passed all four host regression steps, `make`, clean `make check`, kernel/ISO verification, repeated QEMU boot matrix, scheduler fault certification, two-boot AHCI/NVMe persistence, production ISO build, production desktop/network smoke, and diagnostic preservation. Stage 1 Scheduler CI #47 also passed. Release publication was skipped on the PR event as intended. This does not verify current `main` or the live release asset; physical hardware and the optional 600-second soak remain open.

## Post-merge evidence — 2026-10-28: remote affinity owner fix

- PR #30 merged as `acae3eba7b2f9c6cf2c52038d7b5606c28f4a077`. The fix makes a RUNNING task's affinity update use its actual owner CPU rather than the setter CPU.
- Candidate workflow #1626 (run ID `40630820065`) passed on code head `f6450de619a4ea052b84d9394fe0d4dd58d98236`; Build workflow #1632 (run ID `40631843123`) passed on the final PR tip `a533d6ecdd6db4345ed473ecd9240ce287257256`. Stage 1 Scheduler CI #47 and #53 also passed. The Build runs passed regression tests, `make`, clean `make check`, kernel/ISO verification, repeated QEMU boot matrix, scheduler fault certification, two-boot AHCI/NVMe persistence, production ISO and desktop/network smoke. PR release publication was skipped as intended.
- Post-merge `main` CI and live stable ISO verification remain unconfirmed by the available status lookup. The 600-second Stage 1 soak and physical hardware are separate outstanding evidence gates.

## Candidate regression — 2026-10-29 (pending)
- Added a guard to `process_create` so a non-null parent must be a correctly aligned object in the static process table before `state`, `pid`, or child-count fields are read.
- Added `tools/test_process_parent_validation.py` to check source ordering, the runtime invalid-pointer probe, required boot marker, and CI placement.
- Added a certification probe using pointer value `1`, a sentinel PID that must remain unchanged on rejection, and required marker `ZEROOS: invalid process parent pointer rejected.`.
- **Not yet validated:** candidate CI, `make check`, QEMU boot milestone, and the production ISO smoke tests must pass on the exact branch head. No pass is claimed here.

## Candidate regression — 2026-10-29 (pending)
- Added a guard to `process_create` so a non-null parent must be a correctly aligned object in the static process table before `state`, `pid`, or child-count fields are read.
- Added `tools/test_process_parent_validation.py` to check source ordering, the runtime invalid-pointer probe, required boot marker, and CI placement.
- Added a certification probe using pointer value `1`, a sentinel PID that must remain unchanged on rejection, and required marker `ZEROOS: invalid process parent pointer rejected.`.
- **Not yet validated:** candidate CI, `make check`, QEMU boot milestone, and production ISO smoke tests must pass on the exact branch head. No pass is claimed here.

## Process-parent provenance regression — 2026-10-29 evidence
- Candidate code head `f767841d9e6dd98df07005d44ebaf086472512e0`: ZEROOS Build workflow #1651 **SUCCESS** and Stage 1 Scheduler CI #72 **SUCCESS**.
- Workflow #1651 passed the new `tools/test_process_parent_validation.py` step, all earlier source regression checks, `make`, clean `make check`, kernel/ISO checks, ordered QEMU boot milestones (including invalid-parent rejection), scheduler panic-diagnostics certification, two-boot AHCI/NVMe persistence, production ISO build, production desktop/network smoke, and diagnostic/artifact steps.
- PR event skipped actual release publication by design. Main SHA `12cdca17f8359fdb6884ec6f1b5b67cc2a78071b` has no returned status entries from the connected lookup; post-merge main CI and live stable ISO verification remain unconfirmed. After this evidence-note update, rerun CI for the final documentation tip before merge.

## 2026-10-29 — Process API pointer-provenance candidate (pending)
- Main SHA reviewed: `b99e5705fa56558637b1983f5f45992d7ae509cb`. The combined-status endpoint returned no checks for this SHA; the connected workflow-run helper filters to PR-triggered runs, so current-main CI remains **unverified**.
- Candidate branch `fix/process-api-pointer-provenance-20261029` checks that direct process-pointer API arguments identify an aligned static process-table entry before reading fields. The new regression checks 20 APIs, verifies a boot-time probe uses address `1`, and requires marker `ZEROOS: invalid process API pointers rejected.` before the production ISO smoke gate.
- The exact candidate's Python regression, clean `make check`, repeated QEMU milestones, scheduler fault tests, two-boot AHCI/NVMe persistence and desktop/network smoke are not yet verified. Do not mark this candidate passed until its own workflow finishes.
- The merged parent-pointer guard's candidate evidence is recorded above; it is not evidence for this broader candidate. Post-merge main CI, live stable ISO verification, long soak and physical hardware remain separate gates.
