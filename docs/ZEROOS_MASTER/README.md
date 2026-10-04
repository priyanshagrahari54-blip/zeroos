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
