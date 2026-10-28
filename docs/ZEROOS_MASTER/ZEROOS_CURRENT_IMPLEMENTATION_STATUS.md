# ZEROOS — CURRENT IMPLEMENTATION STATUS

Updated: 2026-10-28
Branch baseline at this refresh: `main` latest observed commit `7c45183332f895ad778f08b98458f580b18a0067` (`test: certify exec user-copy overflow guard`). PR #16 remains merged as `d6b58292df7e00b4b2f15ba22f3a0405035ee549`.
Status policy: **evidence-weighted estimate; never equate a source file, marker check, architecture document, or host test with a working end-user feature.**

## Historical evidence refresh — 2026-10-26 (superseded for current HEAD)

- Historical note: the prior `main` baseline `b1c848f193940bfd18691f942daf6e32d0dae210` is no longer HEAD. The current reviewed `main` SHA is `7c45183332f895ad778f08b98458f580b18a0067`; the current-head status/workflow lookup returned no entries, so CI for this exact SHA is **unverified**.
- The latest completed main workflow inspected is **#40491492164**, which checked out `b1c848f193940bfd18691f942daf6e32d0dae210`, not current HEAD. That run passed build, clean `make check`, repeated QEMU boot, 4-vCPU SMP, NX-disabled compatibility, failed-IPI recovery, scheduler panic diagnostics, two-boot AHCI/NVMe persistence, production desktop/network smoke, release publication, and stable-ISO byte comparison. The verified ISO SHA-256 was `9d9c5594a4669936e2ac37320a025365018b785768f73d6d52aebfb56a818529`. Run: https://github.com/priyanshagrahari54-blip/zeroos/actions/runs/40491492164. This is strong evidence for that older SHA only; it does not certify current HEAD or physical hardware.
- The six-file reference-PDF inventory and initial disposition are recorded in `ZEROOS_REFERENCE_PDF_AUDIT_AND_ADOPTION_MATRIX.md`. It is an audit, not implementation credit. The two hardware PDFs added in commit `5f0d1a3a012d1f397e9d5561f5081109ced1e521` still need text/page extraction for a content-level review.
- The 100-item and 120-item optimization catalogues (220 entries total) still need an individual Adopt / Adapt / Defer / Reject / Hardware-dependent decision ledger.
- Evidence-weighted estimates remain ~43% overall (Stages 1–10: 92/84/48/43/44/22/15/13/16/10). Do not increase implementation percentages because documentation or CI passed; only verified feature behavior earns credit.

## Executive status

ZEROOS is a real native x86-64 kernel/desktop-session project with an ISO build path, host tests, and QEMU boot certification in CI. It is **not a 100%-complete consumer OS** and is not certified for production installation on arbitrary physical PCs.

- **Working build path:** `make` / `make iso`, with the bootable ISO requiring a valid GRUB/El Torito toolchain.
- **Full verification path:** `make check`; release/boot evidence: `tools/release.sh`.
- **Latest baseline commit reviewed in this refresh:** `7c45183332f895ad778f08b98458f580b18a0067`; no current-head workflow/status result was returned, so its CI is unverified. The latest completed main run inspected is #40491492164 for the older `b1c848f` SHA. Documentation-only commits do not increase implementation progress.
- **PR #16:** merged on 2026-10-17 as `d6b58292`. Its branch CI reported success and included parallel-build dependency fixes, ISO validation/writer fixes, shared 91-milestone boot test, reproducibility checks, and local intent-parser foundations.
- **Post-merge CI:** workflow #40491492164 successfully built, boot-tested and published the ISO for `b1c848f`; current `main` (`7c451833`) has no returned check result. Do not attribute the older run to the current SHA.
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

## Stable release alias rollback hardening — 2026-10-26 (candidate)
- Candidate branch `fix/stable-iso-rollback-20261026` changes release publication so the commit-specific ISO is uploaded and byte-verified before the mutable `zeroos.iso` alias is changed.
- Before changing the alias, the workflow saves any existing stable asset. If stable upload, download, or byte comparison fails, it attempts to restore and verify the previous bytes; if no prior alias existed, it removes any unverified alias and checks that it is absent.
- `tools/test_release_publish_guard.py` now asserts main-only publication, versioned-before-stable ordering, rollback paths, and parses the inline publishing shell with `bash -n`.
- **Exact candidate CI:** workflow #1484 (run ID `40263294383`) succeeded for this exact candidate on 2026-10-26. The regression checks, `make`, clean `make check`, kernel/ISO checks, repeated QEMU boot gate, scheduler panic-diagnostics certification, two-boot AHCI/NVMe persistence, production ISO build, and production desktop/network smoke test passed. The release-publishing step was skipped on the PR event by design. **Still unverified:** actual rollback against GitHub Releases and post-merge live stable-asset verification.
- No kernel functionality or overall OS completion estimate is changed by this release-pipeline hardening.


