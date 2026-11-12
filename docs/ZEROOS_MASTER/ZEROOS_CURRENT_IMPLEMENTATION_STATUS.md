# ZEROOS — CURRENT IMPLEMENTATION STATUS

Updated: 2026-11-12
Branch baseline: current main HEAD `52ef007547e06045bf1c07998d78f581de71779c` is documentation-only after code-bearing commit `2dac661e73655dfc5e6513bbb5a70b0f7791c8c9` (PR #47). Main Stage 1 #327 passed, but Build #1906 failed before QEMU on `ubuntu-26.04` because GCC treats two RFC 8439 test-vector string initializers as `-Werror=unterminated-string-initialization`. PR #48 fixes those test declarations; its Build #1908/#1909 then failed first QEMU boot on `ubuntu-26.04` with #UD/#GP/triple fault at RIP `0x11a5db`. The failing ELF/link map was not retained, so causality is unproven. Build #1905 on `ubuntu-24.04` passed 3/3 QEMU boots (108 milestones each), AHCI/NVMe two-boot persistence and production smoke on a candidate with byte-identical block/storage regression files. A candidate explicit `ubuntu-24.04` runner pin is being validated. Stable ISO still identifies code snapshot `1af592d`.

Status policy: **evidence-weighted estimate; never equate a source file, marker check, architecture document, or host test with a working end-user feature.**

## CI runner stabilization candidate — 2026-11-12

- The Build workflow used `ubuntu-latest`. Build #1905 ran on Ubuntu 24.04 and passed the full QEMU/storage/production gates; Build #1906 and PR #48 Builds #1908/#1909 ran on Ubuntu 26.04. The 26.04 main run failed at GCC's new `-Werror=unterminated-string-initialization` diagnostics. After PR #48 corrected those test declarations, both 26.04 candidate runs reached QEMU but failed at the first boot with #UD/#GP/triple fault at RIP `0x11a5db`.
- This establishes a runner-image correlation, not proof that Ubuntu 26.04 or the crypto test change caused the triple fault. The failing ELF and linker map were not retained; do not attribute the fault to the storage fix without symbolication.
- Candidate PR #48 now pins the full Build and Stage 1 workflows to `ubuntu-24.04`, the last runner image with a complete 3/3 boot, 108-milestone, AHCI/NVMe persistence and production-smoke pass for the same block/storage source and regression files. `tools/test_ci_runner_image.py` asserts both explicit pins and CI ordering.
- Candidate CI after the pin is pending. Do not claim the pin fixed the kernel boot until the full Build completes. Stable ISO remains `1af592d`; the current `2dac661` code snapshot is not yet release-verified.

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
- **Latest reviewed main:** HEAD `52ef007547e06045bf1c07998d78f581de71779c` is documentation-only after code-bearing commit `2dac661e73655dfc5e6513bbb5a70b0f7791c8c9`. Stage 1 #327 passed; Build #1906 failed during `make check` before QEMU on the Ubuntu 26.04 runner. The last successful full main Build remains #1901 on the older `1af592d` code snapshot.
- **PR #16:** merged on 2026-10-17 as `d6b58292`. Its branch CI reported success and included parallel-build dependency fixes, ISO validation/writer fixes, shared 91-milestone boot test, reproducibility checks, and local intent-parser foundations.
- **Latest post-merge CI:** Main Stage 1 #327 (run `43144281205`) passed for `2dac661`. Main Build #1906 (run `43144281250`) failed in the host crypto test compile, before any QEMU boot. Build #1901 (run `42952878802`) remains the last successful full main Build for older code commit `1af592d`.
- **Merged storage correction:** PR #47's duplicate-completion guard is in main at `2dac661`; the earlier code-equivalent candidate Build #1905 passed 3/3 QEMU boots (108 milestones each), storage persistence and production smoke on Ubuntu 24.04. Current main still requires its own successful post-merge Build.
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

**Current execution baseline:** code-bearing main `2dac661` includes the duplicate block-completion guard, but current main Build #1906 failed before QEMU on Ubuntu 26.04. Candidate PR #48 pins the full Build and Stage 1 workflows to the last fully passing Ubuntu 24.04 image; full candidate QEMU/storage/production validation is pending. The stable release still points to `1af592d`.

1. **Block completion correctness:** verify that `block_complete()` claims a request only while it is present in `device->inflight`, publishes `BLOCK_REQ_COMPLETING` under the device lock, and cannot decrement in-flight accounting or signal completion twice.
2. **Scheduler reliability:** complete the 600-second/four-vCPU soak with latency, fairness, CPU hot-offline and wakeup-race evidence.
3. **Process lifecycle:** failure rollback for exec/ELF/stack/thread creation; exit/wait/reap races; exact address-space and process-slot accounting.
4. **Storage integrity:** continue crash/power-cut and fsck recovery testing, FLUSH ordering, writeback error propagation and physical-device validation.
5. **Drivers and security:** physical PCI/ACPI/DMA/IRQ, NIC, USB, graphics, storage and recovery paths; privilege and fault isolation.
6. **Usable native desktop:** real input/display failure recovery, launcher/files/terminal/settings, accessibility and event-driven zero-render idle.
7. **Browser/media:** compatible licensed components, hardware codec paths, browser isolation and sustained playback measurements.
8. **ZERO AI:** permission-brokered tools, scoped consent, cancellation, audit and rollback before broad automation.
9. **Windows/Android compatibility:** only after ABI, loader, graphics/audio, permission and workload tests are operational.
10. **Performance/release:** measure boot, idle RAM/CPU, storage I/O, thermal behavior, recovery and physical G560 behavior.
11. **Documentation:** update status, validation, index, roadmap and subsystem contracts with each evidence-changing batch; keep estimates unchanged unless feature behavior is verified.

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
- Exact candidate head `f6450de619a4ea052b84d9394fe0d4dd58d98236` passed ZEROOS Build workflow #1626 (run ID `40630820065`) and Stage 1 Scheduler CI #47 (run ID `40630819947`). The build workflow passed the release-policy, production-diagnostics, fail-closed boot-gate and affinity-owner regression tests; `make`, clean `make check`, kernel/ISO verification, repeated QEMU boot matrix, scheduler fault certification, two-boot AHCI/NVMe persistence, production ISO, production desktop/network smoke, and diagnostic preservation. Release publication was skipped for the PR event. These results are for this candidate SHA, not `main`; current-main CI and live release-asset verification remain open. Completion estimate unchanged (~43%); physical hardware and the optional 600-second Stage 1 soak remain separate gates.

## Post-merge update — 2026-10-28: remote affinity reschedule owner

- Merged PR #30 to `main` as `acae3eba7b2f9c6cf2c52038d7b5606c28f4a077`. `task_set_affinity()` now identifies the CPU actually running a `TASK_RUNNING` task under `task_lock`; it rejects a RUNNING task with no observed owner before mutation and requests rescheduling when the new affinity excludes that owner CPU. The prior check incorrectly used the caller's CPU.
- Candidate code head `f6450de619a4ea052b84d9394fe0d4dd58d98236` passed ZEROOS Build workflow #1626 (run ID `40630820065`) and Stage 1 Scheduler CI #47 (run ID `40630819947`). The final PR tip `a533d6ecdd6db4345ed473ecd9240ce287257256` also passed Build workflow #1632 (run ID `40631843123`) and Stage 1 Scheduler CI #53 (run ID `40631843041`). Both Build runs passed the affinity/release/diagnostic/boot-gate regressions, `make`, clean `make check`, QEMU boot matrix, scheduler fault certification, two-boot AHCI/NVMe persistence, production ISO, desktop/network smoke, and diagnostic preservation. Release publication was skipped for PR events.
- The connected exact-main status/workflow lookup has not returned post-merge results; current-main CI is **unverified**, and the live stable ISO has not been rechecked against the post-merge production image. Stage 1's optional 600-second soak and physical-hardware certification remain open. Completion estimate unchanged (~43%).

## Candidate kernel fix — 2026-10-29 (validation pending)
- Identified a process-lifecycle hardening gap: `process_create(parent, ...)` read `parent->state` before proving that `parent` points to an aligned entry in the kernel's process table.
- Candidate branch `fix/process-create-parent-pointer-validation-20261029` adds the provenance check before any parent-field dereference. Scheduler certification now passes pointer value `1` to `process_create` and requires safe rejection without changing the output PID sentinel; the marker is a required boot milestone. A source-order regression test is wired into CI before compilation.
- The exact candidate has not yet completed CI. Do not treat the fix, build, or QEMU marker as validated until its workflow passes. Current-main status lookup has no check entries; post-merge main CI and live release ISO verification remain separate blockers. Completion estimate unchanged.

## Candidate kernel fix — validation update 2026-10-29
- Candidate head `f767841d9e6dd98df07005d44ebaf086472512e0` passed ZEROOS Build workflow #1651 and Stage 1 Scheduler CI #72. In #1651, the new process-parent regression, existing release/diagnostic/boot-gate/affinity regressions, build, clean `make check`, kernel/ISO verification, QEMU boot milestones, scheduler panic-diagnostics certification, two-boot AHCI/NVMe persistence, production ISO build, production desktop/network smoke test, and diagnostic/artifact steps passed. Actual release publication was skipped on the PR event.
- The process fix is candidate-tested but not merged. These results do not verify the current `main` SHA `12cdca17f8359fdb6884ec6f1b5b67cc2a78071b`, nor the live stable release asset. The exact updated documentation tip must also pass CI before merge. Completion estimate unchanged (~43%); physical hardware, Stage 1 long soak, recovery, and later product stages remain open.

## Current-main review and process API hardening — 2026-10-29
- Latest main commit observed: `b99e5705fa56558637b1983f5f45992d7ae509cb`, which added parent-pointer provenance validation to `process_create`.
- The exact-main combined-status lookup returned no entries. The connected workflow-run lookup only exposes PR-triggered runs, so main CI for this exact SHA is **unverified**, not presumed green or red.
- Candidate branch `fix/process-api-pointer-provenance-20261029` extends the same guard to public APIs receiving a `struct process *`: lifecycle pins/reap, child wait/query, thread reservations, resource limits, address-space operations and counters. It adds a boot-time invalid-pointer probe, a required serial milestone, a source-order regression test and a CI step before build/QEMU.
- Candidate validation is pending until CI for the exact PR head passes. No new boot pass or completion credit is claimed by source edits alone.
- Release blockers remain: post-merge main workflow and live stable ISO verification; Stage 1 long soak; physical-hardware matrix; recovery and later-stage product acceptance. Overall estimate unchanged (~43%).

## Main-branch review — 2026-10-30: process API pointer provenance merged

- Current main commit: `4e3691c64079d6a50f3a0cf5230336b49dfb0bee` (squash merge of PR #33). The kernel now validates caller-supplied `struct process *` arguments against the aligned static process table before field dereferences across 20 public lifecycle, child-wait/query, thread-reservation, limits, address-space and counter APIs.
- Candidate head `2c7a0e9294890341f969c614cde2bae00243907b` passed ZEROOS Build workflow #1677 (run ID `40813814987`) and Stage 1 Scheduler workflow #1677 (run ID `40813814986`). Build steps passed: release-policy, diagnostic-retention, fail-closed QEMU-gate, affinity-race, invalid-parent and process-API pointer regressions; native build; reproducible release gate; kernel/ISO verification; QEMU boot test; scheduler fault certification; two-boot AHCI/NVMe persistence; production ISO build; production desktop/network smoke; and diagnostic/artifact preservation. The bootable GitHub Release publication step was skipped on the PR event; this is not a post-merge release verification.
- After merge, the combined-status lookup for `4e3691c64079d6a50f3a0cf5230336b49dfb0bee` returned no status entries. The connected workflow-run helper only returns PR-triggered runs, so exact post-merge main CI is **unverified**, not presumed green or red. The live stable `zeroos.iso` has not been independently compared with a production image from a post-merge main run.
- Open follow-ups include PR #32 and #25 documentation reconciliation, PR #27 Stage 1 scheduler certification, PR #26 QEMU evidence hardening, the optional 600-second Stage 1 soak, and physical-hardware certification. Recovery and later desktop/browser/media/AI/compatibility work remain roadmap items, not completed features.
- Overall completion estimate remains unchanged (~43%). This process-pointer hardening and its CI evidence do not justify a percentage increase.


## Main review and atomic user-copy hardening — 2026-10-31

- Confirmed current `main` HEAD by comparing `main` with itself: `9f7b715ee9c15506514c4e563ea78801d9c77e9d`. The combined-status lookup returned no entries for this exact SHA; current-main CI is **unknown/unverified**, not a pass or failure.
- Latest inspected successful candidate Build workflow #1686 (run ID `40994113537`) completed its build job successfully. Its recorded steps include the release/diagnostic/QEMU/scheduler/process regression checks, native build, clean `make check`, QEMU boot test, scheduler panic-diagnostics, two-boot AHCI/NVMe persistence, production ISO build, production desktop/network smoke, and artifact preservation. The GitHub Release publication step was **skipped** because this was PR validation. Do not attribute that run to current `main` or to live release publication.
- Code review found a mapping-lifetime TOCTOU in both syscall user-copy helpers and the exec image/argv/envp copier: they checked the user range under `process_lock`, released the lock, then translated and dereferenced physical pages. A concurrent thread could unmap/remap the page in that interval.
- Candidate branch `fix/atomic-process-user-copy-20261031` adds process-owned copy-from/copy-to operations that keep range validation, page translation, and bounded copying under the same process lock. Syscall and exec paths are routed through these operations. A guest probe checks cross-page read/write correctness and rejects an unmapped tail; a Python source regression checks lock ordering and call-site routing.
- **Candidate only:** the code, test, boot milestone, workflow step, and this documentation update are committed on the candidate branch. No candidate test pass is claimed until its exact PR workflow finishes. Overall stage estimates remain unchanged (~43% overall).
- Release/acceptance blockers remain: exact post-merge `main` CI and live stable ISO byte verification; the 600-second Stage 1 scheduler soak; physical Lenovo G560/device certification. Open PRs #26 (boot-test integrity) and #27 (scheduler certification gaps) remain unmerged and their branches are substantially behind/diverged from current `main`; review/rebase before integration.

## Evidence update — 2026-11-01: atomic user-copy fix merged

- PR #35 merged to `main` as `6521dcf3e2b7d0caef25cb47515b8572f0358fcc`. Latest `main` observed at review time is this commit (repository commit search); comparing `main` with the merge SHA reports identical refs.
- The fix adds `process_address_space_copy_from()` / `process_address_space_copy_to()`. Process identity/state checks, whole-range user permission validation, page translation and bounded physical-byte copying now execute under `process_lock`, serializing copies against the process map/unmap/destroy paths. Syscall and exec copy helpers use the process-owned API.
- Candidate PR head `40d0184debf7acd80b5358eb82fa4834e5e0ea29` was validated by ZEROOS Build workflow #1702 (run ID `41164323843`). The job checked out GitHub's PR integration merge commit `59654aa954f56a56eaa1ff7ea444b3a6e486da45` (PR #35 merged into then-current main), not the final squash merge SHA.
- Build #1702 passed the release-policy, diagnostic-retention, fail-closed boot, scheduler-affinity, invalid-parent, process-API pointer, and new process-user-copy regression scripts; native `make`; clean `make check`; kernel/ISO artifact checks; repeated QEMU boot gate; SMP4, NX-disabled and failed-IPI recovery boots; scheduler panic-diagnostics; two-boot q35 AHCI/NVMe persistence; production ISO build; production desktop/network smoke; and diagnostic/artifact preservation. The new atomic-copy source regression ran 5 tests and passed. Stage 1 Scheduler workflow #123 (run ID `41164323767`) also passed native build and scheduler certification.
- PR-event validation skipped the GitHub Release publication step. The run's successful production-artifact step is not proof that a GitHub Release was published. The combined-status lookup for post-merge main SHA `6521dcf...` returned no entries, and the connected workflow-run helper exposes PR-triggered runs only. Therefore post-merge main CI and current live stable-ISO byte identity remain **unverified**.
- Still open: execute/retain the optional 600-second Stage 1 scheduler soak; physical Lenovo G560 and device-matrix certification; reconcile open/stale PRs (notably #26/#27 scheduler/boot evidence, #14 broader runtime work overlapping this user-copy fix, and #23 release-note ordering which should be checked against the already merged release workflow). Browser/media/AI/compatibility requirements remain roadmap work unless separately evidenced.
- Previous 2026-10-31 candidate-pending statements above are superseded by this merge and candidate CI evidence. Overall completion estimate remains unchanged (~43%); this focused race fix does not justify a percentage increase.


## Evidence update — 2026-11-03: stable release publisher race (candidate fix)

- Latest reviewed `main` is `28112603ebbd6a4bfa6cc93a6c6cb0be84cc2f3a`. Main push Build #41265715805 (run ID `41265715805`) and Stage 1 Scheduler #41265715839 both completed successfully. Build #41265715805 passed the three-boot QEMU gate, SMP4, NX-disabled, failed-IPI recovery, scheduler fault diagnostics, two-boot AHCI/NVMe persistence, production ISO build, production desktop/network smoke, and GitHub Release publication.
- The release pointer is nevertheless stale. The latest-main run logged stable ISO SHA-256 `eea7d77da1be0dc00b9b8a6dbb11035714d60eb9eb06ead3ce29037bb89c43a1` for commit `2811260`. A later-finishing older main run #41265707574 for commit `3ba0dbe` logged `c80f4270d9f34be9f0d6ba57e93ee5ddb77a3ddb08c97273f7386db8ba26afd0`; the current Release API reports that same older digest for `zeroos.iso` and release notes naming `3ba0dbe`, while the immutable `zeroos-2811260.iso` asset has the newer `eea7d77...` digest. This confirms out-of-order main workflows overwrote the stable alias after the newer build.
- Root cause: `.github/workflows/build.yml` had no workflow-level concurrency control. Candidate branch `fix/serialize-stable-release-publish-20261103` adds per-ref serialization with `cancel-in-progress: false` and a latest-main SHA guard after immutable-asset verification but before stable-alias mutation. The release-policy regression now checks both protections.
- This is candidate workflow/test code, not yet merged. Its PR CI and a post-merge main release run are required. The live stable alias and notes remain stale until a new serialized main run republishes and verifies the current image.
- Separate open PR #37 addresses Ring-3 interrupt GS ownership; its candidate Build #1740 has passed the source regressions, native build, clean check, kernel/ISO validation and initial boot gate, while later fault/storage/production gates were not yet complete at this update. Do not infer merge readiness from partial steps.
- Overall implementation estimate unchanged; no OS feature-completion credit is assigned for release workflow hardening.


## Follow-up evidence — 2026-11-03: candidate checks and remaining review findings

- PR #38 candidate `ad59ca140c69d28d5d2462e090419d13a96b7cf1`: the release-policy regression (including the new serialization/stale-SHA assertions), diagnostic retention, fail-closed boot-gate, scheduler-affinity, invalid-parent, process-pointer and user-copy regressions all passed. Native build, reproducible release gate, and kernel/ISO verification passed. PR Build #41574414947's Boot test remains `in_progress` (started 03:50:32Z); same-SHA push Build #41574386407's Boot test remains `in_progress` (started 03:50:09Z). Stage 1 Scheduler #41574414962 passed. No full candidate QEMU result is claimed.
- PR #37 Build #1740 and Stage 1 Scheduler #161 subsequently completed successfully, including repeated QEMU, scheduler fault diagnostics, two-boot AHCI/NVMe persistence, production ISO and production boot smoke. PR #37 remains open.
- Code review found a separate unclosed path in PR #37: `kernel/task.c:dispatch_locked` passes a selected task's `interrupt_frame` to `context_switch_ex`, while `kernel/context.S:.Lresume_frame` restores that frame and executes `iretq` without the GS-ownership restore used by `boot/isr.S`. Since a selected frame may be Ring 3, PR #37 must be reviewed for this cooperative-to-interrupt-frame handoff before merge. This is a source-path finding; no runtime failure is claimed.
- Main run #41265715805's retained serial diagnostics show the 1024x768 desktop, compositor, scanout and input milestones, but also explicitly report `net0: no supported network device present; network unavailable`. The production smoke step passed; actual NIC functionality is not demonstrated by that QEMU configuration.
- Stable release race remains unresolved on main until PR #38 passes full CI, is merged, and a new main release run verifies that the stable alias and notes match its exact commit. Overall completion estimate unchanged.

## Final evidence update — 2026-11-04: stable release publication race fixed

- PR #38 is merged to `main` as `84f8a5cf23e03098419d92231a25908174e3c21f`. The workflow now serializes Build/release runs per Git ref with `cancel-in-progress: false`, and checks the live `main` SHA after immutable ISO verification but before mutating `zeroos.iso` or final release notes. Stale runs retain their versioned asset but skip the mutable stable pointer.
- Regression coverage in `tools/test_release_publish_guard.py` verifies per-ref non-cancelling concurrency, stale-SHA guard ordering, versioned-asset verification, stable-alias rollback, final-note ordering, and shell syntax.
- Candidate exact-tip validation: Build PR #41574953029 and push Build #41574947638 **SUCCESS**; Stage 1 Scheduler PR #41574953139 and push #41574947653 **SUCCESS**. The full Build gates passed: all pre-build regressions (including the release-publisher regression), native build, clean `make check`, kernel/ISO verification, three sequential QEMU boots, SMP4, NX-disabled, failed-IPI recovery, scheduler panic diagnostics, two-boot q35 AHCI/NVMe persistence, production ISO, and production desktop/network smoke. Release publication was skipped on candidate refs as designed.
- Post-merge exact-main validation: Build #41766774251 **SUCCESS** and Stage 1 Scheduler #41766774267 **SUCCESS**, both for merge SHA `84f8a5cf...`. The main Build passed the same regression/build/clean-check/ISO gates, QEMU boot variants, scheduler fault diagnostics, AHCI/NVMe two-boot persistence, production ISO and production boot smoke; the GitHub Release publication step also succeeded.
- The release job downloaded and byte-compared the stable `zeroos.iso` against `build-production/zeroos.iso`, then logged SHA-256 `297ac5218d9c4e9b4b59cb85638a087fb2633a569c91bd40b8dc23e0e7a6016e`. The GitHub Release API reports the same digest and size (12,369,920 bytes) for `zeroos-84f8a5c.iso` and `zeroos.iso`; release notes identify commit `84f8a5c`. The previously observed stale stable alias (`3ba0dbe`) is therefore repaired for this release.
- Production QEMU serial evidence shows the RTL8139 device attached, DHCP bound to `10.0.2.15`, and the first-hop ARP entry resolved. This is QEMU RTL8139 evidence, not physical NIC or Lenovo G560 certification.
- Still outstanding: the optional 600-second scheduler soak; physical Lenovo G560/device matrix; PR #37's separate Ring-3 GS restoration review gap; PR #36 range-unmap hardening review/merge; and broader driver/security/recovery, desktop hardware, browser/media/AI/compatibility requirements. Preserve these as incomplete. No overall completion percentage increase.

## Latest follow-up — 2026-11-04: documentation-tip main build and release verified

- The documentation snapshot at `9d77ad3befb6c782b548020d67d6af38401533b2` also passed exact-main Build #41767825881 and Stage 1 Scheduler #41767825887 (**SUCCESS** for both).
- Build #41767825881 passed the release-policy and existing regression suite, native build, clean `make check`, kernel/ISO verification, repeated QEMU boot/SMP4/NX-disabled/failed-IPI gates, scheduler panic diagnostics, two-boot AHCI/NVMe persistence, production ISO, production boot smoke, diagnostic preservation, and GitHub Release publication.
- Release notes now identify `9d77ad3`. Versioned `zeroos-9d77ad3.iso` and stable `zeroos.iso` both report SHA-256 `cebb728732f1cd04e2058182842e53c5d988cc0463fc7bb2ac4b8217314da413` and size 12,369,920 bytes. The workflow log explicitly confirms that stable `zeroos.iso` was downloaded and byte-compared with the boot-tested production ISO.
- Production QEMU serial evidence includes RTL8139 attach, DHCP lease `10.0.2.15`, and gateway ARP resolution. This remains QEMU virtual-NIC evidence, not physical NIC or G560 certification.
- This final follow-up commit changes only these four documentation files and intentionally skips CI; the boot-tested/released OS code snapshot remains `9d77ad3`. The release-ordering blocker is closed for the current published image. Scheduler soak, physical hardware, PR #37 GS-return-path correction, PR #36 range-unmap review, and broader roadmap requirements remain open. No completion estimate increase.

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

## Current-main review and Multiboot2 boot-range correction — 2026-11-05

- Reviewed `main` at `c3c4eb6ba1a508ff8258d4cf96864317de2da6b4`; compare `main...c3c4eb6ba1a508ff8258d4cf96864317de2da6b4` is identical.
- The Ring-3 GS frame-resume correction (`0336bfb407f7194838cc2508dba61d3264aa3ce5`) and process range-unmap preflight correction (`c3c4eb6ba1a508ff8258d4cf96864317de2da6b4`) are now in main. Earlier entries in this document that described those two fixes as candidate-only are superseded by this evidence update.
- Candidate Build workflow #41963710252 completed successfully. Its job steps report PASS for the release-policy/diagnostic/QEMU-gate/scheduler-affinity/GS/process regressions, native build, clean `make check`, kernel/ISO verification, repeated QEMU boot, scheduler fault diagnostics, two-boot AHCI/NVMe persistence, production ISO generation, and production desktop/network smoke. The GitHub Release publication step was skipped on the PR event.
- Stage 1 Scheduler workflow #41963710086 completed successfully, including native x86-64 kernel build and Stage 1 scheduler certification.
- The combined-status query for the current main SHA returned no status entries; the available workflow-run lookup exposes PR-triggered runs only. Do not claim a post-merge push workflow from the candidate runs above.
- The stable release asset has not been independently verified against a production ISO built from `c3c4eb6ba1a508ff8258d4cf96864317de2da6b4`; the candidate run skipped GitHub Release publication. Stable-release verification remains open for this main snapshot.
- New boot finding: `memory_init()` rejected a Multiboot2 information-block start address above 16 MiB even though `boot/boot.S` identity-maps 1 GiB and the allocator manages the first 512 MiB. Multiboot2 does not require the information block to start below 16 MiB; a valid handoff above that address could leave the allocator empty and prevent boot.
- Candidate correction: branch `fix/multiboot-info-window-20261105` validates the 8-byte-aligned information-block start against the boot's 1 GiB identity map, independently caps the block length at 16 MiB, and verifies the complete block remains inside that identity map. The allocator still manages only the first 512 MiB; `reserve_range()` clips metadata reservations to that managed window, including a block that straddles its upper boundary. The boot-time boundary self-test covers 32 MiB, an address above 512 MiB, and the last identity-mapped page.
- Added `tools/test_multiboot_info_range.py`, wired it before toolchain installation in the Build workflow, and added the required serial milestone `ZEROOS: Multiboot2 info range policy self-test passed.`
- Candidate Build/QEMU for this new correction has not yet completed. No new boot pass or physical-hardware result is claimed.
- Remaining release/certification blockers: post-merge main workflow and current-main stable ISO verification; 600-second scheduler soak; physical Lenovo G560/device matrix. PR #26/#27 boot/scheduler evidence branches require reconciliation/rebase against current main. QEMU RTL8139/DHCP evidence does not certify a physical NIC.
- Overall implementation estimate remains approximately 43%; no percentage increase is assigned for this validation-policy fix.

## Candidate validation correction — PR #40 Build #1770 — 2026-11-05

- PR #40 first Build workflow #1770 failed at `Verify Multiboot2 information range handling`, before toolchain installation, native release build, clean `make check`, or any QEMU boot. The exact failing assertion expected the runtime boundary self-test to include a 17-byte (non-8-byte-aligned) total-size rejection; the C self-test did not yet contain that case. This was a regression-coverage mismatch, not a compiler or guest boot failure.
- Stage 1 Scheduler workflow #191 completed SUCCESS on the initial PR head, including native x86-64 kernel build and Stage 1 scheduler certification. It does not substitute for the full Build/QEMU gate.
- Corrected the C runtime self-test to reject the 17-byte size and made the source regression normalize whitespace before checking the call. The current candidate head is `c2010a2175ea6f73b0057143def8d4172fc54d65`.
- The exact-head rerun has not yet been returned by the available workflow lookup. Do not claim the new regression, native release build, QEMU, storage, or production desktop gates passed until a completed run for the updated PR head is inspected.
- PR #40 remains open and unmerged. Main's post-merge CI and stable ISO verification for `c3c4eb6ba1a508ff8258d4cf96864317de2da6b4` remain separate release blockers. Overall completion estimate remains unchanged.
## PR #40 latest validation snapshot — 2026-11-05

- Stage 1 Scheduler workflow #203 completed SUCCESS on PR tip `7043444b45c5cd068651b7a2bb054851ae5d18cb`, including native x86-64 kernel build and Stage 1 scheduler certification.
- Build workflow #1782 for that same PR tip is pending and had not yet created a job at the last lookup. Build #1770 is the earlier failed run caused by the missing 17-byte runtime self-test case; Build #1781 was cancelled during PR close/reopen to trigger a fresh `reopened` validation event.
- The corrected C boundary case and whitespace-normalized source assertion are present on the current PR branch. No full Build, clean `make check`, QEMU, storage, or production-smoke pass is claimed for the corrected PR tip.
- PR #40 remains open/unmerged. PR #26 is 6 ahead/30 behind main, PR #27 is 1 ahead/29 behind, and broad PR #14 is 19 ahead/321 behind; rebase/reconcile before considering those branches.
## Latest PR #40 workflow snapshot — 2026-11-05

- On the reviewed PR tip `7043444b45c5cd068651b7a2bb054851ae5d18cb`, Stage 1 Scheduler #203 completed SUCCESS, including native kernel build and Stage 1 scheduler certification.
- Build #1770 failed at the new Multiboot2 source regression before toolchain installation or QEMU; the missing 17-byte runtime self-test case and whitespace-sensitive assertion were corrected afterward.
- Build #1782 was cancelled during the PR close/reopen used to request a fresh PR validation event. The latest observed Build #1789 for documentation tip `beaba6139cc06426ce5dc0243f6978a48e842e1f` is pending with no job started. Stage 1 #209/#210 were also queued/in progress at the last lookup; #203 is the completed pass.
- Therefore, the corrected source regression has not yet been shown passing in a completed Build workflow, and no QEMU, storage, or production ISO result exists for PR #40. Keep it open and unmerged.
- Main remains `c3c4eb6ba1a508ff8258d4cf96864317de2da6b4`; post-merge main CI and stable ISO verification for that SHA remain open. Overall completion estimate unchanged.
## Final candidate-head validation snapshot — 2026-11-05

- Final reviewed code/test head: `9c14a4a4fed9d0f3a8e21e3f5c2ecbf45a819a99` (Multiboot2 information may be anywhere below the 1 GiB identity-map limit; total block size remains capped at 16 MiB; reservation is clipped to the allocator's first 512 MiB).
- Build #1810 for this head was pending with no job started at the last lookup. Build #1809 was cancelled. The earlier Build #1770 failed at the first version of the new source regression; the missing 17-byte size-alignment case was added afterward.
- Stage 1 #230 completed the native x86-64 kernel build and Stage 1 scheduler certification steps successfully, but the overall workflow conclusion was CANCELLED. Stage 1 #231 was pending with no job started. Do not label the current candidate's Stage 1 workflow green.
- No full Build, clean `make check`, QEMU boot, storage, production ISO, or desktop smoke result is established for the final 1 GiB-boundary code/test head.
- Local checkout was unavailable because the container could not resolve `github.com`; QEMU is not installed in the local environment. GitHub Actions is the required execution path.
- PR #40 remains open and unmerged. Post-merge main workflow and stable ISO verification for main `c3c4eb6ba1a508ff8258d4cf96864317de2da6b4` remain open. Completion estimate unchanged.
## PR #40 latest CI result — 2026-11-05

- Stage 1 Scheduler #231 completed SUCCESS on code/test head `9c14a4a4fed9d0f3a8e21e3f5c2ecbf45a819a99`. Both `Build native x86-64 kernel` and `Run Stage 1 scheduler certification` passed. This verifies compilation and Stage 1 scheduler certification for the 1 GiB Multiboot2-boundary code.
- Build #1810 was cancelled during overlapping PR validation events. Latest full Build #1816 for documentation tip `9c1d3b1d5d9fc04c0857056b2fe5dea6a0cd9928` remained pending with no job started at the last lookup. No new Multiboot2 source-regression, full `make check`, QEMU, storage, production ISO, or desktop result is established.
- The initial Build #1770 source-regression mismatch was corrected; the corrected regression has not yet completed in a successful Build workflow. PR #40 remains open/unmerged.

## 2026-11-06 — PR #40 merged: Multiboot2 information-block range

- Current main after merge: `f20ea855fc30baf54cdc5d45915d357266f2ae6e`. PR #40 merged the Multiboot2 handoff-range correction.
- The kernel now validates the information-block start against the first 1 GiB identity-mapped window, applies the 16 MiB limit to the total block size, requires the Multiboot2 alignment constraints, and reserves the block's intersection with the allocator-managed first 512 MiB. The allocator itself remains 512 MiB; no wider physical-memory support is claimed.
- Candidate code/test head: `9c14a4a4fed9d0f3a8e21e3f5c2ecbf45a819a99`; final documentation tip validated by Build #1822 / run ID `41968388714` and Stage 1 Scheduler #243 / run ID `41968388659`.
- **Build #1822 completed SUCCESS.** Its job passed the release/diagnostic/QEMU/scheduler/process regressions, the new Multiboot2 range regression, native build, clean `make check`, kernel/ISO verification, repeated QEMU boot, scheduler fault diagnostics, q35 AHCI + NVMe two-boot persistence, production ISO build, and production desktop/network smoke. The GitHub Release publication step was skipped because the run was PR-triggered.
- **Stage 1 Scheduler #243 completed SUCCESS**, including native x86-64 kernel build and Stage 1 scheduler certification.
- Post-merge main verification remains distinct: the combined-status lookup for merge commit `f20ea855fc30baf54cdc5d45915d357266f2ae6e` returned no status entries, and the available commit-workflow lookup exposes PR-triggered runs only. Do not label the post-merge push workflow green from candidate evidence.
- The previously recorded stable-ISO byte comparison applied to the earlier `9d77ad3` image. No live stable-asset comparison against the new `f20ea85` main build is established here; main release publication and exact stable-asset verification remain open.
- Remaining evidence gaps: 600-second scheduler soak, physical Lenovo G560/device matrix, and post-merge main/release verification. Relevant open engineering queues include PR #26 (boot evidence/soak), #27 (scheduler certification), and #14 (broad cross-stage hardening; reconcile overlapping work before integration).
- **Implementation estimate unchanged (~43% overall).** This boot-handoff correction does not complete a stage or certify physical hardware.


## 2026-11-07 — latest main CI and stable-release freshness defect

- Latest `main` HEAD is `9a0d4d30d5cef5ca4842033c6a75abd589b1c93b`; its 12 commits after code-bearing `f20ea855fc30baf54cdc5d45915d357266f2ae6e` modify only the four master-document Markdown files. The latest executable code snapshot remains `f20ea855`.
- Main push Build #1823 (`42152327282`) completed successfully on `f20ea855`. Its regression steps, native build, clean `make check`, kernel/ISO checks, repeated QEMU boot matrix, scheduler fault diagnostics, AHCI/NVMe two-boot persistence, production ISO, production desktop smoke, and artifact publication steps all report success.
- Build #1823 uploaded and byte-verified the immutable `zeroos-f20ea85.iso`, but its log explicitly skipped the mutable stable alias because the current main SHA had advanced through documentation-only commits. This is a release-pipeline false-stale condition, not a kernel/QEMU failure.
- Current release metadata confirms the mismatch: `zeroos-f20ea85.iso` has SHA-256 `e40ac5276a83dc24f45866024a2871aa1ce7b6eaafc3dda64a4e22e9bee764d2`; stable `zeroos.iso` has SHA-256 `ef092a8c9cb2c7b98ec8af20ce53239b8d3b36cc4525ee833ef94376e2404e53`, matching the older `zeroos-c3c4eb6.iso`. Release notes still identify `c3c4eb6`.
- Candidate fix `fix/release-guard-docs-only-20261107`: fetch current main and compare the build SHA with current main while excluding only `docs/**` and Markdown files. Documentation-only advances may publish the already boot-tested code snapshot; any kernel, source, test, build, boot-config or workflow change still causes the stale-run guard to skip publication. Existing serialization, immutable-asset verification, stable backup/rollback, byte comparison and final-note ordering are retained.
- Updated `tools/test_release_publish_guard.py` to exercise the exact Git pathspec in a temporary repository: documentation-only successors must pass, while a kernel-source successor must be rejected. PR validation must still pass before merge.
- The stable release is currently behind the latest code-bearing main image. Do not call the release current until a successful main run updates `zeroos.iso` and release notes, and the downloaded stable bytes match the same run's production ISO.
- No kernel feature or completion percentage is added by this release fix. The previous overall estimate remains unchanged. Physical G560 certification and the 600-second scheduler soak remain separate outstanding gates.


## 2026-11-08 — PR #41 merged; post-merge validation in progress

- PR #41 (release freshness correction) merged to `main` as `fff32e9b35fa61f1911d43d45fc1b99aabfbdd57`. The merge changes the release publisher and its regression test plus these master documents; it does not change kernel source. The latest code-bearing kernel baseline therefore remains `f20ea855fc30baf54cdc5d45915d357266f2ae6e` until another executable-code commit lands.
- Candidate PR Build #1826 (run `42332516298`) and branch-push Build #1825 (run `42332496757`) both completed SUCCESS. Their job steps passed the release-publication regression, diagnostic-retention regression, fail-closed QEMU regression, Multiboot2 range regression, reproducible native build/check gate, three repeated QEMU boots (104 milestones each), SMP4, NX-disabled boot, failed-IPI recovery, scheduler panic-diagnostics, AHCI/NVMe two-boot persistence, production ISO build, and production desktop/network smoke. PR-event GitHub Release publication was correctly skipped.
- Stage 1 Scheduler #247 (run `42332516393`) and #246 (run `42332496715`) both completed SUCCESS.
- Post-merge main Build #1827 (run `42442854152`) was `in_progress` at the last check. Main Stage 1 Scheduler #248 (run `42442854128`) completed SUCCESS. Do not attribute candidate QEMU/storage evidence to the merged main run until #1827 completes.
- The live release API was checked after the merge: stable `zeroos.iso` still had SHA-256 `ef092a8c9cb2c7b98ec8af20ce53239b8d3b36cc4525ee833ef94376e2404e53`, and release notes still identified `c3c4eb6`. The immutable `zeroos-f20ea85.iso` asset had SHA-256 `e40ac5276a83dc24f45866024a2871aa1ce7b6eaafc3dda64a4e22e9bee764d2`. The stable alias is therefore still stale at this observation; publication and byte identity for the new main build are pending.
- The new guard fetches current main and compares the build tree while excluding only `docs/**` and Markdown files. This documentation-only update is intentionally kept separate from kernel changes; if Build #1827 reaches publication after this commit, its live diff check should permit the already boot-tested code snapshot. The main run's own log and final release bytes remain the acceptance evidence.
- Remaining gates: 600-second scheduler soak; physical Lenovo G560 boot/device matrix; physical SATA/NVMe, NIC, USB and graphics validation; expanded storage recovery/fault testing. Preserve all Stage 0–10 and canonical product requirements. Overall estimate remains unchanged (~43%).


## 2026-11-09 — post-merge release verified; QEMU timeout evidence hardening candidate

- Latest `main` HEAD: `1b2caeb7ce0a735f0b792c2f114ceaedc5a5062e` (documentation-only, `[skip ci]`). The release/CI code commit is `fff32e9b35fa61f1911d43d45fc1b99aabfbdd57`; the latest kernel-source baseline remains `f20ea855fc30baf54cdc5d45915d357266f2ae6e`.
- Main Build #1827 / run `42442854152` completed SUCCESS on `fff32e9`. Its exact job steps passed the release-policy regression, production-diagnostic retention regression, fail-closed QEMU regression, scheduler-affinity regression, Ring-3 GS ownership, process-parent/API/user-copy/range-unmap regressions, Multiboot2 range regression, native build, reproducible release gate, kernel-image verification, boot test, scheduler panic-diagnostics, AHCI/NVMe two-boot persistence, production ISO build, production desktop/network smoke, and GitHub Release publication.
- Main Stage 1 Scheduler #248 / run `42442854128` completed SUCCESS: compiler installation, native x86-64 kernel build, and Stage 1 scheduler certification.
- Build #1827's release log explicitly reports that main advanced only through documentation files and that it is publishing the boot-tested `fff32e9` code snapshot. It then reports: `ZEROOS: stable release ISO matches the boot-tested production ISO (sha256=18cfe872ab7c946d6e4adc75e411043971d5e3878eb4a4df499e4320b94af15a).`
- GitHub Release metadata confirms `zeroos-fff32e9.iso` and `zeroos.iso` are both 12,369,920 bytes and both have SHA-256 `18cfe872ab7c946d6e4adc75e411043971d5e3878eb4a4df499e4320b94af15a`. Release notes identify commit `fff32e9`. The P0 stable-alias alignment blocker is **CLOSED for this boot-tested code snapshot**. The image is a legacy-BIOS/SeaBIOS-tested live ISO, not a disk installer and not physical-G560 certification.
- New boot-test audit finding: `tools/boot_test.sh` accepts QEMU exit status 124 or 137 but previously did not verify that GNU `timeout` had actually reached its requested deadline. An early QEMU exit with status 124, or an external SIGKILL yielding 137, could be mistaken for a completed observation if serial milestones had already been emitted.
- Candidate branch `fix/boot-timeout-duration-guard-20261109` changes the gate to use `timeout --verbose`, requires the supervisor's explicit `sending signal TERM` deadline marker before milestone acceptance, removes stale timeout logs, and retains `timeout.log` as a CI artifact. The fail-closed regression now simulates early 124/137 exits and requires them to fail before milestone evaluation. Candidate CI is pending; this is test-harness hardening, not a new kernel feature or QEMU pass.
- Open evidence gates remain: execute the optional 600-second/four-vCPU scheduler soak; certify physical Lenovo G560, SATA/NVMe, NIC, USB, and graphics behavior; expand storage crash/recovery fault injection. PR #26 (boot evidence/soak) and PR #27 (scheduler certification) are stale/diverged and need selective reconciliation; PR #14 is a broad integration branch and must not be merged wholesale.
- Preserve all Stage 0–10 and canonical product requirements. Overall estimate remains approximately 43%; no completion credit is assigned for release-pipeline or test-harness changes.



## 2026-11-09 — first boot-timeout candidate CI correction

- Candidate PR #42 Build #1829 (run `42558109355`) and PR Build #1830 (run `42558124850`) stopped before compiler installation/native build/QEMU at the existing `tools/test_interrupt_gs_entry.py::test_qemu_boot_timeout_force_kills_paused_emulator` source assertion.
- The failure was an outdated test expectation for the exact command string. The candidate correctly added `LC_ALL=C timeout --verbose` and the deadline TERM-marker check, but the cross-suite GS/boot regression still asserted the previous unprefixed `timeout --kill-after=5s` command.
- Updated the cross-suite regression to assert the new verbose command, the retained timeout log, and that the deadline marker is checked before serial milestones. Removed an unused test-module import and clarified fresh-log comments.
- This is a test-contract mismatch, not a kernel compile, guest boot, or QEMU failure. No native build or QEMU result is claimed for Build #1829/#1830. A new exact-head Build is required.


## 2026-11-09 — QEMU timeout-deadline guard merged

- PR #42 merged to main as 1a57d5cebe0e1eff87fd10450be841708de4e01d. Scope: tools/boot_test.sh, fail-closed boot-gate tests, the cross-suite boot/GS source assertion, CI artifact retention, and these master documents. No kernel source was changed.
- The previous gate accepted QEMU exit status 124 or 137 without proving that GNU timeout had reached the requested observation deadline. The new gate runs LC_ALL=C timeout --verbose --kill-after=5s, requires the explicit 'timeout: sending signal TERM to command' marker before checking serial milestones, removes stale timeout.log before each iteration, and retains the fresh log in the boot-diagnostics artifact.
- Regression tools/test_boot_gate_fail_closed.py now rejects early 124/137 results before milestone evaluation and preserves the full-window timeout cases. The cross-suite tools/test_interrupt_gs_entry.py assertion was updated to verify the new command and guard ordering.
- Exact candidate head 3f36bb5e0eb9de9bcd16df39f912f382a37716b2: Build #1833 (push, run 42558262851) and Build #1834 (PR, run 42558267310) both completed SUCCESS. Both Build jobs passed the release-policy, diagnostic-retention, fail-closed boot, scheduler-affinity, GS, process-parent/API/user-copy/range-unmap, and Multiboot2 regressions; native x86-64 build; reproducible release gate; kernel-image verification; boot test; scheduler panic diagnostics; AHCI/NVMe two-boot persistence; production ISO build; production desktop/network smoke; and diagnostics/artifact retention. The PR-event release-publication step was skipped by design.
- Candidate Stage 1 Scheduler #254 (run 42558262771) and #255 (run 42558267278) both passed compiler setup, native kernel build, and Stage 1 certification.
- Candidate Build #1833 boot-diagnostics artifact 13648807591 contains timeout.log with the expected deadline marker: timeout: sending signal TERM to command 'qemu-system-x86_64'. The job log records all three repeated QEMU boots passing all 104 configured milestones; it also records the 32-bit entry and long-mode markers on each iteration.
- Earlier candidate Build #1829/#1830 failed before compilation because an existing GS/boot source assertion still expected the old command literal. The assertion was updated; exact final-head Build #1833/#1834 passed. Those earlier failures were test-contract mismatches, not guest boot failures.
- Post-merge main Build #1835 (run 42560302805, commit 1a57d5d) is in progress at the reproducible release gate at this documentation snapshot. Main Stage 1 Scheduler #256 (run 42560302640) completed SUCCESS. Do not attribute candidate QEMU/storage/desktop results to the post-merge main run until #1835 completes.
- The currently published stable and versioned ISO assets remain aligned to the previous release-tested snapshot fff32e9, both SHA-256 18cfe872ab7c946d6e4adc75e411043971d5e3878eb4a4df499e4320b94af15a. The new merge has not yet had its own post-merge main Build/release verification; no claim is made that the stable alias identifies 1a57d5d.
- Remaining gates: complete main Build #1835 and verify its boot-test, storage, production smoke, and release steps; execute the 600-second/four-vCPU scheduler soak; certify physical Lenovo G560, SATA/NVMe, NIC, USB and graphics; continue storage crash/recovery and kernel/process lifecycle hardening. Preserve every Stage 0–10 and canonical product requirement. Overall estimate remains unchanged (~43%); this test-integrity fix does not add a kernel feature.

## 2026-11-09 — post-merge main boot and release verified

- Main Build #1835 (run 42560302805) completed SUCCESS on code-bearing commit 1a57d5cebe0e1eff87fd10450be841708de4e01d. Main Stage 1 Scheduler #256 (run 42560302640) completed SUCCESS.
- Build #1835 passed every configured job step: release-policy and diagnostic-retention regressions; fail-closed QEMU boot regression; scheduler-affinity, Ring-3 GS, process-parent/API/user-copy/range-unmap, and Multiboot2 regressions; native build; reproducible release gate; kernel-image verification; QEMU boot test; scheduler panic-diagnostics; AHCI/NVMe two-boot persistence; production ISO; production desktop/network smoke; diagnostics/artifact upload; and GitHub Release publication.
- QEMU boot test: iterations 1/3, 2/3 and 3/3 each passed all 104 configured milestones with no failure markers. Each iteration also reported the 32-bit entry and long-mode markers. The retained timeout.log in artifact 13649770651 contains the GNU timeout deadline marker: timeout: sending signal TERM to command 'qemu-system-x86_64'.
- Storage evidence: the same artifact contains serial-disk1.log and serial-disk2.log. Both show SATA and NVMe discovery, both destructive driver tests passing, /data mounted, boot counters 1 (previous 0) and 2 (previous 1), the persistent-file check, the Ring-3 fsync probe, and Stage 3 certification completion. Neither log contains a panic/failure marker; the storage workflow step concluded SUCCESS. The job log emits a transient storage ERR-trap diagnostic between sequential boots; the retained per-boot logs are the deciding evidence and confirm both boots passed.
- Production smoke: the desktop session reached 'interactive desktop session ready'; RTL8139 attached, DHCP bound 10.0.2.15, and the QEMU first-hop gateway resolved. This is QEMU device evidence, not physical NIC certification.
- Release freshness: Build #1835 observed that main advanced only through documentation files after its tested SHA and explicitly allowed publication of the boot-tested 1a57d5d code snapshot. The release log confirms stable ISO SHA-256 082bd06046aad31f8f9d2d39b20b5361b46d0a44cd39ddda26bff07a321e58cd.
- GitHub Release metadata confirms zeroos-1a57d5c.iso and zeroos.iso are both 12,369,920 bytes and have identical SHA-256 082bd06046aad31f8f9d2d39b20b5361b46d0a44cd39ddda26bff07a321e58cd. Release notes identify commit 1a57d5c and describe a GRUB/SeaBIOS legacy-BIOS El Torito live ISO; there is no disk installer.
- The ISO verifier still emits a generic 'THIS IMAGE WILL NOT BOOT' warning when optional pycdlib cross-check is unavailable. In this run the actual QEMU boot test passed 3/3, so that optional-tool warning is not evidence of a boot failure; improve the wording separately.
- The P0 post-merge boot/release gate is CLOSED for code snapshot 1a57d5d. Remaining gates: 600-second/four-vCPU scheduler soak; physical Lenovo G560 boot/device matrix; physical SATA/NVMe, NIC, USB and graphics; expanded physical crash/power-cut recovery. Overall estimate remains unchanged (~43%); PR #42 changes boot-test evidence, not kernel features.

## 2026-11-10 — ordered-data writeback failure found; fix on candidate branch

- Current `main` HEAD at review: `e1392c094e1ea58594cee6907001858fec361f96`; latest code-bearing commit: `1a57d5cebe0e1eff87fd10450be841708de4e01d`. The intervening main commits are documentation-only.
- Latest verified main evidence remains Build #1835 (run `42560302805`) and Stage 1 Scheduler #256 (run `42560302640`), both SUCCESS. Build #1835 passed the configured native build/check, three QEMU boots (104 milestones each), storage persistence, production desktop/network smoke, and release publication. The stable/versioned ISO pair for `1a57d5c` has matching SHA-256 `082bd06046aad31f8f9d2d39b20b5361b46d0a44cd39ddda26bff07a321e58cd`.
- Code review found a storage-integrity defect in `zj_commit_locked`: it discarded the return value from ordered page-cache writeback and continued journaling metadata. A failed write of newly allocated file data could therefore be followed by a durable metadata commit that exposes unwritten/stale contents.
- Candidate branch `fix/zjfs-ordered-data-writeback-20261110` now propagates the writeback error, aborts the transaction, marks the filesystem ERROR/read-only through the existing abort path, and returns before `zj_sb_to_txn` or journal writes.
- Added a boot-time RAM-disk regression that injects persistent EIO only into the allocated data block, requires commit failure and an abort with no commit-count increase, then remounts and verifies the uncommitted file is absent. Added a source-contract regression and a required serial boot milestone.
- **Candidate CI/QEMU results are pending at this documentation snapshot.** Do not attribute the candidate fix or its new milestone to main, and do not increase completion estimates until the exact candidate Build/QEMU workflow passes.
- Remaining open gates: 600-second/four-vCPU scheduler soak; physical Lenovo G560 and NIC/USB/graphics/storage certification; broader physical crash/power-loss recovery; clean ACPI S5 shutdown remains unmerged (open PR #9 is highly divergent). Overall estimate remains ~43%; no progress credit for documentation or test infrastructure alone.

## 2026-11-10 — current main review and ordered-data durability barrier candidate

- Reviewed `main` HEAD `3f42def2a0695c53f6d9b6d37d807ce627feaae0`. PR #43 is closed and its ordered-data writeback error-propagation change is present in main. The tested PR head was `fb1e973b0b41ebeb273c37350bd0bb0d785c1b09`; comparing it to the main merge commit reports one additional commit and no changed-file delta.
- PR Build #1837 (run `42747010883`, head `fb1e973b0b41ebeb273c37350bd0bb0d785c1b09`) and Stage 1 Scheduler #258 (run `42747011119`) completed successfully. Build #1837's successful steps include the new ordered-writeback source regression, native x86-64 build, reproducible release gate, kernel/ISO verification, the three-boot QEMU gate (105 required milestones), scheduler panic diagnostics, two-boot AHCI/NVMe persistence, production ISO generation and production desktop/network smoke. The GitHub Release publication step was skipped because this was a PR run.
- The post-merge push-triggered main workflow for `3f42def2a0695c53f6d9b6d37d807ce627feaae0` was not returned by the available commit-workflow/status queries. Do not label post-merge main CI as passed based on the PR run. The last release snapshot recorded in the repository documents remains `1a57d5c`; a release for `3f42def2a0695c53f6d9b6d37d807ce627feaae0` has not been verified in this review.
- Additional storage-integrity finding: ordered page-cache writeback completion does not itself prove data reached stable media. The commit protocol needs a device FLUSH barrier after all ordered data writes complete and before any journal metadata/commit record is submitted. The later journal FLUSH cannot guarantee that ordering if the device reorders volatile-cache writes.
- Candidate branch `fix/zjfs-ordered-data-flush-barrier-20261110` adds that pre-journal FLUSH, aborts/read-only on barrier failure, injects EIO into the first FLUSH in a RAM-disk runtime test, and verifies after remount that the uncommitted file is absent. A source-contract regression and required QEMU boot milestone are included. Historical candidate status before PR #44 completed validation; superseded by the merged Build #1854 result below.
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



## 2026-11-11 — block-layer in-flight FLUSH barrier merged; main release verified

- Latest code-bearing main commit: `1af592d300e648ffddb7e76f644d28d9156c34fb` (PR #46).
- Fix: `kernel/storage/block.c:block_barrier_sequence()` now includes the earliest dispatched `BLOCK_OP_FLUSH` still present in `device->inflight`, not only `device->flush_waiting`. Dispatch removes a FLUSH from the waiting list; retaining its sequence as the active barrier prevents requests submitted after that FLUSH from overtaking it before completion.
- Runtime regression in `kernel/storage/test_block.c`: a one-shot stalled FLUSH is confirmed DISPATCHED, a later write remains QUEUED, and completing the FLUSH releases the later write. Required boot milestone: `ZEROOS: storage block in-flight flush barrier passed.`
- Added `tools/test_block_flush_barrier.py`; CI also runs fail-closed storage-persistence and boot-diagnostic regressions. The workflow now preserves the original failing status while printing diagnostic tails.
- Candidate Build #1899 (push) and #1900 (PR) passed on exact candidate head `8e69f8fb4a4f91e2a354744f4bd8ae76ef311efd`; Stage 1 #320 and #321 passed. PR #46 merged as `1af592d`.
- Post-merge main Build #1901 (run 42952878802) completed successfully; main Stage 1 #322 (run 42952878772) completed successfully.
- Main Build evidence: native x86-64 build and reproducible release gate passed; kernel image verified; three QEMU boots each reached all 107 required milestones; SMP4, NX-disabled, failed-IPI recovery and scheduler panic-diagnostic gates passed; the q35 AHCI+NVMe two-boot persistence step passed; production ISO and production desktop/network smoke passed.
- The Build workflow published `zeroos-1af592d.iso` and updated `zeroos.iso`. Both are 12,374,016 bytes and have identical SHA-256 `5df87ed020a9d5695a113ad75e163b4b85f8d086dd9a9c8ee89310dcec37f789`. Release notes identify `1af592d`, legacy BIOS El Torito boot, and a live session; no disk installer is included.
- The ISO verifier still emits its generic one-warning summary even though the same production image passed QEMU boot. Keep that verifier warning visible; do not reinterpret it as a QEMU failure or as proof of hardware compatibility.
- P0 build/boot/release verification is closed for code-bearing snapshot `1af592d`. Remaining gates include the 600-second/four-vCPU scheduler soak, physical Lenovo G560 and device matrix, physical power-loss/recovery, and clean ACPI S5 shutdown.
- Code-review follow-up (not yet fixed or runtime-reproduced): `block_complete()` checks `state == BLOCK_REQ_DISPATCHED` but decrements `inflight_count` even when the request is no longer found in `device->inflight`. A duplicate IRQ/reset completion in the unlink-to-finalize window may therefore decrement accounting twice. Add an in-flight membership guard and deterministic regression as a separate P1 fix; do not claim it is already fixed.
- Overall completion estimate remains approximately 43%; no percentage increase for CI, release-pipeline or documentation work.



## 2026-11-12 — duplicate block completion fix merged; release validation blocked

- PR #47 merged as `2dac661e73655dfc5e6513bbb5a70b0f7791c8c9`. `kernel/storage/block.c:block_complete()` now requires actual in-flight-list membership, rejects a zero in-flight count, and claims `BLOCK_REQ_COMPLETING` while holding the device lock before unlink/accounting. The deterministic stale-callback runtime regression, source-contract regression, CI invocation and boot milestone are in main.
- Exact candidate evidence before merge: Build #1904 (push) and #1905 (PR) passed on code SHA `abac8c3c027416755102d44f2a5301c0be1372a9`; Stage 1 #325/#326 passed. The Build completed native `make check`, kernel/ISO verification, 3/3 QEMU boots with 108/108 milestones each, SMP4/NX-disabled/failed-IPI/scheduler diagnostics, two-boot AHCI/NVMe persistence and production desktop/network smoke. The retained boot artifact includes the new duplicate-completion milestone; storage counters advanced 0→1→2 across the two boots.
- Post-merge main Stage 1 #327 (run `43144281205`) and #328 (run `43144667521`) passed on `2dac661`. Main Build #1906 (run `43144281250`) and #1907 (run `43144667493`) both failed at the reproducible `make check` gate before kernel-image/QEMU/storage/release steps. Exact compiler diagnostics: `tests/crypto_test.c:72:5` and `tests/crypto_test.c:101:36`, `-Werror=unterminated-string-initialization`; the fixed-size byte arrays intentionally omitted the string terminator.
- PR #48 (`1342d6f91a9a8b1869678aa0839ea868b16df05f`) changes only `tests/crypto_test.c`: inferred-size arrays, `sizeof(...)-1` lengths and compile-time 114/34-byte assertions. Its Build #1908 (push) and #1909 (PR) passed the reproducible `make check` gate and kernel-image verification, and Stage 1 #329/#330 passed; however, both full Builds failed QEMU boot iteration 1. The serial log stops after `ZEROOS: Multiboot2 info range policy self-test passed.`; 105/108 milestones are absent. QEMU reports #UD, then #GP error `0x32`, then a triple fault at RIP `0x11a5db`. This is observed on the candidate snapshot; causality from the test-only change is not established. Do not merge PR #48.
- Stable release remains the older `zeroos-1af592d.iso` / `zeroos.iso`, both 12,374,016 bytes with SHA-256 `5df87ed020a9d5695a113ad75e163b4b85f8d086dd9a9c8ee89310dcec37f789`. It is not yet verified against `2dac661`; P0 stable-release freshness is OPEN.
- Next concrete action: retain the failing `build/zeroos.elf` and linker map from PR #48's QEMU failure, symbolicate RIP `0x11a5db` against that exact ELF, and compare the early memory/GDT path and layout with successful Build #1905 before changing kernel code. Then rerun the full Build/QEMU/storage/production gates; only publish stable ISO after byte-for-byte comparison.
- Other open gates: 600-second/four-vCPU scheduler soak; physical Lenovo G560, SATA/NVMe/NIC/USB/graphics matrix; physical power-loss recovery; clean ACPI S5. PR #26 is 6 ahead/101 behind, #27 1/100, #14 7/186, #9 4/293, #8 147/293, #6 19/392 and #4 10/392; selectively rebase/review rather than blind-merge.
- Overall implementation estimate remains approximately 43%. No completion credit for documentation, CI infrastructure or test-only portability work.
