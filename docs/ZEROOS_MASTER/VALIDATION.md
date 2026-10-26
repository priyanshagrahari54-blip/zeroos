# ZEROOS — Validation and Support Matrix

Updated: 2026-10-25. This is the evidence policy for Stages 1-10; it must not overstate support.

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

## 2026-10-26 — Release metadata ordering (candidate)
- Latest reviewed main commit: `251f7bb05c58b20b4c7b7e5fd0ae9249276caf92`.
- Release publisher previously updated release notes before the stable alias upload/download/byte comparison finished. A rollback could restore the old stable ISO while leaving notes that described the new build.
- Candidate change defers final notes for an existing release until the stable alias has passed `cmp`. A newly created release carries a temporary note stating that stable ISO verification is pending.
- `tools/test_release_publish_guard.py` checks the pending note and requires the final `gh release edit` to follow the successful stable-ISO marker; the existing inline-shell `bash -n` check remains in place.
- **Pending:** run the exact candidate regression script and full CI/QEMU/storage/production smoke workflow. The prior candidate workflow #1484 passed before this change; it does not validate this candidate. Post-merge main-push publication and live release-asset verification remain separate gates.

### Candidate workflow #1500 — regression assertion correction (2026-10-26)
- Result: **FAIL before build/QEMU**, at `Verify stable release publication policy`.
- Cause: the new ordering assertion used the human-readable success echo as its boundary, but the workflow emits that echo after updating final notes. The actual notes edit was already after the successful `cmp`; the assertion marker was too late.
- Correction: use the explicit workflow comment immediately after the stable byte-comparison branch as the ordering boundary. Exact corrected candidate workflow #1503 was queued at the time of this update; its result is pending.
