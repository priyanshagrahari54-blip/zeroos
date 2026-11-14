# ZEROOS — Validation and Support Matrix

Updated: 2026-11-14. Latest main HEAD `129707fa0c1f1d92ac8d2e972d09f0254437f9b7` contains the PR #50 code tree. PR #50 head `c536f785b89c5ebcfb7f2d80352cb8c20fd0e18e` passed Build #1920 (run `43360750832`) and Stage 1 Scheduler #341 (run `43360750870`); seven changed code/workflow/test blobs were verified identical between PR head and main. Build #1920 verified 3/3 QEMU boots with 110/110 milestones per boot, SMP4, NX-disabled, failed-IPI recovery, scheduler panic diagnostics, AHCI/NVMe two-boot persistence, production ISO generation and production desktop/network smoke. Its release-publication step was skipped for the PR event.

## Current main acceptance — 2026-11-14

- Main HEAD `129707fa0c1f1d92ac8d2e972d09f0254437f9b7` is the PR #50 merge. The PR head `c536f785b89c5ebcfb7f2d80352cb8c20fd0e18e` has successful full Build #1920 and Stage 1 #341. The seven PR #50 changed code/workflow/test blobs were independently compared and match main, so the tested code tree is the code tree currently on main.
- Build #1920 completed native x86-64 compilation, reproducible `make check`, kernel/ISO verification, 3/3 sequential QEMU boots with 110/110 milestones, SMP4, NX-disabled, failed-IPI recovery, scheduler panic diagnostics, AHCI/NVMe two-boot persistence, production ISO generation and desktop/network smoke.
- Build #1920 was a PR event and skipped GitHub Release publication. The last verified stable ISO remains the Build #1918 artifact for `7b73f7a`, SHA-256 `e78d8f4c2d74c9a356cbdfc04497636792543963bebcd7477a1e2ab06b1fb789`. Post-merge main publication and current stable/versioned byte equality for `129707f` are not verified.
- Candidate `fix/process-thread-attach-ownership-20261114` adds bounded process-thread list validation and rejects cross-process/duplicate thread attachment before consuming reservations or changing counters. It adds a guest negative probe, required milestone and source regression. Candidate Build/QEMU is not yet verified.


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

- Latest code tree on main `129707f` is QEMU-validated through PR #50 Build #1920; PR #50 Stage 1 #341 also passed. Build #1920 did not publish a GitHub Release because it was a PR event.
- Last independently verified stable release publication: Build #1918 for `7b73f7a`, SHA-256 `e78d8f4c2d74c9a356cbdfc04497636792543963bebcd7477a1e2ab06b1fb789`.
- The post-merge main Build/release result for `129707f` and the current stable/versioned ISO SHA equality are not verified. Do not label the `7b73f7a` asset as current-head certified.
- Physical Lenovo G560 certification remains pending. QEMU evidence does not certify physical Wi-Fi, GPU acceleration, fan control, Bluetooth, USB or sustained media workloads.
- Standalone disk installation, full dynamic linking, production browser/media stacks, Windows/Android runtime compatibility and a complete ZERO AI tool/backend execution stack remain incomplete.


## Stage completion rule

No stage is 100% complete until its hardening checklist has executable evidence at the required class. A host test cannot close a QEMU gate; QEMU cannot close a real-hardware gate. Detection, parsing, UI, architecture docs, source-marker checks and mocked APIs do not establish operational support.

## Release blockers

1. **P0 — post-merge release freshness:** main code tree `129707f` has passing PR Build/QEMU evidence, but Build #1920 skipped release publication; verify the post-merge main Build and stable/versioned ISO byte equality for this exact tree.
2. **P0 — candidate attachment fix:** `fix/process-thread-attach-ownership-20261114` needs exact PR-head Build, clean `make check`, the required guest milestone and repeated QEMU/storage/production gates before merge.
3. **P1 — scheduler:** 600-second/four-vCPU soak with latency, fairness, hot-offline and wakeup-race evidence.
4. **P1 — physical hardware/recovery:** G560 boot/device matrix and physical power-loss/recovery.
5. **P1 — power/install:** clean ACPI S5 and standalone disk installer/recovery path.
6. **P2 — product expansion:** native desktop hardware integration first; browser/media/AI/compatibility only after kernel, storage and driver gates.
7. Unsupported features and the legacy BIOS live-session/no-installer limitation must remain explicit in release notes.

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

## 2026-10-30 — Process API pointer-provenance merge and validation evidence

- PR #33 merged to `main` as `4e3691c64079d6a50f3a0cf5230336b49dfb0bee`. The patch validates process-table pointer provenance before dereference across 20 direct process-pointer APIs. It adds `tools/test_process_api_pointer_validation.py`, a boot-time invalid-pointer probe, and required milestone `ZEROOS: invalid process API pointers rejected.`.
- Exact candidate head `2c7a0e9294890341f969c614cde2bae00243907b`: ZEROOS Build workflow #1677 / run ID `40813814987` **SUCCESS**; Stage 1 Scheduler workflow / run ID `40813814986` **SUCCESS**.
- Build workflow step evidence: release-policy, production-diagnostic retention, fail-closed QEMU boot gate, scheduler-affinity race, invalid-parent pointer and process-API pointer regression scripts all passed; native build, reproducible release gate, kernel/ISO verification, QEMU boot test, scheduler fault certification, two-boot q35 AHCI/NVMe persistence, production ISO build, production desktop/network smoke and diagnostic preservation all passed. The GitHub Release publication step was **skipped** because the run was a pull-request event.
- Post-merge combined-status lookup for main SHA `4e3691c64079d6a50f3a0cf5230336b49dfb0bee` returned an empty status list. The connected workflow-run helper filters to PR events; therefore it cannot establish the push-triggered main workflow result. Main CI and live stable ISO byte-identity remain **unverified**.
- These results certify the candidate workflow, not physical hardware. The 600-second Stage 1 soak and Lenovo G560/device matrix remain separate validation gates. No completion estimate increase.


## 2026-10-31 — Atomic process user-copy fix (candidate; not yet validated)

- Current `main` HEAD: `9f7b715ee9c15506514c4e563ea78801d9c77e9d`. Exact-head combined-status lookup returned no entries; current-main CI is unknown. The connected workflow-run lookup returns PR-triggered runs only.
- Last inspected successful candidate Build workflow #1686 / run ID `40994113537`: build job completed successfully with release-policy, diagnostic-retention, fail-closed boot, scheduler-affinity, invalid-parent and process-pointer regressions; native build; clean `make check`; repeated QEMU boot gate; scheduler fault certification; two-boot AHCI/NVMe persistence; production ISO; production desktop/network smoke; and diagnostic artifact preservation. GitHub Release publication was skipped on the PR event. This is candidate evidence, not current-main publication evidence.
- New finding: `kernel/syscall.c` and `kernel/exec.c` validated user ranges via a process API that released `process_lock`, then translated/copied through the process page tables without the lock. Concurrent unmap/remap could invalidate the translation before the physical copy.
- Candidate fix adds `process_address_space_copy_from/to`; each operation validates the complete range, translates pages, and copies while holding `process_lock`, which serializes against process address-space map/unmap/destroy paths. Syscall and exec helpers now use this API.
- Candidate regression coverage: `tools/test_process_user_copy_atomic.py` checks lock/order/call-site routing and CI wiring; guest boot certification checks a 16-byte cross-page read, rejects a range with an unmapped tail, verifies a cross-page write/readback, and requires marker `ZEROOS: process user-copy mapping stability self-test passed.`.
- **Pending exact candidate checks:** run the new Python regression, `make`, clean `make check`, repeated QEMU boot milestones, SMP4/NX-off/failed-IPI/panic diagnostics, two-boot AHCI/NVMe persistence, production ISO build, and production desktop/network smoke. No pass is claimed yet for this candidate.
- **Separate release blockers:** post-merge `main` workflow and live stable `zeroos.iso` byte comparison; 600-second scheduler soak; physical hardware matrix. No completion estimate increase.

## 2026-11-01 — Atomic process user-copy fix: merged and candidate-validated

