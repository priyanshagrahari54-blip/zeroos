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
- Prior PR candidate workflow #1420 passed its build, `make check`, QEMU certification, storage persistence, production ISO/desktop smoke test, and the earlier release-policy check. The new branch's exact workflow result is pending; no new pass is inferred.
- Pending release gates: green CI for this exact branch, reviewed merge, successful post-merge `main` workflow, and verification that the stable release asset corresponds to that exact `main` commit.
