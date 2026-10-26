# ZEROOS Master Documentation Set

This directory is the single organized documentation home for ZEROOS.

## Canonical document
- ZEROOS_ALL_IN_ONE_MASTER_SPEC.md — consolidated product, architecture, requirements, hardening, UI/UX, hardware, AI, lifecycle, security, testing and release specification.

## Organization
All existing ZEROOS documentation files are kept here together under one directory. Individual files remain as subsystem/source documents for traceability; the consolidated master is the primary planning/design source of truth.

## Rules
- Do not delete source requirements merely because they are represented in the master.
- Resolve conflicts deliberately and update the master plus the affected source document.
- Implementation status is determined by code, tests and evidence, not documentation alone.


---

## Cross-Cutting Master Contract

See [`ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md`](./ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md) for the mandatory cross-cutting engineering contract and the expanded ZEROOS feature/platform catalog. Applicable requirements cover ownership/lifetime/concurrency, boot/firmware, hardware certification, networking, packages, SDK, observability, accounts, backup/recovery, privacy, supply-chain security, accessibility/i18n, virtualization, power-loss certification, performance/compatibility labs, ZERO AI safety, and additional product features. This is a specification link only; implementation status remains evidence-based.


## Current implementation status — 2026-10-04
For the live evidence-weighted stage percentages and recent hardening, see `ZEROOS_CURRENT_IMPLEMENTATION_STATUS.md`. Current estimate: ~44% overall; S1 92%, S2 82%, S3 45%, S4 39%, S5 41%, S6 20%, S7 12%, S8 12%, S9 12%, S10 6%.


## Live implementation status — 2026-10-05
**~44% overall** — S1 92%, S2 82%, S3 45%, S4 39%, S5 41%, S6 20%, S7 12%, S8 12%, S9 12%, S10 6%.

Current execution is intentionally focused on the lowest later-stage gaps. Latest hardening added explicit ZERO AI action permissions and revocation enforcement; later-stage percentages remain unchanged until integration evidence is produced.

## Complete implementation directive — 2026-10-26

See [ZEROOS_ALL_FEATURES_IMPLEMENTATION_AND_BOOTABLE_RELEASE_CONTRACT.md](./ZEROOS_ALL_FEATURES_IMPLEMENTATION_AND_BOOTABLE_RELEASE_CONTRACT.md) for the full ten-stage execution plan, bootable release gates, product feature ledger, and the 220-entry optimization-catalog disposition matrix. It is a specification and work plan, not a claim that the OS is complete.


## Low-level optimization catalogs (separate source-derived references)
- [`LOW_LEVEL_SYSTEM_OPTIMIZATIONS_CATALOG_100.md`](./LOW_LEVEL_SYSTEM_OPTIMIZATIONS_CATALOG_100.md) — separate 100-feature catalog covering storage, memory, CPU/scheduling, networking/IPC and hardware management.
- [`LOW_LEVEL_SYSTEM_NETWORK_BROWSER_CATALOG_120.md`](./LOW_LEVEL_SYSTEM_NETWORK_BROWSER_CATALOG_120.md) — separate 120-feature catalog with storage, memory, CPU, network/IPC and low-RAM browser optimization guidance.

These documents transcribe and technically qualify the two supplied PDF catalogs. They are design/evaluation inventories, not proof that every item is implemented. Linux-specific APIs/settings and hardware-dependent optimizations must be adapted, capability-gated, security-reviewed and benchmarked for native ZEROOS.