- PR #35 merged to `main` as `6521dcf3e2b7d0caef25cb47515b8572f0358fcc`. PR head: `40d0184debf7acd80b5358eb82fa4834e5e0ea29`; Build #1702 checked out integration merge commit `59654aa954f56a56eaa1ff7ea444b3a6e486da45`.
- ZEROOS Build workflow #1702 / run ID `41164323843`: **SUCCESS**. The new `tools/test_process_user_copy_atomic.py` ran 5 tests successfully. Existing release, diagnostics, fail-closed boot, affinity, parent-pointer, and process-pointer regressions passed; `make`, clean `make check`, kernel/ISO checks, repeated QEMU boot, SMP4, NX-disabled, failed-IPI recovery, scheduler panic diagnostics, two-boot q35 AHCI/NVMe persistence, production ISO build, desktop/network smoke, and diagnostic preservation all completed successfully.
- ZEROOS Stage 1 Scheduler workflow #123 / run ID `41164323767`: **SUCCESS** (native x86-64 build and Stage 1 scheduler certification).
- The PR workflow skipped GitHub Release publication by design. No post-merge `main` workflow result was returned for merge SHA `6521dcf...`; the helper used for workflow lookup is PR-event-only, and combined-status lookup returned an empty list. Do not mark post-merge main CI green or infer live release publication.
- Outstanding acceptance gates: verify a push-triggered workflow for the exact post-merge main SHA; verify the live stable `zeroos.iso` byte-for-byte against that run's production ISO and confirm release notes; execute and retain the 600-second Stage 1 soak; complete physical Lenovo G560/device certification. Open PRs #26/#27 and broader #14 require rebase/review; #14 overlaps the merged user-copy implementation and must be reconciled rather than merged blindly.


## Evidence update — 2026-11-03: main CI passed, but stable release was overwritten by an older run

- Exact main SHA `28112603ebbd6a4bfa6cc93a6c6cb0be84cc2f3a`: Build #41265715805 **SUCCESS** and Stage 1 Scheduler #41265715839 **SUCCESS**. The Build job passed native build, clean `make check`, kernel/ISO verification, three sequential QEMU boots, SMP4, NX-disabled, failed-IPI recovery, scheduler fault certification, two-boot AHCI/NVMe persistence, production ISO, production desktop/network smoke, and release publication.
- Same-run release log reported stable `zeroos.iso` SHA-256 `eea7d77da1be0dc00b9b8a6dbb11035714d60eb9eb06ead3ce29037bb89c43a1`. However, older run #41265707574 (commit `3ba0dbe`) completed its stable update later and reported `c80f4270d9f34be9f0d6ba57e93ee5ddb77a3ddb08c97273f7386db8ba26afd0`. The current Release API stable-asset digest is `c80f...`, and release notes name `3ba0dbe`; the versioned latest-main asset `zeroos-2811260.iso` has digest `eea7d77...`. The current stable alias therefore does not match the latest-main production ISO.
- Cause: multiple push-triggered main workflows were allowed to publish concurrently. Candidate fix `fix/serialize-stable-release-publish-20261103` adds workflow-level per-ref concurrency (`cancel-in-progress: false`) and a stale-`GITHUB_SHA` check before mutating the stable alias. `tools/test_release_publish_guard.py` asserts both the serialization and stale-run ordering.
- Candidate CI has not yet run for this commit. After merge, require a fresh main push workflow and independently confirm that the release notes name its exact commit and that live `zeroos.iso` digest equals that run's production ISO SHA-256.
- The 600-second scheduler soak, physical Lenovo G560/device matrix, and remaining driver/security/recovery/desktop/browser/media/AI/compatibility requirements remain open. No completion percentage increase.


## Follow-up validation snapshot — 2026-11-03

- PR #38 head `ad59ca140c69d28d5d2462e090419d13a96b7cf1`: release-policy regression including new concurrency and stale-SHA assertions **PASS**; diagnostic retention, QEMU fail-closed, scheduler affinity, invalid-parent, process-pointer and user-copy regressions **PASS**; native build, reproducible release gate and kernel/ISO verification **PASS**. PR Build #41574414947 Boot test remains in progress; same-SHA push Build #41574386407 Boot test remains in progress. Stage 1 Scheduler #41574414962 **PASS**. Do not treat these partial results as full QEMU certification.
- PR #37 Build #1740 and Stage 1 #161 are now complete and successful. Full QEMU variants, scheduler fault diagnostics, AHCI/NVMe persistence, production ISO and production boot smoke passed. Separate source review still finds the direct `context_switch_ex` frame-resume `iretq` path in `kernel/context.S` lacks the GS restore helper; keep PR #37 open pending that correction and regression.
- Main QEMU diagnostics from #41265715805 confirm desktop/compositor/input and storage tests, but report no supported network device. The smoke gate's pass is not NIC-driver certification.
- Current stable release still reflects older commit `3ba0dbe` (digest `c80f...`) rather than latest-main versioned image `2811260` (digest `eea7...`). PR #38 must pass the full boot gate and then a post-merge main run must republish/verify the stable asset and notes.

## Final evidence update — 2026-11-04: merged release fix and verified publication

- PR #38 merged as `84f8a5cf23e03098419d92231a25908174e3c21f`. `.github/workflows/build.yml` now uses non-cancelling per-ref concurrency and a latest-main SHA guard after immutable versioned-ISO verification and before stable alias/final-note mutation. `tools/test_release_publish_guard.py` passed in candidate and post-merge CI.
- Candidate Build #41574953029 (PR) and #41574947638 (push) both **SUCCESS**; Stage 1 Scheduler #41574953139 (PR) and #41574947653 (push) both **SUCCESS**. Full Build steps passed, including release/diagnostic/boot/scheduler/process regressions, native build, clean `make check`, kernel/ISO checks, repeated QEMU, SMP4, NX-off, failed-IPI, scheduler panic diagnostics, two-boot AHCI/NVMe persistence, production ISO and production boot smoke. PR publication was skipped as intended.
- Exact post-merge main SHA `84f8a5cf...`: Build #41766774251 **SUCCESS**; Stage 1 Scheduler #41766774267 **SUCCESS**. Main Build completed all above gates and published the release.
- Main release job explicitly reports: `stable release ISO matches the boot-tested production ISO`, SHA-256 `297ac5218d9c4e9b4b59cb85638a087fb2633a569c91bd40b8dc23e0e7a6016e`. GitHub Release metadata reports the same digest and 12,369,920-byte size for `zeroos-84f8a5c.iso` and mutable `zeroos.iso`; release notes name `84f8a5c`. This closes the prior stable-alias mismatch for the verified release.
- QEMU production serial shows RTL8139 attach, DHCP lease `10.0.2.15`, and first-hop ARP resolution. This does not certify physical NICs or the Lenovo G560.
- Remaining: 600-second scheduler soak, physical hardware/device matrix, PR #37 GS return-path correction/review, PR #36 range-unmap review, and later storage recovery/driver/security/desktop/browser/media/AI/compatibility requirements. No completion estimate increase.

## Latest follow-up — 2026-11-04: exact current release verified

- Latest tested main snapshot: `9d77ad3befb6c782b548020d67d6af38401533b2`. Build #41767825881 and Stage 1 Scheduler #41767825887 both **SUCCESS**.
- Main Build passed release regression, diagnostic retention, fail-closed QEMU gate, scheduler affinity, process-parent/API-pointer/user-copy regressions, native build, clean `make check`, ISO verification, repeated QEMU variants, scheduler fault diagnostics, two-boot AHCI/NVMe persistence, production ISO, production boot smoke and release publication.
- The workflow log reports a successful stable-vs-production byte comparison with SHA-256 `cebb728732f1cd04e2058182842e53c5d988cc0463fc7bb2ac4b8217314da413`. Release API metadata reports that same digest and 12,369,920-byte size for `zeroos-9d77ad3.iso` and `zeroos.iso`; notes name `9d77ad3`.
- QEMU production serial confirms RTL8139 attach, DHCP lease `10.0.2.15`, and gateway ARP resolution. Do not extend this to physical hardware certification.
- The final documentation-only follow-up skips CI and does not change the OS code or the published image. Remaining: 600-second scheduler soak, physical G560/device matrix, PR #37 GS return-path source gap, PR #36 range-unmap review, and later driver/security/recovery/desktop/browser/media/AI/compatibility work. No completion increase.

## Evidence update — 2026-11-05: complete GS ownership on direct frame resume (candidate)

