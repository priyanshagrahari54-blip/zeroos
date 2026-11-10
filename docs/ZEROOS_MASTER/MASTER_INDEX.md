# ZEROOS — MASTER DOCUMENT INDEX
Version: 3.0
Updated: 2026-11-10 (code-bearing main merge 100c8e2; PR #44 merged; Build #1854 and Stage 1 #275 passed on tested tree 8ce02ea; post-merge main CI and stable ISO for 100c8e2 remain unverified)

This is the navigation layer for the complete ZEROOS documentation set. `ZEROOS_ALL_IN_ONE_MASTER_SPEC.md` remains the canonical consolidated product/architecture specification. Stage 6-10 execution is split into dedicated production master documents so an implementation agent can work one stage at a time without losing cross-cutting requirements.

## Source-of-truth hierarchy
1. `ZEROOS_ALL_IN_ONE_MASTER_SPEC.md` — canonical consolidated system/product specification.
2. `ZEROOS_MASTER_ROADMAP.md` — authoritative execution order and stage gates.
3. `STAGE_6_SECURITY_UPDATE_RECOVERY_MASTER.md` — Stage 6 execution contract.
4. `STAGE_7_GPU_MEDIA_BROWSER_APPS_MASTER.md` — Stage 7 execution contract.
5. `STAGE_8_WINDOWS_ANDROID_GAMING_MASTER.md` — Stage 8 execution contract.
6. `STAGE_9_ZERO_AI_ECOSYSTEM_PERFORMANCE_MASTER.md` — Stage 9 execution contract.
7. `STAGE_10_CERTIFICATION_RELEASE_MASTER.md` — Stage 10 certification/release contract.
8. `ZEROOS_MASTER_IMPLEMENTATION_PROMPT.md` — implementation-agent operating contract.
9. `ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md` — cross-cutting engineering/product contract.
10. `ZEROOS_VISUAL_DESIGN_THEMES_AND_APP_SUITE.md` — canonical visual themes, desktop/tablet adaptation and first-party app UX contract.
11. `ZEROOS_ZERO_RENDER_IDLE_ARCHITECTURE.md` — mandatory zero-render idle, retained-surface, damage-driven presentation and optional-animation architecture.
12. `ZEROOS_LENOVO_G560_REFERENCE_PROFILE.md` — Lenovo G560 hardware capability profile, resource targets, HDD optimization, thermal/audio/Bluetooth and complete UI token/behavior profile.
13. `DESIGN_UI_UX.md` — UI/UX design-system summary.
14. `ZEROOS_MASTER_BLUEPRINT.md`, `PRD.md`, `ARCHITECTURE.md`, `TECHSPEC.md`, `RULES.md`, `AGENTS.md`, `PHASES.md`, `MEMORY.md`, `VALIDATION.md` and subsystem specifications — supporting source documents.

## Stage 6-10 rule
Stages are dependency gates, not feature-quality levels. Every stage starts with the intended production architecture. No placeholder implementation may be introduced solely to move the stage number.

## Reference PDF audit — 2026-10-26

`ZEROOS_REFERENCE_PDF_AUDIT_AND_ADOPTION_MATRIX.md` records the initial disposition of the 100-item and 120-item optimization catalogues plus the operating-system reference PDFs. It separates safe-to-adapt concepts from unsafe defaults, hardware-dependent techniques, and upstream components requiring license/ABI/security review. Two PDFs uploaded in commit `5f0d1a3a012d1f397e9d5561f5081109ced1e521` still need content-level extraction; the audit does not claim their text was reviewed. The 220 optimization entries still need an individually numbered disposition ledger. No implementation percentage increases from this documentation work.

## Full completion directive

`ZEROOS_MASTER_IMPLEMENTATION_PROMPT.md` includes the full-repository directive for stages 0–10 and all documented features. Follow the dependency order: reproducible build/boot, kernel and userspace correctness, storage integrity, real hardware paths, desktop/apps, security/recovery, ZERO AI, compatibility, then measured release certification. The directive is a work contract, not evidence that these features are implemented. Latest evidence snapshot: `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md`; latest roadmap: `ZEROOS_MASTER_ROADMAP.md`.

## Universal execution loop
AUDIT -> DESIGN -> IMPLEMENT -> TEST -> STRESS -> MEASURE -> DOCUMENT -> INTEGRATE

## Universal production gate
A feature is not complete because it parses, detects hardware, has a UI, passes a host test, or has a design. Completion requires real enforcement/operation plus relevant positive, negative, boundary, concurrency, fault, resource and recovery evidence; CI/QEMU/hardware evidence where applicable; measured resource/performance behavior; and synchronized documentation.

## Feature lifecycle
STOPPED -> DORMANT -> WARM -> ACTIVE -> THROTTLED -> SUSPENDED -> DORMANT/STOPPED

Installed != loaded != running != active. Dormant features must not create unnecessary continuous CPU/GPU/network work.

## Stage map
| Stage | Focus | Dedicated plan |
|---|---|---|
| 6 | Security, package trust, transactional updates, snapshots, recovery | `STAGE_6_SECURITY_UPDATE_RECOVERY_MASTER.md` |
| 7 | GPU, compositor acceleration, media, browser, native apps | `STAGE_7_GPU_MEDIA_BROWSER_APPS_MASTER.md` |
| 8 | Windows, Android, compatibility, gaming | `STAGE_8_WINDOWS_ANDROID_GAMING_MASTER.md` |
| 9 | ZERO AI, automation, ecosystem, performance/regression | `STAGE_9_ZERO_AI_ECOSYSTEM_PERFORMANCE_MASTER.md` |
| 10 | Hardware certification, soak, release gates | `STAGE_10_CERTIFICATION_RELEASE_MASTER.md` |

## Visual/product system
`ZEROOS_LENOVO_G560_REFERENCE_PROFILE.md` specializes the system for the Lenovo G560 reference envelope: 1366x768, legacy Intel/NVIDIA graphics variants, 5400-RPM SATA HDD behavior, thermal/fan policy, audio/Bluetooth, eye protection, multitasking and the complete Aurora Legacy UI profile. `ZEROOS_VISUAL_DESIGN_THEMES_AND_APP_SUITE.md` defines the premium but resource-aware visual direction: rounded glass-like materials with solid fallbacks, Dynamic Capsule, adaptive ZERO Bar/dock, desktop/hybrid/tablet layouts, UHD/high-DPI behavior, theme catalog, motion levels, first-party app suite and per-app resource/accessibility contracts. `ZEROOS_ZERO_RENDER_IDLE_ARCHITECTURE.md` adds the stronger requirement that an unchanged visible scene produces no new software/GPU frame; animation is optional and event/pacing driven. `DESIGN_UI_UX.md` is the concise UI/UX design-system reference.

## Cross-cutting requirements
See `ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md` for boot/firmware, hardware certification, networking, packages, SDK, observability, accounts, backup/recovery, privacy, supply-chain security, accessibility/i18n, virtualization, power-loss testing, performance/compatibility labs, ZERO AI safety and additional product features.

## Boot/release truth (2026-10-18)

ZEROOS has a native x86-64 kernel and QEMU boot certification infrastructure, but is not a 100%-complete consumer OS. PR #16 was merged as `d6b58292df7e00b4b2f15ba22f3a0405035ee549`. Commit `a72c171d` expanded the full-completion directive but changed documentation only. A status lookup returned no entries for that commit; run the clean release gate and verify main CI. Use `tools/release.sh` for the full gate. Physical G560 support, standalone installer, full dynamic linker, production browser/media, Windows/Android runtimes, and the full ZERO AI execution stack remain open. Never equate a passing marker/host test with real subsystem support.

## Documentation synchronization
When implementation changes a contract, update the closest subsystem document and the affected master/roadmap/stage plan in the same change. Never mark an item implemented without evidence.


## Current implementation status
The live percentage/evidence snapshot is maintained in `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md`. Current evidence-weighted estimate (2026-10-17) is ~43%: Stage 1 92%, Stage 2 84%, Stage 3 48%, Stage 4 43%, Stage 5 44%, Stage 6 22%, Stage 7 15%, Stage 8 13%, Stage 9 16%, Stage 10 10%. See `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md` for limitations and exact evidence policy. These are engineering estimates, not certification percentages. These are engineering estimates, not source-line percentages; documentation maturity is not counted as implementation.

## Release pipeline status — 2026-10-24
The main-only stable ISO publishing guard is under review on `fix/stable-iso-main-only-20261024`. The workflow regression test is `tools/test_release_publish_guard.py`. Candidate workflow #1429 and documentation-synchronized head workflow #1435 passed; post-merge `main` and stable release-asset verification remain pending. See `VALIDATION.md` and `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md`. This change does not certify additional OS functionality.

### Candidate CI update — 2026-10-24
Workflow #1429 passed on candidate head `0f17ee1d8da6ea606bb43070faa6514000ed3a84`, including the new release-policy regression and production QEMU smoke test. The stable release upload was correctly skipped for a PR event. This does not establish merged-main or published-release verification; see `VALIDATION.md`.


## Production boot diagnostics — 2026-10-24
The release candidate adds unconditional upload of production serial/QEMU/debug logs when boot smoke tests fail. Regression coverage lives in `tools/test_production_boot_diagnostics.py`; see `VALIDATION.md` for the pending exact-commit CI gate. This improves diagnosis only and is not additional OS feature evidence.

### Workflow test invocation uniqueness — 2026-10-24
The release-policy regression script now also asserts that CI invokes it exactly once, preventing duplicate or missing guard checks. The production-diagnostic retention check remains separate; CI for the exact corrected commit is pending.

## Release artifact integrity — 2026-10-24
- The release workflow's main-only publication guard and production diagnostic retention are already present on `main`.
- Candidate follow-up adds ordered download and byte-for-byte verification that the mutable `zeroos.iso` release asset matches the production ISO boot-tested in that workflow. See `VALIDATION.md` and `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md` for validation state and limitations.

## Evidence navigation update — 2026-10-25
- PR #20 merged as `34010187ef16b4a71b476a22c782cabf4558dc06`, adding stable bootable ISO byte-integrity verification to the main-only release workflow.
- Candidate CI #1464 passed the full available QEMU/storage/production smoke gates; release publication was skipped on the PR event as intended.
- Post-merge `main` workflow and live stable-asset verification are still pending confirmation. See the 2026-10-25 entry in `VALIDATION.md` and `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md`.

## Release alias rollback hardening — 2026-10-26
- Candidate branch `fix/stable-iso-rollback-20261026` adds fail-safe restoration/removal for a failed stable ISO update.
- See `VALIDATION.md` for regression assertions and evidence. Candidate workflow #1484 passed the build/QEMU/storage/production smoke gates; main-push publication and live stable-asset verification remain pending. This is release-pipeline hardening, not additional kernel or desktop functionality.


## Complete implementation and bootable release contract

- [ZEROOS_ALL_FEATURES_IMPLEMENTATION_AND_BOOTABLE_RELEASE_CONTRACT.md](./ZEROOS_ALL_FEATURES_IMPLEMENTATION_AND_BOOTABLE_RELEASE_CONTRACT.md) — mandatory execution plan for all 10 stages, native bootable ISO acceptance gates, all ZEROOS product requirements, traceability for both uploaded optimization catalogs (220 entries total), safe/adaptive optimization policy, AI permission boundaries, performance measurements, recovery, and evidence-based completion status.


## Low-level optimization catalogs (separate source-derived references)
- [`LOW_LEVEL_SYSTEM_OPTIMIZATIONS_CATALOG_100.md`](./LOW_LEVEL_SYSTEM_OPTIMIZATIONS_CATALOG_100.md) — separate 100-feature catalog covering storage, memory, CPU/scheduling, networking/IPC and hardware management.
- [`LOW_LEVEL_SYSTEM_NETWORK_BROWSER_CATALOG_120.md`](./LOW_LEVEL_SYSTEM_NETWORK_BROWSER_CATALOG_120.md) — separate 120-feature catalog with storage, memory, CPU, network/IPC and low-RAM browser optimization guidance.

These documents transcribe and technically qualify the two supplied PDF catalogs. They are design/evaluation inventories, not proof that every item is implemented. Linux-specific APIs/settings and hardware-dependent optimizations must be adapted, capability-gated, security-reviewed and benchmarked for native ZEROOS.


## External reference audit — 2026-10-26
- [EXTERNAL_REFERENCE_AUDIT_2026-10-26.md](./EXTERNAL_REFERENCE_AUDIT_2026-10-26.md) — audit of six uploaded PDFs (four distinct reference families), with adopt/adapt/reject decisions, safety caveats, G560/2 GB policy, import/licensing workflow, and implementation order. This is an audit document, not an implementation claim.

## Library research and selective reuse — 2026-10-26

- `EXTERNAL_REFERENCE_AUDIT_2026-10-26.md`: findings from the PDF/reference audit; two uploaded hardware/kernel PDFs still require full content extraction.
- `LIBRARY_REUSE_AND_INTEGRATION_PLAN.md`: maps reusable libraries and ideas to ZEROOS subsystems, labels candidates vs actual imports, defines license/provenance/testing gates, and inventories the user's Library research bundles. **No third-party component is marked imported merely because it appears here.**
- `LOW_LEVEL_SYSTEM_OPTIMIZATIONS_CATALOG_100.md` and `LOW_LEVEL_SYSTEM_NETWORK_BROWSER_CATALOG_120.md`: catalogues supporting the adoption ledger.

Priority is still boot/release gate → kernel/userspace lifecycle correctness → storage integrity → native interactive session → carefully isolated third-party ports → browser/media/compatibility/AI expansion. Research documentation does not raise implementation completion.

## Current review note — 2026-10-27
- Release publishing has a candidate guard ensuring final release notes are updated only after the stable ISO alias has passed byte-for-byte comparison with the production ISO from the same run.
- Regression coverage is in `tools/test_release_publish_guard.py`. Current candidate branch: `fix/release-notes-verify-order-20261027`; exact CI is pending.
- See `VALIDATION.md` and `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md` for the known successful main workflow SHA and the limits of that evidence. Do not infer current-HEAD CI status from an earlier run.

## Evidence update — 2026-10-27
- PR #24 release-note ordering correction is merged to `main` as `1cd514b641d148b4094c0de524925dbd323b92eb`.
- Candidate workflow #1553 passed build, clean `make check`, QEMU boot, storage persistence, production desktop/network smoke, and release-policy/diagnostic regression gates. Release publication was skipped for the PR event.
- Remaining verification: post-merge `main` workflow and the live stable ISO's byte identity against the production image from that run. Do not raise completion estimates based on this workflow-only fix.


## Current CI and boot-gate evidence — 2026-10-28

- Latest reviewed `main`: `7c45183332f895ad778f08b98458f580b18a0067`; its exact-head CI status is not available from the current lookup.
- Last inspected completed main run #40491492164 belongs to `b1c848f193940bfd18691f942daf6e32d0dae210`, passed build/QEMU/storage/production smoke and release publication, and verified the stable ISO SHA-256 `9d9c5594a4669936e2ac37320a025365018b785768f73d6d52aebfb56a818529`.
- Boot-gate integrity candidate: `fix/boot-gate-fail-closed-20261028`; shared QEMU gate and inline CI boots now require expected timeout status, fresh logs and long-mode debug markers. Regression test `tools/test_boot_gate_fail_closed.py` is wired into CI. Candidate CI remains pending; no completion increase is claimed.

## Current engineering queue — 2026-10-28

- Latest `main` ref: `731e9e8856976beb2e717eef169ded72f4917741`; exact-head CI status was not returned, so current CI remains unverified.
- New candidate: `fix/scheduler-affinity-offline-race-20261028`. `task_set_affinity` now rechecks that at least one requested CPU is online and not offlining while holding `task_lock`, before changing affinity. A policy self-test and boot milestone cover the offline-mask rule; `tools/test_task_affinity_offline_race.py` checks the guard ordering and CI wiring.
- Exact candidate head `8fe3ee0a97788b762e990ef253238eb51c49b008` passed ZEROOS Build workflow #1610 and Stage 1 Scheduler CI #31: clean check, repeated QEMU, scheduler fault, AHCI/NVMe persistence, production ISO and desktop/network smoke all passed. Release publication was skipped on the PR event. The fix is verified in candidate CI but not merged; current-main CI and live release-asset verification remain open.
- Release blockers remain: exact-main CI, post-merge stable ISO verification, full Stage 1 scheduler exit evidence/soak, and physical hardware certification. Overall completion estimate unchanged.

## Engineering queue update — 2026-10-28

- Kernel scheduler priority: remote `task_set_affinity()` calls previously tested affinity against the caller CPU rather than the CPU actually running the target task. Candidate branch `fix/affinity-remote-owner-reschedule-20261028` uses the actual owner under `task_lock` and adds host/runtime regression coverage.
- Baseline `main` SHA: `218cbc43eaeba24612a91838ee381460aa2212e8`; exact-head CI is unverified by the current connected status lookup. Candidate SHA `f6450de619a4ea052b84d9394fe0d4dd58d98236` passed Build workflow #1626 and Stage 1 Scheduler CI #47, including the affinity regression, clean `make check`, QEMU boot matrix, storage persistence and production smoke. Merge remains pending; this candidate result is not current-main evidence.
- The change does not close the Stage 1 soak or physical-hardware gates and does not increase the OS completion estimate.

## Post-merge engineering status — 2026-10-28

- PR #30's remote-affinity owner fix is merged to `main` at `acae3eba7b2f9c6cf2c52038d7b5606c28f4a077`.
- Candidate Build workflows #1626 and #1632 and Stage 1 Scheduler workflows #47 and #53 passed. Verified gates include host regressions, clean `make check`, repeated QEMU boots, scheduler fault certification, two-boot AHCI/NVMe persistence, production ISO and desktop/network smoke. Release publication was skipped for PR events.
- Post-merge main workflow and live stable ISO verification are not yet established. The Stage 1 soak and physical hardware matrix remain open; no completion estimate increase.

## Candidate kernel lifecycle fix — 2026-10-29
- Branch `fix/process-create-parent-pointer-validation-20261029` adds early process-parent pointer provenance validation in `process_create`, plus a boot-time invalid-pointer rejection probe and CI regression script.
- Candidate CI and QEMU evidence are pending; see `VALIDATION.md`. Do not count this as implemented-and-verified until the exact candidate workflow passes. Existing release and hardware blockers remain open.

## Candidate kernel lifecycle fix — 2026-10-29
- Branch `fix/process-create-parent-pointer-validation-20261029` adds early process-parent pointer provenance validation in `process_create`, plus a boot-time invalid-pointer rejection probe and CI regression script.
- Candidate CI and QEMU evidence are pending; see `VALIDATION.md`. Do not count this as implemented-and-verified until the exact candidate workflow passes. Existing release and hardware blockers remain open.

## Process lifecycle candidate — evidence update 2026-10-29
- `process_create` parent-provenance guard and runtime invalid-pointer probe passed candidate Build workflow #1651 and Stage 1 Scheduler CI #72 at code head `f767841d9e6dd98df07005d44ebaf086472512e0`.
- The full candidate build/check/QEMU/storage/production desktop smoke gates passed; PR release publication was skipped as intended. This does not establish current-main CI or live release integrity. See `VALIDATION.md`; completion estimate unchanged.

## Latest evidence snapshot — 2026-10-29
- Main SHA observed: `b99e5705fa56558637b1983f5f45992d7ae509cb`; exact-main CI status is unverified because no check entries were returned.
- Current candidate `fix/process-api-pointer-provenance-20261029` hardens direct process-table pointer arguments and adds source regression + boot-time negative certification. Candidate CI is pending; this is not yet an accepted implementation.
- Consult `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md` and `VALIDATION.md` for evidence limits and open gates. Completion estimates remain unchanged.

## Latest implementation and validation snapshot — 2026-10-30

- PR #33's process API pointer-provenance hardening merged to `main` at `4e3691c64079d6a50f3a0cf5230336b49dfb0bee`.
- Candidate Build workflow #1677 and Stage 1 Scheduler workflow (run ID `40813814986`) passed. Verified candidate gates include the new 20-API pointer regression and boot-time negative probe, native build, clean reproducible release gate, QEMU boot, scheduler fault certification, two-boot AHCI/NVMe persistence, production ISO and desktop/network smoke. PR-triggered run skipped actual GitHub Release publication.
- Exact post-merge main CI and live stable ISO comparison remain unverified. See `VALIDATION.md` and `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md` for precise evidence boundaries and open blockers. Completion estimate unchanged.


## Main review — 2026-10-31
- Current `main` HEAD is `9f7b715ee9c15506514c4e563ea78801d9c77e9d`; exact-head CI status was not returned, so treat it as unverified.
- Candidate `fix/atomic-process-user-copy-20261031` addresses a process address-space mapping TOCTOU in syscall and exec user-copy paths. It introduces lock-serialized process copy APIs, a cross-page guest certification milestone, a source regression test, and a CI invocation. Candidate checks are pending; this is not yet an accepted/merged implementation.
- Priority remains kernel/process memory safety after build/ISO/boot. Preserve the later storage, driver/security/recovery, native desktop, browser/media/AI and compatibility requirements. See `VALIDATION.md` for exact evidence boundaries.

## Latest implementation snapshot — 2026-11-01

- PR #35's process address-space user-copy TOCTOU fix is merged to `main` at `6521dcf3e2b7d0caef25cb47515b8572f0358fcc`.
- Build #1702 (run ID `41164323843`) passed on PR integration merge commit `59654aa954f56a56eaa1ff7ea444b3a6e486da45`: new atomic-copy regression (5 tests), native build, clean `make check`, repeated QEMU/SMP4/NX-off/failed-IPI gates, scheduler fault certification, two-boot AHCI/NVMe persistence, production ISO and desktop/network smoke. Stage 1 Scheduler #123 also passed.
- GitHub Release publication was skipped on the PR event. Post-merge main CI and live stable ISO verification remain unconfirmed; the 600-second soak and physical hardware matrix remain outstanding. Overall completion estimate unchanged.


## Evidence update — 2026-11-03: stable release race

- Main `28112603` Build #41265715805 and Stage 1 Scheduler #41265715839 passed, including QEMU boot variants, storage persistence, production desktop/network smoke and release publication.
- A later-finishing older main workflow #41265707574 overwrote stable `zeroos.iso`: current release digest `c80f4270...` and notes name `3ba0dbe`, while latest-main immutable asset `zeroos-2811260.iso` is `eea7d77...`. The missing per-ref workflow concurrency allowed out-of-order publication.
- Candidate `fix/serialize-stable-release-publish-20261103` adds non-cancelling per-ref serialization plus a stale-main SHA guard, with regression coverage in `tools/test_release_publish_guard.py`. Not merged; PR CI and a post-merge release repair run remain required. Completion estimate unchanged.


## Follow-up evidence — 2026-11-03

- PR #38 release serialization/stale-main regression, all pre-build regressions, native build, reproducible release gate and kernel/ISO verification passed. Its full QEMU Boot test remains in progress; Stage 1 Scheduler #165 passed. Candidate is not merge-ready on partial evidence.
- PR #37 full Build #1740 and Stage 1 #161 passed, but code review found `context_switch_ex`'s direct frame `iretq` path in `kernel/context.S` bypasses GS restoration. Keep that separate kernel PR open until covered.
- Main QEMU serial evidence shows desktop/compositor/input and storage self-tests; network reports no supported device. Do not index the smoke-gate pass as network-driver support.
- Latest release pointer is stale versus latest main due out-of-order publisher runs; PR #38 is the candidate fix. Completion estimate unchanged.

## Final evidence update — 2026-11-04

- Merged PR #38: stable ISO publication is now serialized per ref and stale-main runs are prevented from mutating the stable alias/final release notes. Main merge: `84f8a5cf23e03098419d92231a25908174e3c21f`.
- Candidate Build #41574953029 and push Build #41574947638 passed; candidate Stage 1 workflows #41574953139 and #41574947653 passed. Full candidate QEMU, storage, production ISO and desktop/network smoke steps completed successfully.
- Post-merge main Build #41766774251 and Stage 1 Scheduler #41766774267 passed on `84f8a5cf...`. Main CI completed the native build, clean `make check`, QEMU boot variants, scheduler diagnostics, AHCI/NVMe two-boot persistence, production ISO, production smoke and release publication.
- Release log byte-compared the stable ISO with the boot-tested production ISO. Stable and versioned release assets both report SHA-256 `297ac5218d9c4e9b4b59cb85638a087fb2633a569c91bd40b8dc23e0e7a6016e`; release notes name `84f8a5c`. The previous stale-alias blocker is closed for this release.
- QEMU RTL8139/DHCP evidence exists (`10.0.2.15` lease and gateway ARP resolution); physical NIC/G560 support remains unverified.
- Next indexed work: PR #37 GS ownership return-path correction; PR #36 address-space unmap hardening; 600-second scheduler soak; physical hardware matrix; then storage recovery, drivers/security, desktop hardware integration, browser/media/AI/compatibility. No completion increase.

## Latest current-main evidence — 2026-11-04

- Documentation-tip main snapshot `9d77ad3befb6c782b548020d67d6af38401533b2`: Build #41767825881 **SUCCESS** and Stage 1 Scheduler #41767825887 **SUCCESS**.
- Full Build gates passed: release-policy and existing regressions, native x86-64 build, clean `make check`, kernel/ISO checks, QEMU boot variants, scheduler fault diagnostics, two-boot AHCI/NVMe persistence, production ISO, production boot smoke and release publication.
- Current release notes identify `9d77ad3`. Versioned and stable ISO assets both have SHA-256 `cebb728732f1cd04e2058182842e53c5d988cc0463fc7bb2ac4b8217314da413` (12,369,920 bytes); workflow log confirms the stable ISO byte comparison passed.
- QEMU RTL8139 attach, DHCP lease and gateway ARP resolution are evidenced. Physical NIC/G560 certification is still separate.
- The final follow-up is documentation-only and skips CI; the tested OS code/release remains `9d77ad3`. Keep PR #37 GS return-path review, PR #36 range-unmap review, scheduler soak, physical hardware, and the remaining product roadmap open. No percentage increase.

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

## Current-main implementation/evidence update — 2026-11-05

- Current main baseline reviewed: `c3c4eb6ba1a508ff8258d4cf96864317de2da6b4`.
- GS frame-resume and process range-unmap corrections are now included in main; see the current implementation status and validation evidence.
- New candidate boot fix: Multiboot2 information-block addresses are no longer constrained to the first 16 MiB; the candidate accepts aligned starts within the 1 GiB boot identity map while retaining a 16 MiB maximum block size; the allocator still manages only the first 512 MiB.
- New regression file: `tools/test_multiboot_info_range.py`; new required boot milestone: `ZEROOS: Multiboot2 info range policy self-test passed.`
- Candidate branch: `fix/multiboot-info-window-20261105`. Its new Build/QEMU result is pending; do not list the fix as merged or boot-certified until CI finishes and the candidate is integrated.
- Main candidate evidence: Build #41963710252 and Stage 1 Scheduler #41963710086 passed; PR-event release publication was skipped. Post-merge main workflow and stable ISO verification for the current main SHA remain open.
- The canonical product specification and all Stage 0-10 requirements remain unchanged. No completion percentage increase.

## PR #40 validation status — 2026-11-05

- New Multiboot2 handoff-range regression was added to the Build workflow. Its first Build #1770 failed at the new test because the runtime self-test lacked the expected 17-byte size-alignment case; no native release build or QEMU step ran in that attempt.
- The missing C self-test case and whitespace-sensitive test assertion have been corrected. Candidate head: `c2010a2175ea6f73b0057143def8d4172fc54d65`; exact-head Build/QEMU validation is pending.
- Do not merge PR #40 until the new regression, clean release gate, QEMU, storage and production smoke checks pass. No completion percentage increase.
## PR #40 latest validation snapshot — 2026-11-05

- Stage 1 Scheduler #203 passed on PR tip `7043444b45c5cd068651b7a2bb054851ae5d18cb`, including native x86-64 build and Stage 1 scheduler certification.
- Build #1782 for that tip was pending with no job started at the last lookup; Build #1770 failed earlier in the new source regression before native release build/QEMU, and Build #1781 was cancelled during PR close/reopen.
- Corrected PR head has not yet completed the full Build/QEMU/storage/production smoke gate. Keep PR #40 unmerged until it does.
## Latest PR #40 CI snapshot — 2026-11-05

- Stage 1 Scheduler #203 passed native kernel build and scheduler certification on PR tip `7043444b45c5cd068651b7a2bb054851ae5d18cb`.
- Build #1770 failed in the new Multiboot2 regression before native release build/QEMU; the missing 17-byte runtime case and whitespace-sensitive assertion were corrected.
- Build #1782 was cancelled; latest Build #1789 for docs tip `beaba6139cc06426ce5dc0243f6978a48e842e1f` is pending with no job started. No full QEMU/production validation is claimed for PR #40.
- Keep PR #40 open/unmerged until the exact current head passes the full Build workflow.
## Final candidate-head validation snapshot — 2026-11-05

- Final code/test head: `9c14a4a4fed9d0f3a8e21e3f5c2ecbf45a819a99`; Multiboot2 metadata is accepted within the 1 GiB identity map while allocator reservation is clipped to 512 MiB.
- Build #1810 remained pending without a job; Build #1809 was cancelled. Build #1770 failed in the first source-regression version before native build/QEMU.
- Stage 1 #230's native build and scheduler-certification steps passed, but the overall workflow was cancelled. Stage 1 #231 remained pending.
- Full QEMU, storage, production ISO and desktop validation for this final candidate are not established. PR #40 remains open/unmerged.
## PR #40 latest CI result — 2026-11-05

- Stage 1 Scheduler #231 passed on code/test head `9c14a4a4fed9d0f3a8e21e3f5c2ecbf45a819a99`, including native x86-64 kernel build and Stage 1 scheduler certification.
- Build #1816 for docs tip `9c1d3b1d5d9fc04c0857056b2fe5dea6a0cd9928` was pending with no job started; Build #1810 was cancelled. The corrected Multiboot2 regression and full QEMU/production gates remain unverified.
- PR #40 remains open and unmerged.

## Current implementation/evidence note — 2026-11-06

- Latest main merge: PR #40, `f20ea855fc30baf54cdc5d45915d357266f2ae6e` — Multiboot2 information-block range validation.
- The boot handoff start is validated within the 1 GiB identity map; total size is separately capped at 16 MiB; metadata reservation is clipped by the existing 512 MiB allocator window.
- Candidate Build #1822 and Stage 1 Scheduler #243 both completed SUCCESS. Build #1822 passed the new regression, clean build/check, repeated QEMU boot, AHCI/NVMe two-boot persistence, production ISO and desktop/network smoke. PR-event release publication was skipped.
- See `VALIDATION.md` for evidence classes and the remaining post-merge main/stable-ISO, scheduler-soak and physical-hardware gates. The canonical product specification and all Stage 0–10 requirements remain unchanged; no completion estimate increase.


## 2026-11-07 — release freshness correction

- Current main documentation head: `9a0d4d30`; latest code-bearing commit: `f20ea855`.
- Main Build #1823 passed the complete configured QEMU/storage/production smoke gates and uploaded a verified versioned ISO. Its stable-alias update was skipped because documentation-only commits had advanced main.
- Current stable `zeroos.iso` digest remains `ef092a8c...` (older `c3c4eb6` image), while `zeroos-f20ea85.iso` is `e40ac527...`. Treat the live stable alias as stale until post-fix main publication and byte comparison complete.
- Release-publisher source: `.github/workflows/build.yml`; regression: `tools/test_release_publish_guard.py`. Candidate branch `fix/release-guard-docs-only-20261107` tests that docs-only advances are allowed but kernel/source changes still block stale publication. Candidate CI is pending; do not mark the release gate closed yet.


## 2026-11-08 — PR #41 merged; release publication pending

- Merged release-publisher freshness correction: `fff32e9b35fa61f1911d43d45fc1b99aabfbdd57` (PR #41).
- Candidate Build #1826 and branch Build #1825 both passed the release regression, native build/check, repeated QEMU boot matrix, storage persistence, production ISO, and desktop/network smoke. Stage 1 Scheduler #247 and #246 passed.
- Post-merge main Build #1827 is the authoritative acceptance run and was still in progress at the last check; main Stage 1 #248 passed.
- Stable `zeroos.iso` was still the older `c3c4eb6` image at the last release API query. Do not mark the release current until Build #1827 publishes and the downloaded stable asset matches that run's production ISO byte-for-byte.
- 600-second scheduler soak and physical G560/device matrix remain open. Product roadmap and all Stage 0–10 requirements are preserved; overall completion estimate unchanged (~43%).


## Current main validation snapshot — 2026-11-09

- Main Build #1827 and Stage 1 Scheduler #248 passed on release/CI commit `fff32e9`; the latest main HEAD `1b2caeb7` is a documentation-only successor.
- The stable `zeroos.iso` and immutable `zeroos-fff32e9.iso` release assets match exactly by GitHub's SHA-256 digest: `18cfe872ab7c946d6e4adc75e411043971d5e3878eb4a4df499e4320b94af15a`. Release notes identify `fff32e9`.
- The P0 stable-release alignment issue is closed for the QEMU-tested code snapshot. Physical hardware, installation, and long-soak certification remain separate requirements.
- A candidate boot-test hardening change requires GNU `timeout` to emit its deadline TERM marker before a 124/137 QEMU result can satisfy the repeated-boot gate. Candidate CI is pending; see `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md` and `VALIDATION.md`.
- Preserve the canonical specification and all Stage 0–10 requirements. No implementation percentage increase is recorded for CI, release, or test-harness work.


## Candidate CI correction — 2026-11-09

- The first PR #42 Build #1829 and PR Build #1830 stopped at the existing GS/boot source regression because it expected the old timeout command literal. The implementation's new `LC_ALL=C timeout --verbose` invocation made that assertion stale.
- Updated `tools/test_interrupt_gs_entry.py` to verify the new command, retained timeout diagnostics, and deadline-marker ordering. No native build/QEMU result is claimed for the failed attempts; rerun exact-head CI.


## Main/boot evidence snapshot — 2026-11-09

- PR #42 merged as 1a57d5cebe0e1eff87fd10450be841708de4e01d; this hardens the QEMU observation deadline check and artifact retention. No kernel source changed.
- Candidate Build #1833 and #1834 passed on 3f36bb5; Stage 1 Scheduler #254 and #255 passed. Three repeated QEMU boots each reached all 104 milestones; the timeout diagnostics artifact contains the supervisor TERM-at-deadline marker.
- Post-merge main Stage 1 Scheduler #256 and Build #1835 both passed. Main Build #1835 completed 3/3 QEMU boots, storage persistence, production desktop/network smoke and release publication.
- Latest fully verified release pair remains zeroos-fff32e9.iso and zeroos.iso, both SHA-256 18cfe872ab7c946d6e4adc75e411043971d5e3878eb4a4df499e4320b94af15a. New main release verification is pending.
- Open: 600-second scheduler soak; physical G560/device matrix; expanded storage recovery/fault testing; kernel/process lifecycle hardening. Preserve the canonical specification and all Stage 0–10 requirements. No completion estimate increase.

## Main/release acceptance snapshot — 2026-11-09

- Current main HEAD documentation-only main successor is documentation-only; latest code-bearing commit 1a57d5d passed Build #1835 and Stage 1 Scheduler #256.
- Build #1835 passed all configured checks. The boot test passed 3/3 QEMU iterations with all 104 milestones and 32-bit/long-mode markers. The timeout diagnostics artifact contains the supervisor TERM-at-deadline marker.
- Retained storage serial logs for both QEMU disk boots show SATA/NVMe discovery, destructive driver tests, persistent /data counters 1 and 2, file check, Ring-3 fsync probe and Stage 3 completion without panic/failure markers.
- Production QEMU smoke reached the desktop session, RTL8139 attached, DHCP bound and gateway ARP resolved.
- Versioned and stable ISO assets match SHA-256 082bd06046aad31f8f9d2d39b20b5361b46d0a44cd39ddda26bff07a321e58cd; release notes identify 1a57d5c. The P0 post-merge boot/release gate is closed for this snapshot.
- Remaining: 600-second scheduler soak; physical G560/device certification; physical crash/power-cut storage recovery; physical NIC/USB/graphics. No completion estimate increase.

## 2026-11-10 — ordered-data durability review

- Main remains verified at code-bearing commit `1a57d5c` (Build #1835 and Stage 1 #256 passed; stable ISO checksum is recorded in `VALIDATION.md`).
- Candidate branch `fix/zjfs-ordered-data-writeback-20261110` fixes a ZJFS ordered-data error path: failed page-cache writeback now aborts before metadata journaling. A block-LBA-scoped EIO runtime test and source-contract regression are included.
- Candidate CI/QEMU is pending. The candidate is not part of main or the published stable ISO until the exact full Build passes and the change is integrated.

## 2026-11-10 — current main review and ordered-data durability barrier candidate

- Reviewed `main` HEAD `3f42def2a0695c53f6d9b6d37d807ce627feaae0`. PR #43 is closed and its ordered-data writeback error-propagation change is present in main. The tested PR head was `fb1e973b0b41ebeb273c37350bd0bb0d785c1b09`; comparing it to the main merge commit reports one additional commit and no changed-file delta.
- PR Build #1837 (run `42747010883`, head `fb1e973b0b41ebeb273c37350bd0bb0d785c1b09`) and Stage 1 Scheduler #258 (run `42747011119`) completed successfully. Build #1837's successful steps include the new ordered-writeback source regression, native x86-64 build, reproducible release gate, kernel/ISO verification, the three-boot QEMU gate (105 required milestones), scheduler panic diagnostics, two-boot AHCI/NVMe persistence, production ISO generation and production desktop/network smoke. The GitHub Release publication step was skipped because this was a PR run.
- The post-merge push-triggered main workflow for `3f42def2a0695c53f6d9b6d37d807ce627feaae0` was not returned by the available commit-workflow/status queries. Do not label post-merge main CI as passed based on the PR run. The last release snapshot recorded in the repository documents remains `1a57d5c`; a release for `3f42def2a0695c53f6d9b6d37d807ce627feaae0` has not been verified in this review.
- Additional storage-integrity finding: ordered page-cache writeback completion does not itself prove data reached stable media. The commit protocol needs a device FLUSH barrier after all ordered data writes complete and before any journal metadata/commit record is submitted. The later journal FLUSH cannot guarantee that ordering if the device reorders volatile-cache writes.
- Candidate branch `fix/zjfs-ordered-data-flush-barrier-20261110` adds that pre-journal FLUSH, aborts/read-only on barrier failure, injects EIO into the first FLUSH in a RAM-disk runtime test, and verifies after remount that the uncommitted file is absent. A source-contract regression and required QEMU boot milestone are included. Candidate CI/QEMU is pending; this candidate is not part of main or the stable ISO.
- Open gates remain: 600-second/four-vCPU scheduler soak; physical Lenovo G560 and device matrix; physical power-loss/recovery; USB/graphics/NIC certification; clean ACPI S5 shutdown. Overall estimate remains ~43%; no completion percentage increase.

## 2026-11-10 — merged ZJFS ordered-data durability and torn-JSB recovery

- PR #44 was squash-merged as `100c8e2cf4cdd049c4bf5565b5eebbc0f5284651`; the four follow-up commits update only the master documentation.
- Build #1854 (PR run `42751115805`, tested head `8ce02ea537489c7daea0537d6dc36e31e4364a13`) and Stage 1 Scheduler #275 (`42751115731`) passed. The Build job passed source regressions, native x86-64 build, reproducible release gate, kernel verification, boot certification, scheduler panic-diagnostics certification, two-boot storage persistence, production ISO generation, production boot smoke and artifact preservation.
- The boot diagnostics ZIP was inspected. It records 3/3 normal QEMU boots with all 106 required milestones and no failure markers; four-vCPU topology and scheduler certification passed. The new ordered-data FLUSH-EIO self-test marker is present.
- The eight-round crash/power-cut matrix passed. Round 2 explicitly exercised the torn journal-superblock fallback (`journal superblock invalid; checking descriptor/commit`) and recovered with fsck clean. All eight rounds report recovered/fsck clean.
- AHCI/NVMe two-boot persistence passed: the /data counter advanced from 1 on boot 1 to 2 on boot 2; both runs emitted the persistence-check and Stage 3 completion markers. Production desktop/network smoke passed.
- Candidate Build #1851 initially failed in crash round 2 because a torn journal-superblock write caused recovery to return `-SE_UCLEAN`. The descriptor/commit fallback was added; Build #1854 then passed the complete boot and storage gates. This failure and correction remain part of the evidence history.
- The PR-event GitHub Release publication step was skipped as designed. The post-merge push-triggered main workflow/status for `100c8e2cf4cdd049c4bf5565b5eebbc0f5284651` is not exposed by the available commit-workflow/status query, so post-merge main CI is not independently confirmed. The last stable release snapshot documented in the repository remains `1a57d5c`; no stable ISO checksum for `100c8e2cf4cdd049c4bf5565b5eebbc0f5284651` was verified here.
- Next P0: independently verify the post-merge main Build and stable/versioned ISO byte identity. Remaining engineering gates: 600-second/four-vCPU scheduler soak; physical Lenovo G560 and device matrix; physical power-loss/recovery; USB/graphics/NIC certification; clean ACPI S5 shutdown.
- Overall completion estimate remains ~43%. No completion credit is assigned for CI or release-pipeline work.

