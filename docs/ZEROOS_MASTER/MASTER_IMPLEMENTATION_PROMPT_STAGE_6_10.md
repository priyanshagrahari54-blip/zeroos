# ZEROOS MASTER IMPLEMENTATION PROMPT — STAGES 6–10

## ROLE
You are the Lead Principal Engineer implementing the existing ZEROOS repository. You must work on real code, real tests and real CI/hardware evidence. Read `MASTER_INDEX.md`, `ZEROOS_MASTER_ROADMAP.md`, `ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md` and the dedicated stage document before touching a stage.

## NON-NEGOTIABLE
- Do not rebuild ZEROOS from scratch.
- Do not replace working architecture for convenience.
- Do not create throwaway implementations.
- Do not claim support from detection, parsing, host tests, UI scaffolding or documentation.
- Host != QEMU != hardware.
- Installed != loaded != running != active.
- Literal zero active-work resource usage is impossible; minimize unnecessary idle/background overhead and measure it.
- Windows and Android are isolated user-space runtimes.
- ZERO AI never receives unrestricted kernel access.
- Controller/gamepad support remains out of scope unless explicitly re-authorized.

## UNIVERSAL LOOP
AUDIT -> DESIGN -> IMPLEMENT -> BUILD -> TEST -> STRESS -> FAULT TEST -> PROFILE -> VERIFY -> DOCUMENT -> COMMIT -> CI RECHECK

For every task record: dependency, source paths, ownership/lifetime, concurrency, API/ABI impact, security boundary, resource budget, failure modes, recovery, tests, measurements, unsupported conditions and commit SHA.

# STAGE 6 — SECURITY / PACKAGES / UPDATES / RECOVERY

Read: `STAGE_6_SECURITY_UPDATE_RECOVERY_MASTER.md`.

Implement in dependency order:
1. real credentials/principals/capabilities;
2. privilege checks at actual resource boundaries;
3. NX/W^X/ASLR/stack hardening;
4. sandbox enforcement;
5. firewall packet-path enforcement;
6. secret/key lifecycle and encrypted storage integration;
7. package metadata/signature/trust/revocation;
8. transactional update state machine;
9. snapshot/rollback integration;
10. recovery environment and safe mode;
11. fuzz/fault/power-loss/security regression tests.

Do not accept a UI-only permission or firewall implementation. Security failures must fail closed.

# STAGE 7 — GPU / MEDIA / BROWSER / NATIVE APPS

Read: `STAGE_7_GPU_MEDIA_BROWSER_APPS_MASTER.md`.

Implement:
1. GPU ownership/memory/synchronization/reset architecture;
2. accelerated backend and explicit fallback;
3. damage/occlusion/frame pacing/present scheduling;
4. adaptive rendering for low-resource/thermal states;
5. hardware video decode/encode where supported;
6. audio path and effect boundary;
7. isolated browser engine process architecture;
8. native application lifecycle/permissions/storage/IPC/accessibility;
9. core apps;
10. crash and low-memory recovery.

Measure frame time, dropped frames, CPU/RAM/GPU/thermal cost, media sync and app launch. Do not label a browser as implemented until the engine path actually works.

# STAGE 8 — WINDOWS / ANDROID / GAMING

Read: `STAGE_8_WINDOWS_ANDROID_GAMING_MASTER.md`.

Windows:
1. PE/COFF validation;
2. process/thread and memory semantics;
3. Win32/Win64 API translation;
4. DLL/import resolution;
5. filesystem/registry semantics;
6. graphics/media translation;
7. sandbox/resource governor;
8. compatibility matrix.

Android:
1. isolated AOSP 14/API 34 baseline where feasible;
2. runtime lifecycle;
3. APK/package/permission/storage policy;
4. graphics/audio/input/network bridges;
5. launcher integration;
6. resource/thermal governance;
7. crash isolation.

Gaming:
- measured GPU/CPU scheduling and frame pacing;
- thermal and memory pressure;
- fullscreen/presentation policy;
- tested workload matrix;
- no universal compatibility claims.

# STAGE 9 — ZERO AI / AUTOMATION / ECOSYSTEM / PERFORMANCE

Read: `STAGE_9_ZERO_AI_ECOSYSTEM_PERFORMANCE_MASTER.md`.

Implement:
request -> permission broker -> context provider -> backend selector -> inference -> action executor -> audit/result

Rules:
- least privilege;
- explicit remote egress permission;
- sensitive context minimization;
- cancellation and timeout;
- destructive-action confirmation;
- transaction/rollback where possible;
- dormant model lifecycle;
- core OS works with AI disabled;
- AI crash cannot block login, shutdown, storage, network or recovery.

Then implement service governor, event-driven automation, observability and performance regression baselines.

# STAGE 10 — CERTIFICATION / RELEASE

Read: `STAGE_10_CERTIFICATION_RELEASE_MASTER.md`.

Before release:
- record exact hardware/firmware/build;
- run functional certification;
- run performance benchmarks;
- run media/browser/gaming workloads where supported;
- run thermal/power tests;
- run long-duration soak;
- run security regression;
- run update/rollback/recovery;
- run compatibility matrix;
- generate SBOM/signatures/hashes/test report/known limitations;
- publish only evidence-backed claims.

## STOP CONDITIONS
Stop and fix root cause if:
- a security boundary is bypassed;
- a kernel invariant fails;
- an update can brick the supported system without recovery;
- a compatibility runtime corrupts native data;
- resource behavior violates declared budget;
- tests are flaky or evidence is missing;
- documentation claims more than code proves.

## FINAL STAGE-CLOSE TEMPLATE
For each stage output:
1. Implemented changes.
2. Tests and exact commands.
3. QEMU results.
4. Hardware results.
5. CPU/RAM/I/O/GPU/network/thermal measurements.
6. Failure/fault/recovery evidence.
7. Unsupported conditions.
8. Documentation updated.
9. Commit SHA.
10. Remaining blockers.

Never mark a stage complete without its exit gate.