- Current `main` HEAD is `49eb15d2aad3e96bb67efe9808c25ac40937e756`, a documentation-only `[skip ci]` commit. The latest OS-bearing snapshot is `9d77ad3befb6c782b548020d67d6af38401533b2`; Build #1748 / run `41767825881` and Stage 1 Scheduler #169 / run `41767825887` both completed successfully.
- Build #41767825881 passed existing source regressions, native x86-64 build, clean `make check`, kernel/ISO verification, repeated QEMU boot, SMP4, NX-disabled and failed-IPI boots, scheduler fault diagnostics, two-boot AHCI/NVMe persistence, production ISO, production desktop/network smoke, diagnostic preservation and GitHub Release publication.
- Release API metadata reports versioned `zeroos-9d77ad3.iso` and stable `zeroos.iso` with the same SHA-256 `cebb728732f1cd04e2058182842e53c5d988cc0463fc7bb2ac4b8217314da413` and size 12,369,920 bytes. The main Build log confirms the stable asset was downloaded and byte-compared with the production ISO; release notes identify `9d77ad3`.
- Existing PR #37 candidate `cb92e9b9aa944bfaaa5be8dbce48800bc193494e` passed Build #41573450851 and Stage 1 Scheduler #41573450849. It establishes GS ownership in `boot/isr.S:isr_common`, but source review found that its separate cooperative `kernel/context.S:.Lresume_frame` path can also resume a saved Ring-3 interrupt frame via `iretq` without selecting the target GS state.
- Candidate branch `fix/complete-gs-frame-resume-20261105` ports the reviewed GS entry/return changes onto current main and adds `cpu_interrupt_gs_prepare_frame()`: it verifies that kernel per-CPU GS is active and selects user GS only for a Ring-3 target immediately before the direct frame restore. No C call follows the Ring-3 `SWAPGS`.
- Added source regressions for the direct `.Lresume_frame` call order and fail-closed target-CPL selection; the Build workflow runs `python3 tools/test_interrupt_gs_entry.py` before toolchain installation. The exact new candidate head has not yet completed CI/QEMU; no new boot pass is claimed.
- PR #36 range-unmap preflight remains a separate unmerged candidate. The 600-second scheduler soak and physical Lenovo G560/device matrix remain outstanding. QEMU RTL8139/DHCP evidence does not certify a physical NIC. Browser/media/AI/compatibility remain roadmap work.
- Overall completion estimate is unchanged; source assertions and documentation do not earn feature-completion credit.


## Candidate validation correction — 2026-11-05

- First exact-head Build #41960677907 stopped at `Verify Ring-3 interrupt GS ownership`: the inherited regression expected `timeout --kill-after=5s`, but current-main `tools/boot_test.sh` used plain `timeout`. This was a test/harness contract mismatch, not a compiler or guest-kernel failure; build and all later QEMU steps were skipped in that run.
- The candidate now ports the bounded QEMU termination behavior into `tools/boot_test.sh`: `timeout --kill-after=5s` and explicit acceptance of timeout status 124 or forced-kill status 137. This prevents a paused `-no-shutdown` QEMU process from hanging the boot gate and makes the existing regression match the actual harness.
- The updated exact-head Build/QEMU run must still complete. No native build, QEMU boot, storage or production-smoke pass is inferred from the failed first run.


## Candidate validation correction — second CI attempt (2026-11-05)

- Build #41960835677 stopped in `Verify QEMU boot gate fails closed`, before native build or QEMU. The newly bounded timeout command added `--kill-after=5s`, but the test's fake `timeout` executable treated that option as the duration and attempted to execute `1s`; the regression then saw the wrapper's traceback instead of the expected exit diagnostic.
- Updated `tools/test_boot_gate_fail_closed.py` to strip the optional `--kill-after=...` argument before invoking its fake child, match the revised 124/137 diagnostic, and add a case proving forced-kill status 137 is accepted only as a timeout outcome while boot milestones are still mandatory.
- The next exact-head Build must rerun both the fail-closed boot-gate suite and the GS ownership suite before the compiler/build/QEMU stages. No kernel build or QEMU result is claimed for Build #41960835677.

## Current-main evidence and Multiboot2 handoff range — 2026-11-05

- Current reviewed main SHA: `c3c4eb6ba1a508ff8258d4cf96864317de2da6b4`.
- Candidate Build #41963710252: job completed SUCCESS. Verified steps include clean `make check`, kernel/ISO verification, repeated QEMU boot, scheduler panic diagnostics, q35 AHCI + NVMe persistence over two boots, production ISO generation, and production desktop/network smoke. GitHub Release publication was skipped because the run was PR-triggered.
- Stage 1 Scheduler #41963710086: completed SUCCESS. This is candidate evidence; a separate post-merge main push run was not returned by the available workflow-run lookup.
- Current-main combined-status lookup returned no entries. Treat post-merge main CI as unverified, not failed and not passed.
- Current main's stable release asset has not been independently compared with a production ISO from `c3c4eb6ba1a508ff8258d4cf96864317de2da6b4`; release publication remains an open gate.
- Boot parser defect found in `kernel/memory.c`: the Multiboot2 information-block start was incorrectly capped at 16 MiB. The boot identity map covers 1 GiB and the allocator's supported physical window is 512 MiB. The 16 MiB cap belongs on `total_size`, not on the starting physical address.
- Candidate branch `fix/multiboot-info-window-20261105` separates aligned start-address validation from the 16 MiB size limit, checks the full block fits within the 1 GiB identity map while clipping allocator reservations to the first 512 MiB, adds a boot-time boundary self-test (32 MiB start, final identity-mapped page, invalid alignment/size/window cases), and reserves the parsed block.
- New host/source regression: `tools/test_multiboot_info_range.py`; Build workflow invocation is before toolchain installation. New required guest marker: `ZEROOS: Multiboot2 info range policy self-test passed.`
- Exact candidate CI/QEMU for this change is pending. Until it completes, the change is implemented on the candidate branch but not boot-certified.
- Remaining release gates: post-merge main workflow, stable ISO byte comparison for current main, 600-second scheduler soak, and physical hardware certification. Do not equate QEMU RTL8139/DHCP with physical NIC support.

## PR #40 first-run failure and correction — 2026-11-05

- Build #1770 failed only in the new `Verify Multiboot2 information range handling` step. It stopped before toolchain installation, kernel release build, `make check`, and QEMU; no boot result can be inferred from this run.
- The failing assertion required the runtime boundary self-test to cover a 17-byte, non-8-byte-aligned Multiboot2 total size. The C self-test initially omitted that case. The candidate now includes it, and the Python assertion normalizes whitespace.
- Stage 1 Scheduler #203 completed SUCCESS on current PR tip `7043444b45c5cd068651b7a2bb054851ae5d18cb`, including native kernel build and Stage 1 certification. Build #1782 for that tip remains pending with no job started; Build #1770 is the earlier regression failure. The Stage 1 pass does not substitute for full release/QEMU validation.
- Corrected candidate head: `c2010a2175ea6f73b0057143def8d4172fc54d65`. Exact-head Build/QEMU retry is pending. PR #40 is not merge-ready until the full required gate passes.
## Latest PR #40 workflow snapshot — 2026-11-05

- Stage 1 Scheduler #203 completed SUCCESS on PR tip `7043444b45c5cd068651b7a2bb054851ae5d18cb`, including native kernel build and Stage 1 certification.
- Build #1770 failed at the new Multiboot2 regression before compiler installation and QEMU; the missing 17-byte runtime case and formatting-sensitive assertion were corrected.
- Build #1782 was cancelled during PR close/reopen. Latest Build #1789 for docs tip `beaba6139cc06426ce5dc0243f6978a48e842e1f` remains pending with no job started. Stage 1 #209/#210 had not both completed at last lookup.
- No full Build, clean `make check`, QEMU boot, storage, or production smoke pass is established for the corrected PR #40 head. Do not merge until a completed exact-head Build passes.
- Current main post-merge CI and stable ISO verification remain separate release gates.
## Final candidate-head validation snapshot — 2026-11-05

- Code/test head: `9c14a4a4fed9d0f3a8e21e3f5c2ecbf45a819a99`. Multiboot2 info start is checked against the 1 GiB identity map; total size is capped/aligned at 16 MiB; `reserve_range()` clips reservations to the allocator's first 512 MiB.
- Build #1810 was pending with no job started; Build #1809 was cancelled. Build #1770 failed in the earlier version of the new regression before toolchain installation/QEMU; the missing 17-byte size-alignment case was added afterward.
- Stage 1 #230's native kernel build and scheduler certification steps both completed SUCCESS, but the overall workflow conclusion was CANCELLED. Stage 1 #231 remained pending with no job started.
- No completed full Build/QEMU/storage/production-smoke result exists for the final candidate. Local clone was blocked by DNS and local QEMU is unavailable.
- Keep PR #40 unmerged until a completed exact-head Build passes the new regression, native release build, clean `make check`, QEMU boot matrix, storage, and production smoke.
## PR #40 latest CI result — 2026-11-05

- Stage 1 Scheduler #231 completed SUCCESS on code/test head `9c14a4a4fed9d0f3a8e21e3f5c2ecbf45a819a99`: native x86-64 kernel build and Stage 1 scheduler certification both passed.
- Build #1810 was cancelled during overlapping PR validation events. Latest Build #1816 for docs tip `9c1d3b1d5d9fc04c0857056b2fe5dea6a0cd9928` was pending with no job started at last lookup.
- The corrected Multiboot2 source regression has not yet completed in a successful Build workflow. Full QEMU boot, storage persistence, production ISO and desktop smoke remain unverified for this candidate.

