# ZEROOS — MASTER DOCUMENT INDEX
Version: 2.2

This is the navigation layer for the complete ZEROOS documentation set. `ZEROOS_ALL_IN_ONE_MASTER_SPEC.md` remains the canonical consolidated product/architecture specification. Stage 6-10 execution is split into dedicated production master documents so an implementation agent can work one stage at a time without losing cross-cutting requirements.

## Source-of-truth hierarchy
1. `ZEROOS_ALL_IN_ONE_MASTER_SPEC.md` — canonical consolidated system/product specification.
2. `ZEROOS_MASTER_ROADMAP.md` — authoritative execution order and stage gates.
3. `STAGE_6_SECURITY_UPDATE_RECOVERY_MASTER.md` — Stage 6 execution contract.
4. `STAGE_7_GPU_MEDIA_BROWSER_APPS_MASTER.md` — Stage 7 execution contract.
5. `STAGE_8_WINDOWS_ANDROID_GAMING_MASTER.md` — Stage 8 execution contract.
6. `STAGE_9_ZERO_AI_ECOSYSTEM_PERFORMANCE_MASTER.md` — Stage 9 execution contract.
7. `STAGE_10_CERTIFICATION_RELEASE_MASTER.md` — Stage 10 certification/release contract.
8. `MASTER_IMPLEMENTATION_PROMPT.md` — Stage 0–5 implementation-agent operating contract.
9. `ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md` — cross-cutting engineering/product contract.
10. `ZEROOS_VISUAL_DESIGN_THEMES_AND_APP_SUITE.md` — canonical visual themes, desktop/tablet adaptation and first-party app UX contract.
11. `ZEROOS_ZERO_RENDER_IDLE_ARCHITECTURE.md` — mandatory zero-render idle, retained-surface, damage-driven presentation and optional-animation architecture.
12. `ZEROOS_LENOVO_G560_REFERENCE_PROFILE.md` — Lenovo G560 hardware capability profile, resource targets, HDD optimization, thermal/audio/Bluetooth and complete UI token/behavior profile.
13. `DESIGN_UI_UX.md` — UI/UX design-system summary.
14. `ZEROOS_MASTER_BLUEPRINT.md`, `PRD.md`, `ARCHITECTURE.md`, `TECHSPEC.md`, `RULES.md`, `AGENTS.md`, `PHASES.md`, `MEMORY.md`, `VALIDATION.md` and subsystem specifications — supporting source documents.

## Stage 6-10 rule
Stages are dependency gates, not feature-quality levels. Every stage starts with the intended production architecture. No placeholder implementation may be introduced solely to move the stage number.

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

## Documentation synchronization
When implementation changes a contract, update the closest subsystem document and the affected master/roadmap/stage plan in the same change. Never mark an item implemented without evidence.


## Current implementation status
The live percentage/evidence snapshot is maintained in `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md`. Current overall implementation estimate is ~44%: Stage 1 92%, Stage 2 82%, Stage 3 45%, Stage 4 39%, Stage 5 41%, Stage 6 20%, Stage 7 12%, Stage 8 12%, Stage 9 12%, Stage 10 6%. These are engineering estimates, not source-line percentages; documentation maturity is not counted as implementation.
