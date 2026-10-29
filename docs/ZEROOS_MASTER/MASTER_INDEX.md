# ZEROOS — MASTER DOCUMENT INDEX
Version: 2.4
Updated: 2026-10-28

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