## 2026-11-06 — Multiboot2 handoff-range fix merged (PR #40)

- Merge commit: `f20ea855fc30baf54cdc5d45915d357266f2ae6e`.
- The new `tools/test_multiboot_info_range.py` regression passed in candidate Build #1822 (run ID `41968388714`). The required guest milestone is `ZEROOS: Multiboot2 info range policy self-test passed.`.
- Build #1822 job completed SUCCESS and passed all listed source regressions, native `make`, clean `make check`, kernel/ISO verification, repeated QEMU boot, scheduler fault diagnostics, two-boot q35 AHCI/NVMe persistence, production ISO generation, and production desktop/network smoke. The release-publication step was skipped on the PR event by design.
- Stage 1 Scheduler #243 (run ID `41968388659`) completed SUCCESS, including native x86-64 build and Stage 1 scheduler certification.
- Evidence class: **candidate BUILD + QEMU + STORAGE + production-smoke** for the final PR tree before merge. This is not a post-merge main push result and does not certify physical hardware.
- The combined-status lookup for merge commit `f20ea855fc30baf54cdc5d45915d357266f2ae6e` returned no entries. The connector's commit-workflow lookup returns PR-triggered runs only, so exact post-merge main CI remains unverified.
- Release blocker: the PR run skipped GitHub Release publication. The previously recorded stable ISO comparison was for older commit `9d77ad3`; verify the live `zeroos.iso` against the production ISO from a successful post-merge main run before declaring the current release aligned.
- Other open validation: 600-second scheduler soak and physical G560/device certification. Do not increase stage or overall completion percentages.


## 2026-11-07 — main Build #1823 and release-alias check

- Latest main ref: documentation HEAD `9a0d4d30d5cef5ca4842033c6a75abd589b1c93b`; latest code-bearing commit: `f20ea855fc30baf54cdc5d45915d357266f2ae6e`. The compare endpoint reports the 12 intervening commits touch only `MASTER_INDEX.md`, `VALIDATION.md`, `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md`, and `ZEROOS_MASTER_ROADMAP.md`.
- Push workflow Build #1823 (`42152327282`) on `f20ea855`: SUCCESS. The job step summary records PASS for source regressions, native build, clean `make check`, artifact validation, repeated QEMU boot, scheduler diagnostics, AHCI/NVMe two-boot persistence, production ISO, production desktop smoke, and release-artifact upload/publication steps.
- The release log records: `skipping stable release update for stale main commit f20ea855...; main is 9a0d4d30...`. The immutable versioned asset was verified, but the stable alias was not changed.
- GitHub Release metadata: versioned `zeroos-f20ea85.iso` SHA-256 `e40ac5276a83dc24f45866024a2871aa1ce7b6eaafc3dda64a4e22e9bee764d2`; stable `zeroos.iso` SHA-256 `ef092a8c9cb2c7b98ec8af20ce53239b8d3b36cc4525ee833ef94376e2404e53` (same digest as `zeroos-c3c4eb6.iso`). Release notes still point to `c3c4eb6`. Therefore the current stable alias is not verified as the latest main code image.
- Candidate release-guard correction allows only documentation-only advances (`docs/**`, `**/*.md`) and still rejects any other changed path. The regression test creates a temporary Git history and verifies both cases. This candidate has not yet passed GitHub Actions; no merge is authorized by this snapshot.
- Post-merge acceptance: successful push Build for the merged main SHA; inspect QEMU/storage/desktop step conclusions; download both versioned and stable ISO; compare each with the production ISO; verify release notes name the same code-bearing commit.
- Physical Lenovo G560/device matrix and 600-second scheduler soak remain unverified. No overall completion increase.


## 2026-11-08 — merged release guard and post-merge gate

- PR #41 merged as `fff32e9b35fa61f1911d43d45fc1b99aabfbdd57`. It changes only the stable-release freshness predicate, its regression test, and master documentation; no kernel behavior changed.
- Candidate Build #1826 / run `42332516298` and push Build #1825 / run `42332496757` both completed SUCCESS. The exact job summaries show success for release guard regression, diagnostics/QEMU gate regressions, Multiboot2 regression, native build, clean `make check`, three standard QEMU boots with all 104 milestones, SMP4, NX-disabled, failed-IPI, scheduler panic diagnostics, AHCI/NVMe two-boot persistence, production ISO, and production desktop/network smoke. The PR release-publication step was skipped by its event condition, as expected.
- Stage 1 Scheduler #247 / run `42332516393` and #246 / run `42332496715` completed SUCCESS.
- Post-merge main Build #1827 / run `42442854152` was still `in_progress` at last inspection; Stage 1 Scheduler #248 / run `42442854128` completed SUCCESS. The candidate runs are not substitutes for this main run.
- The release API was queried after merge. At that point `zeroos.iso` still had SHA-256 `ef092a8c9cb2c7b98ec8af20ce53239b8d3b36cc4525ee833ef94376e2404e53` and notes still said `c3c4eb6`; this is older than the verified `zeroos-f20ea85.iso` asset (SHA-256 `e40ac5276a83dc24f45866024a2871aa1ce7b6eaafc3dda64a4e22e9bee764d2`). Current stable-release alignment remains OPEN until Build #1827 completes publication and the downloaded stable/versioned assets are byte-compared with its production ISO.
- The new release guard permits a build SHA to publish after documentation-only main commits, but continues to skip if any non-documentation path changed. The main run must show the exact guard decision, versioned ISO comparison, stable ISO comparison, and final release-note update before the release blocker is closed.
- 600-second scheduler soak and physical G560/device certification remain unverified. No completion estimate increase.


## 2026-11-09 — post-merge main release and boot evidence

- **Main Build #1827** (run `42442854152`, commit `fff32e9b35fa61f1911d43d45fc1b99aabfbdd57`): SUCCESS. The job-step API reports success for the release-policy, diagnostic-retention, QEMU fail-closed, scheduler-affinity, GS-ownership, process-pointer/user-copy/range-unmap, and Multiboot2 regressions; native build; reproducible release gate; kernel-image verification; boot test; scheduler panic-diagnostics; AHCI/NVMe two-boot persistence; production ISO; production desktop/network smoke; and release publication.
- **Main Stage 1 Scheduler #248** (run `42442854128`): SUCCESS. Compiler/binutils setup, native x86-64 kernel build, and Stage 1 scheduler certification all completed successfully.
- The Build #1827 release log records the documentation-only freshness decision and the final stable-ISO comparison. The versioned `zeroos-fff32e9.iso` and mutable `zeroos.iso` assets are both 12,369,920 bytes with SHA-256 `18cfe872ab7c946d6e4adc75e411043971d5e3878eb4a4df499e4320b94af15a`. Release notes identify `fff32e9`. This closes the stable-alias mismatch for the exact production ISO from Build #1827; it does not certify physical hardware or an installer.
- The latest `main` documentation head `1b2caeb7` is a `[skip ci]` documentation-only successor to the release-tested code commit. No executable kernel changes occurred after `fff32e9`; the kernel-source baseline remains `f20ea855`.
- **New candidate (not yet validated):** `tools/boot_test.sh` accepted statuses 124/137 without proving that GNU `timeout` had reached the requested observation deadline. The candidate requires the `timeout --verbose` TERM-at-deadline marker before accepting boot milestones and preserves `timeout.log`. Regression tests simulate early status 124 and 137 and require rejection. Wait for the exact PR Build before claiming the new guard passes; no new QEMU boot is claimed for this candidate.
- Remaining release/product gates: run the 600-second four-vCPU scheduler soak; physical Lenovo G560 and device matrix; crash/power-cut recovery and physical storage validation; real NIC/USB/graphics support. Browser/media, Windows/Android compatibility, and full AI execution remain later-stage work.



## 2026-11-09 — first boot-timeout candidate CI correction

- Candidate PR #42 Build #1829 (run `42558109355`) and PR Build #1830 (run `42558124850`) stopped before compiler installation/native build/QEMU at the existing `tools/test_interrupt_gs_entry.py::test_qemu_boot_timeout_force_kills_paused_emulator` source assertion.
- The failure was an outdated test expectation for the exact command string. The candidate correctly added `LC_ALL=C timeout --verbose` and the deadline TERM-marker check, but the cross-suite GS/boot regression still asserted the previous unprefixed `timeout --kill-after=5s` command.
- Updated the cross-suite regression to assert the new verbose command, the retained timeout log, and that the deadline marker is checked before serial milestones. Removed an unused test-module import and clarified fresh-log comments.
- This is a test-contract mismatch, not a kernel compile, guest boot, or QEMU failure. No native build or QEMU result is claimed for Build #1829/#1830. A new exact-head Build is required.


