# ZEROOS — CURRENT IMPLEMENTATION STATUS

Updated: 2026-10-25
Branch baseline at review: `main` (before PR #16 merge: `e8fb0a72d45733a682b9614e80c019941f747ab1`); PR #16 merged as `d6b58292df7e00b4b2f15ba22f3a0405035ee549`.
Status policy: **evidence-weighted estimate; never equate a source file, marker check, architecture document, or host test with a working end-user feature.**

## Executive status

ZEROOS is a real native x86-64 kernel/desktop-session project with an ISO build path, host tests, and QEMU boot certification in CI. It is **not a 100%-complete consumer OS** and is not certified for production installation on arbitrary physical PCs.

- **Working build path:** `make` / `make iso`, with the bootable ISO requiring a valid GRUB/El Torito toolchain.
- **Full verification path:** `make check`; release/boot evidence: `tools/release.sh`.
- **Latest baseline commit reviewed in this session:** `a72c171d` (`docs: expand master prompt to full ZEROOS completion and bootable release`); it expands the completion directive but is documentation-only, so it does not increase implementation progress.
- **PR #16:** merged on 2026-10-17 as `d6b58292`. Its branch CI reported success and included parallel-build dependency fixes, ISO validation/writer fixes, shared 91-milestone boot test, reproducibility checks, and local intent-parser foundations.
- **Post-merge CI:** this session's GitHub status lookup returned no status entries for `a72c171d`; this is not proof of a passing or failing workflow. Run `tools/release.sh` from a clean checkout and inspect the main-branch Actions run before declaring release certification.
- **Physical hardware:** not certified by this status entry. QEMU is not proof of G560 Wi-Fi, GPU acceleration, fan control, Bluetooth, or sustained 1080p playback.
- **Installer:** a standalone ZEROOS disk installer is not complete; the documented dual-boot path relies on host GRUB.
- **Dynamic linking:** unsupported ELF dynamic/interpreter segments are rejected. A production dynamic linker is not implemented.
- **Compatibility:** PE/Win32 and Android-related foundations are not equivalent to a working Windows or AOSP runtime.
- **Browser/media/AI:** lifecycle/policy/broker foundations must not be described as a complete browser engine, hardware media stack, or full autonomous AI system.

## Stage estimates (not release gates)

| Stage | Estimate | What this means / major open work |
|---|---:|---|
| 1 — Boot/kernel/scheduler/SMP | 92% | More race/fault/soak and multi-vCPU evidence; preserve interrupt-frame and context ownership invariants. |
| 2 — Processes/userspace/IPC | 84% | Lifecycle and IPC foundations; dynamic linker/runtime, full negative/fault/resource tests, syscall and process-reap evidence remain. |
| 3 — Storage/filesystem | 48% | Block/VFS/GPT/ZJFS/page-cache foundations; crash/power-cut, fsck, repeated-boot persistence and fault-injection evidence remain. |
| 4 — Drivers/network/power | 43% | Driver and network foundations; complete real device support, TCP/DNS/DHCP operational coverage, driver isolation, Wi-Fi/audio/thermal certification remain. |
| 5 — Desktop/UI | 44% | Framebuffer/compositor/window/session foundations; full shell/app behavior, font/toolkit/accessibility, display/input failure recovery and measured idle behavior remain. |
| 6 — Security/update/recovery | 22% | Firewall/sandbox/rollback foundations; signed package/update trust, independent recovery boot, encryption/key handling and fault-injection remain. |
| 7 — GPU/media/browser/apps | 15% | Lifecycle/policy and some cores; real GPU driver path, browser engine, codec integration and sustained media evidence remain. |
| 8 — Windows/Android/gaming | 13% | Format/API foundations only; usable Win32 runtime, isolated Android runtime and real workload tests remain. |
| 9 — ZERO AI/ecosystem/performance | 16% | Permission broker and deterministic local intent-parser foundations; tool adapters, backend/inference, task execution, cancellation, audit and measured resource control remain. |
| 10 — Hardware/release certification | 10% | ISO/boot test infrastructure improved; hardware matrix, soak, installation/recovery cycle and measured performance remain. |

**Overall implementation estimate: ~43% (rough engineering estimate, not a metric emitted by CI).** Percentages are deliberately not “100%” because stage scripts passing does not prove every feature in the product specification is operational. Re-estimate after each meaningful code/test batch using changed implementation and evidence, not documentation volume.

## Build, test and boot commands

```sh
make                    # build normal ISO (toolchain must support bootable ISO)
make check              # host/unit + stage certification gates
make run                # QEMU serial boot, if QEMU is installed
tools/release.sh        # full release gate: toolchain, checks, reproducibility, ISO validation, QEMU boot
tools/release.sh --iterations 3
```

Do not use `ZEROOS_ISO_ALLOW_UNBOOTABLE=1` as release evidence. It explicitly permits an image that may not be bootable. A skipped QEMU boot test is not a passing boot test. The release script must complete without `--skip-boot-test` before calling an artifact QEMU-boot-certified.

## Immediate priority order

**Execution directive:** `MASTER_IMPLEMENTATION_PROMPT` now explicitly requires implementation across stages 0–10 and every documented feature, with build/boot proof first. The directive itself is not implementation evidence. This session did not run a local build because the connected GitHub interface exposes repository read/write and CI status queries, not a checked-out shell/runner; no test is claimed as run here.

1. **Release truth / build integrity:** verify post-merge CI for `d6b5829`; fix every failure; ensure `make -j check` and `tools/release.sh` agree.
2. **Boot stability:** preserve all required boot milestones; run repeated SMP QEMU boots; capture serial/debug/QEMU logs on failure.
3. **Stage 2 lifecycle:** process/thread creation rollback, exit/wait/reap, ELF load failures, user-copy boundaries, timeout/wakeup races and resource-leak regression tests.
4. **Storage integrity:** repeated boot/persistence, crash-consistency, block fault injection, fsck/rollback, ownership and cleanup.
5. **Real hardware/driver coverage:** PCI/ACPI/DMA/IRQ correctness, input/display/audio/network and safe unsupported-device fallback.
6. **Security/recovery:** capability checks at real boundaries, signed artifacts, sandbox negative tests, independently bootable recovery and transactional restore.
7. **Usable desktop/apps:** native launcher/files/terminal/settings first; real UI input, error states, session recovery, accessibility and event-driven zero-render idle.
8. **Browser/media:** integrate mature licensed components where compatible; hardware codec detection, A/V sync, browser isolation and sustained playback measurement.
9. **ZERO AI:** broker → scoped tools → local/remote backend adapter → user consent/audit/cancel/rollback; no unrestricted shell/kernel privileges.
10. **Windows/Android runtimes:** only after ABI, loader, graphics/audio, permissions and workload tests are genuinely implemented.
11. **Performance and release:** measure boot, idle RAM/CPU, disk I/O, app launch/switch, thermal, security, long-duration soak and physical G560 behavior.
12. **Docs and percentages:** update status, validation, roadmap and relevant subsystem contract in the same batch as code changes.

## Evidence rules

- A compile is compile evidence only.
- Host unit tests do not prove guest/kernel execution.
- QEMU boot does not prove physical hardware support.
- UI/policy/parser/marker does not prove an operational subsystem.
- Benchmarks must record hardware, build SHA, command, workload, observed result and failure criteria.
- Keep unsupported and untested features explicitly labelled. Never claim literal zero CPU/RAM/latency, universal hardware compatibility, or stable 1080p playback without the corresponding measurements.

## Recent code and release-hardening evidence

- `3d75f7ae`: Stage 2 certification gates for recent userspace hardening invariants.
- `f2345ddb`: IPC timeout deadline arithmetic and wrap semantics.
- `cde3f716`: timed sleep deadline range and overflow semantics.
- `d958b547`: exec argument/environment vector and string-pointer overflow checks.
- `080b4568`: reject unsupported ELF dynamic segments.
- PR #16, merged as `d6b58292`: parallel build dependencies, ISO writer/validator, reproducible build checks, shared boot milestones and local intent parser. Verify the post-merge main workflow before treating its changes as release-certified.

Last verified limitation snapshot: physical hardware validation, standalone installer, full dynamic linker, complete browser/GPU/media stack, full Windows/Android runtimes, and a complete AI execution backend remain open.

## Release-publishing guard review — 2026-10-24
- Reviewed `main` at `4eb68e4fbfeb02d51fc114f82a05ac641e1acd56`. The workflow's stable-release step still used only `github.event_name == 'push'`, allowing a feature-branch push to replace the mutable `zeroos-bootable` release.
- **Candidate change (not merged):** the release step now requires both a push event and `github.ref == 'refs/heads/main'`. Added `tools/test_release_publish_guard.py` and wired it into CI to fail if the publishing step's condition changes or becomes ambiguous.
- The exact candidate code passed workflow #1429; after documentation synchronization, workflow #1435 passed on head `1dbee68abde71e2570d8008f35b0034199d55df0`. Both are candidate-branch runs, not post-merge `main` or stable-release-asset verification.
- Exact current-branch CI, post-merge `main` CI, and stable release asset/source-commit verification remain pending. No new QEMU boot or release publication is claimed.
- This is release-pipeline safety work; it does not add kernel functionality or increase the overall implementation estimate.

### Evidence update — candidate workflow #1429 (2026-10-24)
- Exact candidate head `0f17ee1d8da6ea606bb43070faa6514000ed3a84` completed GitHub Actions workflow #1429 successfully.
- The new release-policy regression step passed. `make`, clean `make check`, ISO verification, repeated QEMU boot certification, scheduler panic-diagnostics fault test, two-boot AHCI/NVMe persistence test, production ISO build, and production desktop/network smoke test all passed in this run.
- Release publication was skipped because this was a pull-request event; that is expected and is not evidence that the stable release asset was restored.
- The candidate is still unmerged. No post-merge `main` workflow, physical-hardware result, or stable release asset/source-commit verification is claimed. Overall implementation estimate remains ~43%.


## Production boot diagnostic retention — 2026-10-24
- The latest reviewed `main` remains `4eb68e4fbfeb02d51fc114f82a05ac641e1acd56`; the latest main-branch workflow (#1407) passed on 2026-10-18. It is not a post-merge verification of the current candidate.
- Candidate PR #19 already restricts stable ISO publishing to `push` on `refs/heads/main`. This follow-up adds an `if: always()` artifact step for production serial, QEMU, and debug-port logs, so a failed desktop/network smoke assertion should retain the evidence needed to diagnose it.
- Added `tools/test_production_boot_diagnostics.py`, wired into CI to check step order, unconditional artifact capture, missing-file tolerance, and that the smoke test writes all three diagnostic outputs.
- **Pending at this commit:** CI/QEMU result for this exact diagnostic change. Do not claim it is validated until the new candidate workflow finishes.
- This is a CI observability improvement; it does not alter kernel behavior, add OS features, or change the evidence-weighted completion estimate.

### Workflow test invocation uniqueness — 2026-10-24
The release-policy regression script now also asserts that CI invokes it exactly once, preventing duplicate or missing guard checks. The production-diagnostic retention check remains separate; CI for the exact corrected commit is pending.

## Stable ISO content verification — 2026-10-24
- **Main already contains:** the main-only release publication condition and production boot diagnostic artifact retention, with regression scripts wired into CI. PR #19's candidate push/PR workflows were reported passing; a successful post-merge main run and stable asset verification were the remaining release checks.
- **New candidate change (not yet validated):** after publishing the versioned ISO and mutable `zeroos.iso`, the workflow downloads the stable alias and byte-compares it with `build-production/zeroos.iso`, the image boot-tested earlier in that run. Publication fails if the alias is missing or differs.
- Extended `tools/test_release_publish_guard.py` to require the main-only publication condition and to assert the stable asset is downloaded before the byte comparison.
- **Evidence status:** this exact candidate still requires the CI regression scripts, clean `make check`, QEMU certification, storage persistence, production desktop/network smoke test, and post-merge release verification. No new pass, release restoration, or physical-hardware result is claimed here.
- This is release-artifact integrity work; the OS implementation estimate remains unchanged.

## Evidence update — 2026-10-25 (supersedes the candidate-pending note above)
- **Merged to main:** PR #20, merge commit `34010187ef16b4a71b476a22c782cabf4558dc06`. The release workflow now downloads the published mutable `zeroos.iso` and byte-compares it with `build-production/zeroos.iso` from the same run; mismatch fails the job. The regression script checks both the main-only guard and download-before-compare ordering.
- **Exact candidate CI:** workflow #1464 (`40058802277`) completed successfully on the PR candidate. Its policy/diagnostic regression checks, clean `make check`, repeated QEMU boot gate, 4-vCPU gate, NX-disabled boot, fault-recovery boot, AHCI/NVMe persistence certification, production ISO build and production desktop/network smoke test all passed. The release-publishing step was correctly skipped for the PR event.
- **Main-run/release status:** the GitHub connector used in this review did not return push-triggered workflow runs for merge commit `34010187...`; therefore post-merge `main` CI and the live release-asset byte comparison remain **unverified here**. Do not treat candidate CI as proof that the remote `zeroos.iso` was refreshed.
- No new kernel functionality or physical-hardware certification is claimed. Overall OS implementation estimate remains unchanged.
