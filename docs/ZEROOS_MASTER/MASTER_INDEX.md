# ZEROOS — MASTER DOCUMENT INDEX
Version: 2.4
Updated: 2026-10-25

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

## Release metadata consistency — 2026-10-26
- Candidate release-workflow fix delays final release notes until the mutable bootable ISO has passed byte-for-byte verification. Validation state and limitations are recorded in `VALIDATION.md` and `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md`.