## 2026-11-09 — merged QEMU observation-deadline validation

- PR #42 merged as 1a57d5cebe0e1eff87fd10450be841708de4e01d. The change is in the boot-test harness and regressions, not kernel source.
- Candidate exact head 3f36bb5e0eb9de9bcd16df39f912f382a37716b2: Build #1833 / run 42558262851 and Build #1834 / run 42558267310 both SUCCESS. Their exact job-step summaries show PASS for the fail-closed QEMU regression, native x86-64 build, reproducible release gate, kernel-image verification, boot test, scheduler diagnostics, AHCI/NVMe two-boot persistence, production ISO and production desktop/network smoke. PR-event release publication was skipped as expected.
- tools/test_boot_gate_fail_closed.py: 7 tests passed in Build #1833. The complete source-regression sequence also passed. Stage 1 Scheduler #254 / run 42558262771 and #255 / run 42558267278 both passed.
- QEMU evidence from Build #1833: boot iterations 1/3, 2/3 and 3/3 each report all 104 milestones present with no failure markers, plus 32-bit entry and long-mode markers. Final summary: boot_test: PASS - 3/3 boots reached every milestone.
- Artifact zeroos-boot-diagnostics ID 13648807591 contains a fresh timeout.log with GNU timeout's deadline TERM marker. The guard checks this marker before evaluating serial milestones; early 124/137 regression cases fail closed.
- Post-merge main Build #1835 / run 42560302805 on 1a57d5d remains in progress at the reproducible release gate. Main Stage 1 Scheduler #256 / run 42560302640 passed. Full post-merge QEMU/storage/desktop/release conclusions remain pending.
- The last fully verified stable/versioned release pair is still for fff32e9, both SHA-256 18cfe872ab7c946d6e4adc75e411043971d5e3878eb4a4df499e4320b94af15a. The stable alias has not yet been verified for 1a57d5d.
- Physical G560/device certification and the 600-second scheduler soak remain unverified. No completion percentage increase.

## 2026-11-09 — post-merge main acceptance

- Main Build #1835 / run 42560302805 completed SUCCESS on 1a57d5d. Main Stage 1 Scheduler #256 / run 42560302640 completed SUCCESS.
- Exact Build #1835 job-step conclusions are SUCCESS for the fail-closed QEMU regression, all existing source regressions, native x86-64 build, reproducible release gate, kernel-image verification, boot test, scheduler panic-diagnostics, storage persistence, production ISO, production desktop/network smoke, artifact retention and release publication.
- Boot log: 3/3 QEMU iterations passed all 104 milestones; 32-bit entry and long-mode markers present on each. Artifact 13649770651 contains timeout.log with the supervisor's deadline TERM marker.
- Storage artifact 13649770651 contains serial-disk1.log and serial-disk2.log. Both logs show SATA/NVMe discovery, driver tests, /data persistence counters 1 and 2, persistent-file validation, Ring-3 fsync probe and Stage 3 completion; no panic/failure markers. The storage step concluded SUCCESS. A transient ERR-trap diagnostic appears between sequential boots; retained per-boot logs establish the actual result.
- Production QEMU smoke reached the interactive desktop session, attached RTL8139, obtained DHCP 10.0.2.15 and resolved the first-hop gateway. This does not establish physical-device compatibility.
- Release log confirms main advanced only through documentation files after the tested SHA and permitted publication of 1a57d5d. Versioned asset zeroos-1a57d5c.iso and stable zeroos.iso are each 12,369,920 bytes with identical SHA-256 082bd06046aad31f8f9d2d39b20b5361b46d0a44cd39ddda26bff07a321e58cd. Release notes identify 1a57d5c.
- The ISO verifier's optional pycdlib cross-check was unavailable and produced a generic warning; the actual QEMU boot gate passed 3/3. Do not treat the optional-tool warning as a boot failure.
- Closed: post-merge main boot/release verification for 1a57d5d. Open: 600-second scheduler soak, physical G560/device matrix, physical storage recovery/power-cut validation, and actual USB/graphics/NIC hardware support. No completion percentage increase.

## 2026-11-10 — current main review and ordered-writeback candidate

- Reviewed `main` HEAD `e1392c094e1ea58594cee6907001858fec361f96`. The latest code-bearing commit is `1a57d5cebe0e1eff87fd10450be841708de4e01d`; subsequent commits in the inspected history are documentation-only.
- Current main Build #1835 / run `42560302805`: SUCCESS. Main Stage 1 Scheduler #256 / run `42560302640`: SUCCESS. Build #1835 reports native build and clean `make check`, 3/3 QEMU boots with all 104 milestones, scheduler panic diagnostics, two-boot AHCI/NVMe persistence, production ISO, production desktop/network smoke, and release publication all successful.
- Stable `zeroos.iso` and versioned `zeroos-1a57d5c.iso` are both 12,369,920 bytes with SHA-256 `082bd06046aad31f8f9d2d39b20b5361b46d0a44cd39ddda26bff07a321e58cd`. The release is a GRUB/SeaBIOS legacy-BIOS El Torito live ISO; it is not a disk installer.
- New review finding: `zj_commit_locked` ignored ordered page-cache writeback errors before writing the metadata journal. This violates the ordered-data commit contract and can expose metadata for data that did not reach storage.
- Candidate correction: propagate the first ordered writeback error, abort the ZJFS transaction before journaling, and force the filesystem read-only. Runtime regression injects EIO at the precise allocated data-block LBA and checks that the transaction is not committed and the file is absent after remount. Source regression and new boot milestone are wired into Build CI.
- **Candidate branch CI and QEMU are pending.** The new source and runtime regressions are not yet counted as passed; the main evidence above remains the only accepted release evidence.
- Open gates: 600-second/four-vCPU scheduler soak; physical G560 and device matrix; physical power-loss/recovery and USB/graphics/NIC certification; clean ACPI S5 shutdown. No completion percentage increase.

## 2026-11-10 — current main review and ordered-data durability barrier candidate

- Reviewed `main` HEAD `3f42def2a0695c53f6d9b6d37d807ce627feaae0`. PR #43 is closed and its ordered-data writeback error-propagation change is present in main. The tested PR head was `fb1e973b0b41ebeb273c37350bd0bb0d785c1b09`; comparing it to the main merge commit reports one additional commit and no changed-file delta.
- PR Build #1837 (run `42747010883`, head `fb1e973b0b41ebeb273c37350bd0bb0d785c1b09`) and Stage 1 Scheduler #258 (run `42747011119`) completed successfully. Build #1837's successful steps include the new ordered-writeback source regression, native x86-64 build, reproducible release gate, kernel/ISO verification, the three-boot QEMU gate (105 required milestones), scheduler panic diagnostics, two-boot AHCI/NVMe persistence, production ISO generation and production desktop/network smoke. The GitHub Release publication step was skipped because this was a PR run.
- The post-merge push-triggered main workflow for `3f42def2a0695c53f6d9b6d37d807ce627feaae0` was not returned by the available commit-workflow/status queries. Do not label post-merge main CI as passed based on the PR run. The last release snapshot recorded in the repository documents remains `1a57d5c`; a release for `3f42def2a0695c53f6d9b6d37d807ce627feaae0` has not been verified in this review.
- Additional storage-integrity finding: ordered page-cache writeback completion does not itself prove data reached stable media. The commit protocol needs a device FLUSH barrier after all ordered data writes complete and before any journal metadata/commit record is submitted. The later journal FLUSH cannot guarantee that ordering if the device reorders volatile-cache writes.
- Candidate branch `fix/zjfs-ordered-data-flush-barrier-20261110` adds that pre-journal FLUSH, aborts/read-only on barrier failure, injects EIO into the first FLUSH in a RAM-disk runtime test, and verifies after remount that the uncommitted file is absent. A source-contract regression and required QEMU boot milestone are included. Historical pre-merge status; superseded by PR #44, main Build #1855 and the verified stable ISO.
- Open gates remain: 600-second/four-vCPU scheduler soak; physical Lenovo G560 and device matrix; physical power-loss/recovery; USB/graphics/NIC certification; clean ACPI S5 shutdown. Overall estimate remains ~43%; no completion percentage increase.

## 2026-11-10 — PR #44 merged; main CI and stable ISO verified