## Completion contract refresh — 2026-10-26

Added [ZEROOS_ALL_FEATURES_IMPLEMENTATION_AND_BOOTABLE_RELEASE_CONTRACT.md](./ZEROOS_ALL_FEATURES_IMPLEMENTATION_AND_BOOTABLE_RELEASE_CONTRACT.md). It consolidates the execution protocol for all ten stages, native bootable release gates, full ZEROOS product feature ledger, and traceability/disposition requirements for all 100 + 120 optimization-catalog entries supplied for this project.

**Progress estimate unchanged by this documentation-only commit:** overall ~43%; Stage 1 92%, Stage 2 84%, Stage 3 48%, Stage 4 43%, Stage 5 44%, Stage 6 22%, Stage 7 15%, Stage 8 13%, Stage 9 16%, Stage 10 10% (engineering estimates from the previous evidence snapshot, not CI metrics). This document does not make missing features implemented and does not certify a release.

**Next gate:** run `tools/release.sh` and inspect the current main-branch CI with the exact current commit; record command output, ISO checksum, QEMU configuration and test results before revising status. Continue in dependency order: boot/build/release blockers → Stage 1–3 correctness and recovery → drivers/network → interactive desktop → security/update/recovery → real browser/media → Windows/Android runtimes → ZERO AI tool execution → physical hardware/performance certification.


## Library research / reuse update — 2026-10-26

Added `LIBRARY_REUSE_AND_INTEGRATION_PLAN.md` and indexed it from `MASTER_INDEX.md` and `ZEROOS_MASTER_ROADMAP.md`. It maps candidates (libc, LZ4, zlib-class compression, FreeType, HarfBuzz, lwIP, TLS libraries, SQLite, FFmpeg-class media, LLVM, AOSP/ART, Wine) to specific ZEROOS layers and defines licensing, provenance, ABI, isolation, test, measurement, and rollback gates. It also records that the 100/120 optimization catalogs are ideas, not ready-made source code, and that RECOVER-X/G560 archives require file-level inventory before reuse.

**Implementation impact: none claimed.** These are documentation and research triage changes. No third-party source was imported by these commits, no boot/CI test was run by the documentation edits, and the existing overall completion estimate is unchanged until a fresh current-main release gate and real integration evidence are available. Two hardware/kernel reference PDFs still need full-text/page-level audit. Next engineering priority remains clean build + ISO verification + repeated QEMU boot, then highest-severity correctness failures.

## Release metadata ordering review — 2026-10-27
- Latest `main` commit observed: `fa6fb6e750843ee14dc0b1f9151c20052741f337` (documentation/roadmap refresh). The combined-status lookup returned no status entries for this exact SHA; this is **unknown**, not a pass or failure.
- Latest directly inspected successful `main` run: workflow #1543 / run ID `40322921742` for `5d89992d6c78a2a19108385c3fc4979fa3cfe19e`. Its job steps show success for `make`, `tools/release.sh` reproducibility/release gate, kernel verification, QEMU boot test, scheduler fault certification, two-boot AHCI/NVMe persistence, production ISO build, production desktop/network smoke, diagnostic artifacts, and the release-publishing step. This is evidence for that SHA only.
- The current workflow still edits final success release notes immediately after uploading the versioned ISO, **before** the mutable `zeroos.iso` alias has been downloaded and byte-compared. If later stable-alias publication fails, metadata can describe a build that is not the verified stable download.
- Candidate fix on `fix/release-notes-verify-order-20261027`: existing releases retain their notes until stable alias verification succeeds; new releases begin with an explicit pending-verification note; final notes are applied only after the stable ISO byte comparison. `tools/test_release_publish_guard.py` now pins that ordering and checks the pending-note path.
- Candidate CI/QEMU evidence is pending. No kernel behavior changed and no OS completion estimate is increased. PR #23's older branch is 27 commits behind current `main` and is not being treated as a clean integration base; this candidate was created from current `main`.
- Release closeout remains: green CI on this exact candidate, reviewed merge, then a successful push-triggered `main` workflow and verification of its stable asset/notes. Physical G560 certification, standalone installer, dynamic linker, production browser/media, Windows/Android runtimes, and full ZERO AI backend remain open.

