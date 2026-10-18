# ZEROOS — Validation and Support Matrix

Updated: 2026-10-18. This is the evidence policy for Stages 1-10; it must not overstate support.

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
- Main baseline `a72c171dc8efb60c7f083b93f34410dfbd6c33ad` passed full CI/QEMU in [run #39043695663](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/39043695663). The GPT-integrity candidate source/test tree `47862ff6b295b92f4bace026aaac767d8b8635d7` passed the full workflow in [run #39091213001](https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/39091213001).
- That candidate run passed `make`, `make check`, ISO verification, three sequential 2-vCPU QEMU boots, 4-vCPU boot, NX-disabled boot, failed-IPI recovery, scheduler panic-diagnostics certification, q35 AHCI+NVMe persistence over two boots, production boot smoke test and release-artifact publication.
- GPT regression coverage now rejects silently divergent-but-individually-valid primary/backup mirrors by reporting `GPT_ERR_MISMATCH`; the valid primary remains authoritative. This is not a substitute for power-cut/fsck/recovery fault coverage.
- The release gate is `tools/release.sh`; it requires GCC/binutils, GRUB/xorriso and QEMU, runs `make check`, reproducibility checks, ISO structure checks and boot milestones.
- An ISO built with `ZEROOS_ISO_ALLOW_UNBOOTABLE=1`, or a run with `--skip-boot-test`, is not a boot-certified release.
- Physical Lenovo G560 certification is pending. Hardware-dependent claims (Wi-Fi, GPU acceleration, fan control, Bluetooth and sustained 1080p) must remain conditional until measured.
- Standalone disk installation, full dynamic linking, production browser/media stacks, Windows and Android runtime compatibility, and a complete ZERO AI tool/backend execution stack remain incomplete.

## Stage completion rule

No stage is 100% complete until its hardening checklist has executable evidence at the required class. A host test cannot close a QEMU gate; QEMU cannot close a real-hardware gate. Detection, parsing, UI, architecture docs, source-marker checks and mocked APIs do not establish operational support.

## Release blockers

1. Full CI/QEMU is green on the exact promoted source/test SHA; rerun after any code change.
2. `make check` is green and deterministic under the release workflow.
3. `tools/release.sh` completes with no boot-test skip.
4. All required boot milestones pass over repeated SMP QEMU boots.
5. Storage persistence and recovery survive repeated guest boots and injected failures.
6. Security, package trust, update rollback and independent recovery have negative/fault evidence.
7. The physical hardware support matrix and performance/soak results are published.
8. Unsupported features and installation limits are explicit in release notes.