- PR #44 was squash-merged as `100c8e2cf4cdd049c4bf5565b5eebbc0f5284651`. The latest code-bearing main commit is `100c8e2cf4cdd049c4bf5565b5eebbc0f5284651`; subsequent documentation updates do not change executable code.
- Main push Build #1855 (`42752361937`) completed successfully. Main Stage 1 Scheduler #276 (`42752361843`) completed successfully. The Build job passed the source regressions, native x86-64 build, reproducible release gate, kernel-image verification, QEMU boot test, scheduler panic-diagnostics fault certification, two-boot AHCI/NVMe persistence, production ISO generation, production desktop/network smoke, artifact preservation and GitHub Release publication.
- The main Build log records that main had advanced only through documentation files and that it published the boot-tested code snapshot `100c8e2cf4cdd049c4bf5565b5eebbc0f5284651`. It ends with: `stable release ISO matches the boot-tested production ISO`.
- The main boot-diagnostics artifact was downloaded and inspected: 3/3 normal QEMU boots reached all 106 required milestones; the new ordered-data FLUSH-EIO self-test marker is present; the four-vCPU log reports 4 CPUs online and scheduler certification passed.
- The eight-round crash/power-cut matrix passed. Round 2 explicitly exercised the torn journal-superblock fallback (`journal superblock invalid; checking descriptor/commit`) and recovered with fsck clean. All eight rounds report recovered/fsck clean.
- AHCI/NVMe two-boot persistence passed: the /data counter advanced from 1 on boot 1 to 2 on boot 2; both runs emitted the persistence-check and Stage 3 completion markers. Production desktop/network smoke passed.
- Release assets `zeroos-100c8e2.iso` and `zeroos.iso` are both 12,374,016 bytes and have identical SHA-256 `4628a75807bb9a074ce48eaf8e627997fd86b9c33c63201d66dacc785b498e80`. The Build release artifact's extracted production ISO independently hashes to the same value. Release notes identify commit `100c8e2` and describe a legacy BIOS El Torito live session; there is no disk installer.
- Candidate Build #1851 initially failed in crash round 2 because a torn journal-superblock write caused recovery to return `-SE_UCLEAN`. Descriptor/commit fallback recovery was added; Build #1854 passed the complete candidate matrix, and main Build #1855 then passed and published the verified image. Preserve this failed-run/correction history.
- No P0 build/boot/stable-release blocker remains for this snapshot. Remaining engineering gates: 600-second/four-vCPU scheduler soak; physical Lenovo G560 and device matrix; physical power-loss/recovery; USB/graphics/NIC certification; clean ACPI S5 shutdown; and a disk installer if installation (rather than live boot) is required.
- Overall completion estimate remains ~43%. No completion credit is assigned for CI or release-pipeline work.


## 2026-11-10 — current open engineering queue

Open PRs remain, but their branches are stale and not merge-ready. Ahead/behind counts below are measured against the code-bearing main commit 100c8e2; later documentation-only commits are excluded:

| PR | Workstream | Ahead | Behind | Current action |
|---|---|---:|---:|---|
| #26 | Stage 1 QEMU boot evidence | 6 | 52 | Rebase; retain only still-missing evidence |
| #27 | Scheduler certification | 1 | 51 | Reconcile with main; do not merge stale snapshot |
| #9 | Repeated-boot SMP/storage stress | 4 | 244 | Rebase selectively |
| #14 | Kernel/runtime hardening | 7 | 137 | Isolate individual fixes and retest |
| #23 | Release-note ordering | 16 | 84 | Reconcile; main Build #1855 already published verified notes/assets |
| #4 | Storage/device/desktop stack | 10 | 343 | Do not merge wholesale; review per subsystem |
| #32 | Process-parent documentation | 4 | 34 | Documentation reconciliation only |
| #25 | Current-main evidence docs | 3 | 52 | Superseded documentation; close or rebase after review |
| #8 | Shell/platform activation | 147 | 244 | Major staged rebase required |
| #6 | Pipe semantics | 19 | 343 | Rebase and rerun IPC tests |

The release pipeline itself is no longer a P0 blocker for code snapshot `100c8e2cf4cdd049c4bf5565b5eebbc0f5284651`: main Build #1855 verified and published the stable/versioned ISO pair at SHA-256 `4628a75807bb9a074ce48eaf8e627997fd86b9c33c63201d66dacc785b498e80`. The open #23 branch is therefore a reconciliation item, not evidence that publication is currently broken.

## 2026-11-10 — post-merge main build and stable ISO verification