## Evidence update — 2026-10-27 (PR #24 merged)
- Release-note ordering fix merged to `main` as `1cd514b641d148b4094c0de524925dbd323b92eb`.
- Exact candidate head `1cb4981e9358563b0dda75671c876307cac80039`, workflow #1553 (run ID `40440843398`): **job succeeded**. Recorded passing steps: release publication policy regression, production diagnostic-retention regression, build, clean `make check`, kernel/ISO artifact verification, repeated QEMU boot gate, scheduler fault certification, two-boot AHCI/NVMe persistence, production ISO build, production desktop/network smoke test, diagnostic preservation. The release publishing step was skipped because this was a pull-request event.
- Post-merge `main` workflow and current live stable-asset verification are still outstanding; do not infer them from PR CI. Physical hardware certification remains unestablished. Completion estimate unchanged.


## Boot-gate integrity candidate — 2026-10-28

- Current `main`'s `tools/boot_test.sh` previously ignored QEMU's exit status with `|| true`, did not remove all prior logs before each iteration, and only checked the 32-bit/long-mode debug markers if a debug log happened to exist. This could allow stale or incomplete evidence to obscure an early QEMU exit.
- Candidate branch `fix/boot-gate-fail-closed-20261028` changes the shared gate to require the expected timeout status (124), clear serial/debug/QEMU logs for each iteration, and require fresh `B` and `L` debug markers every iteration. The workflow now checks exit statuses for 4-vCPU, NX-disabled, failed-IPI, scheduler-panic, storage persistence and production smoke boots.
- Added `tools/test_boot_gate_fail_closed.py` for unexpected exits, stale logs, expected-timeout handling, and workflow wiring. Candidate commits: `e754956` (shared gate), `a1449e1` (regression test), `42fb867` (workflow).
- **Candidate validation is pending** the exact branch CI run. This is test-integrity work, not new kernel functionality. Keep the overall estimate at ~43% until evidence changes; physical-hardware certification remains unestablished.

## Evidence update — 2026-10-28: scheduler affinity / CPU hot-offline race

- Latest `main` verified by comparing refs: `731e9e8856976beb2e717eef169ded72f4917741` (`ci: fail closed on unexpected QEMU boot exits`). The exact-head combined-status lookup returned no entries; current-main CI is **unverified**, not passed or failed.
- `main` now contains the fail-closed QEMU gate: fresh per-boot logs, expected timeout status, required boot-entry/long-mode markers, and explicit exit checks for inline CI boots.
- Candidate branch `fix/scheduler-affinity-offline-race-20261028` revalidates a requested affinity under `task_lock` before mutating a RUNNING or RUNNABLE task. This closes a race where the target AP's `online` flag can remain set after its offline request is published, allowing a RUNNING task to accept an affinity with no eligible CPU.
- Added a scheduler policy self-test for offline-mask filtering, required runtime marker in `tools/boot_milestones.txt` and Stage 1 certification, and host regression `tools/test_task_affinity_offline_race.py` wired into CI.
- Exact candidate head `8fe3ee0a97788b762e990ef253238eb51c49b008` passed ZEROOS Build workflow #1610 (run ID `40628325243`) and Stage 1 Scheduler CI #31 (run ID `40628325242`). The build workflow passed all four host regression steps, `make`, clean `make check`, kernel/ISO verification, repeated QEMU boot matrix, scheduler panic diagnostics, two-boot AHCI/NVMe persistence, production ISO build, production desktop/network smoke, and diagnostic preservation. Stage 1 native kernel build and scheduler certification passed. Release publication was skipped on the PR event. Current `main` exact-head CI remains unverified; the older release workflow #40491492164 belongs to SHA `b1c848f` and is not attributed to current main. Completion estimate remains ~43%; hardware certification and the 600-second Stage 1 soak remain open.

## Candidate update — 2026-10-28: remote affinity reschedule owner

- Latest reviewed `main`: `218cbc43eaeba24612a91838ee381460aa2212e8` (scheduler affinity/hot-offline race fix). The connected combined-status and workflow-run lookup returned no entries for this exact SHA; treat current-main CI as **unverified**, not passing or failing.
- Code inspection found that `task_set_affinity()` decided whether a RUNNING task needed rescheduling by testing the new mask against `task_cpu_index()`. That is the setter's CPU, not necessarily the CPU currently executing the target task. A remote setter could therefore miss the need-reschedule request when the target's actual owner CPU was excluded.
- Candidate branch `fix/affinity-remote-owner-reschedule-20261028` adds a task-lock-protected owner-CPU lookup, rejects an inconsistent RUNNING-without-owner state before mutation, and evaluates affinity against the actual owner CPU. The scheduler policy self-test and `tools/test_task_affinity_offline_race.py` cover the distinction.
- Candidate validation is pending CI. No local build/QEMU result is claimed. Required next evidence: affinity regression, fail-closed boot-gate and release/diagnostic regressions, clean `make check`, QEMU/SMP/fault boots, storage persistence, and production desktop/network smoke. Completion estimate remains unchanged (~43%); physical hardware and the optional 600-second Stage 1 soak remain separate gates.
