# ZEROOS — Validation and Support Matrix

Updated: 2026-10-17. This is the evidence policy for Stages 1-10; it must not overstate support.

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