- Latest code-bearing main merge is `100c8e2cf4cdd049c4bf5565b5eebbc0f5284651` (PR #44, ZJFS ordered-data FLUSH barrier and torn journal-superblock recovery); subsequent main commits in this review are documentation-only.
- Post-merge main Build #1855 (run `42752361937`) completed **successfully** on exact code-bearing commit `100c8e2`. Completed steps include source regressions, native x86-64 build, reproducible release gate, kernel image verification, QEMU boot test, scheduler panic diagnostics, two-boot AHCI/NVMe storage persistence, production ISO build, production desktop/network smoke, production artifact upload, and GitHub Release publication. Main Stage 1 Scheduler #276 (run `42752361843`) also completed successfully on the same commit.
- The `zeroos-bootable` release now contains `zeroos-100c8e2.iso` and stable `zeroos.iso`, each 12,374,016 bytes with identical SHA-256 `4628a75807bb9a074ce48eaf8e627997fd86b9c33c63201d66dacc785b498e80`. Release notes identify `100c8e2`, GRUB/El Torito legacy BIOS boot, and a live session; there is no disk installer. This closes the post-merge CI and stable-ISO P0 blockers for the current code-bearing main snapshot.
- Candidate PR #46 (`fix/block-flush-inflight-barrier-20261110`) addresses a block-layer ordering defect: a dispatched FLUSH was removed from `flush_waiting`, allowing later requests to become eligible while it remained in-flight. Current PR head is `4b7e8a6215fd03da62faa99e0c473c7cbd46a48c`. Stage 1 Scheduler #301 passed on this exact head; Build #1880 (run `42754005097`) is pending, while the earlier pre-rebase Build #1872 (run `42753158241`) reached QEMU boot but has not produced a final result. The source regression passed on the earlier code-equivalent head before one additional source assertion was added; exact-current-head source/build/QEMU/storage/production validation remains pending. Do not merge until Build #1880 completes successfully.
- Remaining release/engineering gates: 600-second/four-vCPU scheduler soak; physical Lenovo G560 boot and physical SATA/NVMe/NIC/USB/graphics matrix; physical power-loss/recovery; clean ACPI S5 shutdown. Existing broad PRs #26/#27/#14/#9/#8/#6/#4 remain heavily diverged and require selective rebase/review rather than blind merge. Overall estimate remains ~43%; no completion percentage increase for CI or documentation work.



## 2026-11-11 — main-branch storage ordering and release validation

| Check | Evidence | Result |
|---|---|---|
| Block FLUSH source regression | Build #1899 / #1900, exact head `8e69f8f` | PASS |
| Block FLUSH runtime self-test | Required serial milestone in the 107-milestone boot set | PASS |
| Storage persistence gate fail-closed regression | Build #1899 / #1900 | PASS |
| Boot diagnostic trap regression | Build #1899 / #1900 | PASS |
| Native x86-64 build | Main Build #1901, commit `1af592d` | PASS |
| Reproducible local release gate | Main Build #1901 | PASS |
| Kernel image verification | Main Build #1901 | PASS |
| Repeated QEMU boot | Main Build #1901: 3/3, 107 milestones each | PASS |
| SMP4 / NX-disabled / failed-IPI / panic diagnostics | Main Build #1901 | PASS |
| AHCI + NVMe persistence | Main Build #1901: two-boot storage gate | PASS |
| Production ISO build | Main Build #1901 | PASS |
| Production desktop/network smoke | Main Build #1901 | PASS |
| Main Stage 1 Scheduler | #322, exact commit `1af592d` | PASS |
| Stable release publication | Main Build #1901 | PASS |

Main Build: https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/42952878802  
Main Stage 1: https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/42952878772  
Candidate Build #1900: https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/42951257089  
Candidate Build #1899: https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/42951248737  
Merged PR #46: https://github.com/priyanshagrahari54-blip/zeroos/pull/46

The stable `zeroos.iso` and versioned `zeroos-1af592d.iso` are both 12,374,016 bytes and match at SHA-256 `5df87ed020a9d5695a113ad75e163b4b85f8d086dd9a9c8ee89310dcec37f789`. Release notes identify code snapshot `1af592d`, legacy BIOS El Torito boot, and a live session (no disk installer).

The ISO verifier continues to emit a generic one-warning summary; QEMU's three successful boot iterations are separate, direct evidence that the image boots in the configured emulator. Physical hardware compatibility remains unverified.

Open P1/P2 validation: 600-second/four-vCPU scheduler soak; physical Lenovo G560, SATA/NVMe/NIC/USB/graphics matrix; physical power-loss/recovery; clean ACPI S5 shutdown. A separate code-review follow-up remains for duplicate block completion accounting in `block_complete()`; it is not included in PR #46.


## 2026-11-11 — latest main verification and duplicate-completion candidate

- Latest main HEAD is `af626f1e802f0c76bb87cb3f6e18392bf6c2c24a`; it is a documentation-only successor to code-bearing merge `1af592d300e648ffddb7e76f644d28d9156c34fb` (PR #46).
- Main Build #1901 (run `42952878802`) and Stage 1 Scheduler #322 (run `42952878772`) completed successfully on `1af592d`. The Build log reports 3/3 QEMU boots with 107/107 milestones each, SMP4, NX-disabled, failed-IPI, scheduler diagnostics, two-boot AHCI/NVMe persistence, production ISO, production desktop/network smoke, and successful release publication.
- The versioned `zeroos-1af592d.iso` and stable `zeroos.iso` are each 12,374,016 bytes and have identical SHA-256 `5df87ed020a9d5695a113ad75e163b4b85f8d086dd9a9c8ee89310dcec37f789`. Release notes identify `1af592d` and state legacy BIOS El Torito live session; no disk installer.
- **New candidate:** `fix/block-duplicate-completion-guard-20261111` addresses a race in `block_complete()`: a second callback can observe `BLOCK_REQ_DISPATCHED` after the first callback has unlinked the request but before `block_finalize_chain()` publishes COMPLETE. The fix verifies list membership, guards the in-flight count, and claims `BLOCK_REQ_COMPLETING` under the device lock. A runtime regression recreates a stale DISPATCHED object after unlink and requires no second accounting or in-flight underflow; a Python source-contract regression and required boot milestone are added.
- Stage 1 Scheduler #325 (push) and #326 (PR) passed on exact candidate code SHA `abac8c3c027416755102d44f2a5301c0be1372a9`. Candidate Build #1904 (push) and #1905 (PR) remain pending and expose no jobs yet. Therefore candidate native full Build, QEMU boot, storage persistence, production ISO and desktop smoke remain unverified. Do not merge or attribute the new milestone to main until the exact candidate Build passes.
- Remaining independent P1/P2 gates: 600-second/four-vCPU scheduler soak; physical Lenovo G560 and SATA/NVMe/NIC/USB/graphics matrix; physical power-loss/recovery; clean ACPI S5 shutdown. Overall estimate remains ~43%.



## 2026-11-12 — post-merge block completion and boot/release blockers

- PR #47 merged as `2dac661e73655dfc5e6513bbb5a70b0f7791c8c9`. Its runtime stale-completion regression and source-contract check passed in candidate Build #1904/#1905. Candidate evidence: native build and `make check`; 3/3 QEMU boots with 108/108 milestones; SMP4, NX-disabled, failed-IPI and scheduler diagnostics; AHCI/NVMe two-boot persistence (counter 1 then 2); production ISO and desktop/network smoke. Stage 1 #325/#326 passed.
- Main Stage 1 #327/#328 passed on `2dac661`. Main Build #1906/#1907 failed before QEMU at the reproducible `make check` gate. GCC diagnostics point to `tests/crypto_test.c:72:5` and `:101:36`: string literal terminators omitted by fixed-size `uint8_t` arrays, treated as errors by `-Werror=unterminated-string-initialization`.
- Candidate PR #48 changes only crypto test-vector storage/length declarations. Build #1908/#1909 passed `make check` and kernel-image verification; Stage 1 #329/#330 passed. Both full Build runs failed the first QEMU boot: 105/108 milestones absent, serial stops after the Multiboot2 range self-test, QEMU reports #UD then #GP error `0x32` and triple fault at RIP `0x11a5db`. Candidate PR #48 is **not accepted** and remains open. Causality from its test-only change is unproven.
- Failed QEMU diagnostics artifact: run #1909 artifact `13911377252`; serial log ends at the Multiboot2 info-range self-test. The exact failing ELF/link map was not retained, so the RIP is not yet symbolicated against the failing image.
- Stable/versioned release assets remain `zeroos-1af592d.iso` and `zeroos.iso`, 12,374,016 bytes, SHA-256 `5df87ed020a9d5695a113ad75e163b4b85f8d086dd9a9c8ee89310dcec37f789`. They match each other but do **not** certify merged code commit `2dac661`. Stable-release freshness is an open P0 blocker.
- Next action: retain the failing kernel ELF and linker map, symbolicate `0x11a5db`, compare early boot memory/GDT state with successful Build #1905, then require a full passing Build/QEMU/storage/production run before merge and stable publication.
- 600-second/four-vCPU scheduler soak, physical G560/device matrix, physical power-loss recovery and ACPI S5 remain unverified. No overall completion increase; estimate remains ~43%.


## 2026-11-12 — exact main and runner-candidate evidence

- Main code commit: `2dac661e73655dfc5e6513bbb5a70b0f7791c8c9`; main documentation HEAD: `52ef007547e06045bf1c07998d78f581de71779c`.
- Main Build #1906 (run `43144281250`) and #1907 (run `43144667493`) both failed at the reproducible `make check` step before kernel-image verification or QEMU. The failed step was `Run reproducible local release gate`; downstream QEMU, storage, production ISO and release steps were skipped. Stage 1 #327/#328 passed.
- PR #48 crypto-test fix commit `1342d6f91a9a8b1869678aa0839ea868b16df05f` removed the GCC warning by using inferred-size arrays, `sizeof(...)-1` lengths and static length assertions. The current PR head `318faf3f16ef3e0f6c40f60365e6a28611feb1f8` additionally pins both CI workflows to Ubuntu 24.04.
- The older PR #48 Build #1909 (run `43144788523`) passed the reproducible host check and kernel-image step, then failed the first QEMU boot. Artifact `13911377252` was inspected: it contains `timeout.log`, `debug.log`, `serial.log`, and `qemu.log`, but no `zeroos.elf` or linker map. QEMU records #UD at RIP `0x11a5db`, #GP error `0x32`, and triple fault. This run was before the current Ubuntu 24.04 pin; it is not evidence against the pinned head.
- The current head's combined-status and PR-workflow lookup returned no entries. Treat exact-head CI as unverified, not passed.
- New candidate change: kernel linking emits `$(BUILD)/zeroos.map`; failure-safe diagnostic artifacts retain the exact ELF, map and ISO for both certification and production images. New `tools/test_boot_image_artifacts.py` checks linker-map generation, artifact paths, always-upload behavior and CI ordering.
- Local validation: `tools/test_ci_runner_image.py` passed against a local fixture reproducing the fetched Ubuntu 24.04 workflow declarations and ordering. GNU ld map-output smoke test and the full pinned-runner Build/QEMU/storage/production gates remain to be completed by CI.
- Stable ISO remains the older `1af592d` image (SHA-256 `5df87ed020a9d5695a113ad75e163b4b85f8d086dd9a9c8ee89310dcec37f789`); it is not verified for `2dac661`. P0 release freshness is open.


## Candidate follow-up — local checks and remaining CI gate

- Commit `e141602158927633f76216d7b85d9e885ba0586b` adds linker-map generation and failure-safe retention of `zeroos.elf`, `zeroos.map`, and `zeroos.iso` for certification and production artifacts, with `tools/test_boot_image_artifacts.py`.
- Passed locally: `python3 -m py_compile` for both new regression scripts; runner-pin regression against a fixture matching the fetched workflow declarations; boot-artifact retention regression against the fetched workflow structure; GNU ld `-Map=` smoke test produced a non-empty ELF and map.
- Not run locally: repository `make check`, QEMU, AHCI/NVMe persistence, production ISO, or desktop/network smoke. No workflow/status was returned for exact candidate commit `e141602`; require full Ubuntu 24.04 CI before merge.
- PR #48 remains open, not mergeable in the current API snapshot, and one commit behind main. Stable release freshness for code commit `2dac661` remains open.


## 2026-11-13 — pinned-runner candidate full Build evidence

- Main HEAD remains `52ef007547e06045bf1c07998d78f581de71779c`; code-bearing main commit is `2dac661e73655dfc5e6513bbb5a70b0f7791c8c9`. Main Stage 1 #327/#328 passed, but main Build #1906/#1907 failed at the host `make check` gate on the Ubuntu 26.04 hosted image before QEMU.
- PR #48 code/test commit `e141602158927633f76216d7b85d9e885ba0586b` was validated by Build #1912 (run `43151270438`, push event) and Stage 1 Scheduler #333 (run `43151270516`). Both completed successfully on the candidate branch pinned to Ubuntu 24.04.
- Build #1912 passed the runner-image, boot-artifact-retention, crypto-vector, block, process, scheduler, storage and boot-gate regressions; native x86-64 build; reproducible `make check`; Multiboot2 verification; QEMU boot gate; scheduler fault diagnostics; AHCI/NVMe two-boot persistence; production ISO generation; and production desktop/network smoke.
- The QEMU log explicitly reports 3/3 boots passing with all 108 required milestones on each iteration. The four-vCPU certification log contains the required 4/4 CPU-online, remote TLB, scheduler, affinity/offline and service lifecycle assertions. NX-disabled and failed-IPI recovery steps completed successfully.
- The retained storage logs show SATA/AHCI and NVMe discovery, destructive QEMU device probes, ZJFS mounts and persistence counters 1 (previous 0) then 2 (previous 1), with the persistence check passing on both boots. The production smoke step passed its desktop-session and RTL8139 attachment assertions.
- Build #1912 artifacts were downloaded and inspected. `zeroos.elf` (499,312 bytes), `zeroos.map` (117,818 bytes), `zeroos.iso` (12,374,016 bytes), serial/timeout/debug/QEMU logs, SMP4/NX logs and both storage-boot logs are present. The timeout log contains the actual TERM deadline marker. This is candidate evidence, not post-merge main evidence.
- The GitHub Release publication step was skipped on the branch push by design. The current stable release still contains `zeroos-1af592d.iso` and `zeroos.iso`, both 12,374,016 bytes with SHA-256 `5df87ed020a9d5695a113ad75e163b4b85f8d086dd9a9c8ee89310dcec37f789`; that image does not certify code-bearing main commit `2dac661`.
- The PR documentation tip `049ee9909706df5cbd3ba2a9ae8e5a467bb0e6b3` is a documentation-only successor to the Build-tested code commit `e141602`. It was one documentation commit behind main before synchronization. The branch has not been merged; require a full Build on the synchronized exact head, then a post-merge main Build and stable/versioned ISO digest equality before closing P0 release freshness.
- The ISO verifier continues to print a generic one-warning summary (`THIS IMAGE WILL NOT BOOT`); do not use that warning as a substitute for guest evidence. The separate QEMU test passed all three iterations. Physical G560 and device compatibility remain unverified.
- Remaining gates: 600-second/four-vCPU scheduler soak; physical Lenovo G560, SATA/NVMe/NIC/USB/graphics matrix; physical power-loss recovery; clean ACPI S5 shutdown. Overall estimate remains ~43%; no completion credit for CI or documentation.


## 2026-11-13 — PR #48 merged; post-merge main validation

- PR #48 was squash-merged as `b37b9542da719cc8dda30d5e08728b73602fe021`. It fixes the RFC 8439 host test-vector declarations (inferred-size arrays, `sizeof(...)-1` lengths, compile-time 114/34-byte assertions), pins both CI workflows to Ubuntu 24.04, emits `zeroos.map`, and retains exact ELF/map/ISO diagnostics for certification and production artifacts. This is build/test/diagnostic infrastructure; it does not change kernel runtime code.
- The synchronized PR head `6143ffa750715d9e2ba2315023e81216570ad106` passed exact-head Build #1913 (push, run `43354185009`) and Build #1914 (PR, run `43354190293`); Stage 1 Scheduler #334/#335 also passed. Both full Build runs passed native build, `make check`, kernel/Multiboot2 verification, QEMU, scheduler fault diagnostics, AHCI/NVMe two-boot persistence, production ISO and desktop/network smoke. PR Build #1914 release publication was skipped by policy because it was a PR event.
- The exact candidate QEMU log reports 3/3 boots with all 108 milestones each. The SMP4 log reports 4 CPUs discovered and online and scheduler certification passed. NX-disabled and failed-IPI recovery checks passed. The two storage serial logs report `/data` counter 1 (previous 0) and 2 (previous 1), with clean-unmount/remount persistence checks passing on both boots. No panic or triple-fault marker appears in the retained serial logs.
- Candidate Build #1914 artifacts were downloaded and inspected: boot diagnostics contain the 499,312-byte ELF, 117,818-byte linker map, 12,374,016-byte ISO, serial/timeout/debug/QEMU logs, SMP4/NX logs and both storage boot logs. The timeout log contains the actual GNU timeout TERM-deadline marker. The candidate production ISO SHA-256 is `26ebd351cd758f814ec83a5f1f9d7f8b721c26d76d1494fa2a2e10d745e03356`; this is a CI artifact, not yet the mutable stable release asset.
- Post-merge main Stage 1 Scheduler #336 passed on code commit `b37b954`. Main Build #1915 (run `43355195504`) has passed all source regressions, native build, reproducible `make check`, and kernel image verification; its QEMU boot gate is running at the time of this update. Do not claim post-merge main QEMU/storage/production/release success until the workflow completes.
- The stable release currently still identifies `1af592d`, with versioned and stable ISO assets both 12,374,016 bytes and SHA-256 `5df87ed020a9d5695a113ad75e163b4b85f8d086dd9a9c8ee89310dcec37f789`. It does not yet certify `b37b954`. The main Build must publish the tested image, then the versioned/stable assets must be downloaded and compared by SHA-256.
- The ISO verifier prints a generic one-warning message (`THIS IMAGE WILL NOT BOOT`); the independent QEMU evidence above is the boot acceptance signal. The G560 report in the candidate artifact explicitly says its performance/thermal values are declared targets, not measured observations, and that no physical boot occurred. Lenovo G560 hardware support is therefore still uncertified.
- Remaining engineering gates: 600-second/four-vCPU scheduler soak; physical Lenovo G560 and SATA/NVMe/NIC/USB/graphics matrix; physical power-loss recovery; clean ACPI S5 shutdown. Overall estimate remains ~43%; no completion credit for CI or documentation.


## 2026-11-13 — post-merge main Build and stable ISO verified

- PR #48 is merged as code-bearing main commit `b37b9542da719cc8dda30d5e08728b73602fe021`. Main Stage 1 Scheduler #336 (run `43355195594`) and main Build #1915 (run `43355195504`) both completed successfully on that exact code commit.
- Build #1915 passed all source regressions, native x86-64 build, reproducible `make check`, Multiboot2/kernel-image verification, QEMU boot certification, scheduler panic diagnostics, AHCI/NVMe two-boot persistence, production ISO generation, production desktop/network smoke, diagnostic/artifact preservation and GitHub Release publication.
- Main QEMU boot log reports 3/3 iterations passing with all 108 required milestones per iteration. SMP4 reported `discovered=4 online=4` and scheduler certification passed. NX-disabled and failed-IPI recovery steps passed. Retained serial logs contain no panic or triple-fault marker.
- AHCI/NVMe persistence logs show `/data` counter 1 (previous 0) on boot 1 and 2 (previous 1) on boot 2; clean-unmount/CLEAN-superblock/remount checks passed on both. Production serial log contains `interactive desktop session ready` and RTL8139 attachment. This is QEMU evidence, not physical NIC or display certification.
- The main Build ran on `b37b954` while main advanced to documentation-only commit `7dd512d`. The release freshness guard fetched the newer main tip, verified that the difference was documentation-only, and explicitly published the boot-tested code snapshot `b37b954`. This directly validates the docs-only freshness exception.
- The release workflow downloaded the newly uploaded stable alias and byte-compared it with `build-production/zeroos.iso`; it reported `stable release ISO matches the boot-tested production ISO`. The downloaded Build #1915 production artifact was independently hashed and matches the GitHub Release API digests.
- Versioned asset `zeroos-b37b954.iso` and stable asset `zeroos.iso` are each 12,374,016 bytes and have identical SHA-256 `cb544ebfe3d91fb50d594aa6c8b81db32f8e24e416f7e71cb72ee596f54516ad`. Release notes identify `b37b954`, GRUB/El Torito legacy BIOS boot, and a live session; there is no disk installer.
- The ISO verifier still emits a generic one-warning string (`THIS IMAGE WILL NOT BOOT`); the three successful guest QEMU iterations are the direct boot evidence. The G560 certification report in the Build artifact says its performance/thermal values are declared targets, not measured observations, and that no physical boot occurred. Physical Lenovo G560 certification remains open.
- P0 build/boot/post-merge CI/stable-release freshness is closed for code snapshot `b37b954`. Remaining engineering gates: 600-second/four-vCPU scheduler soak; physical G560 and SATA/NVMe/NIC/USB/graphics matrix; physical power-loss/recovery; clean ACPI S5 shutdown; standalone disk installer. Overall estimate remains ~43%; no completion credit for CI or documentation.


## PR #51 candidate tracking — 2026-11-14
- Candidate branch `fix/process-thread-attach-ownership-20261114` was opened from main HEAD `129707fa0c1f1d92ac8d2e972d09f0254437f9b7`. Initial code/test commit: `32675ddf50189e42cd513ba7cbd9dcfdbb684bf3`.
- Exact candidate Build/QEMU/storage/production workflows are not yet visible in the current workflow-run lookup. Do not merge until the final PR head has a complete successful Build and Stage 1 result.
