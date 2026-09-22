# ZEROOS — MASTER DOCUMENT INDEX
Version: 1.0

This file is the navigation layer for the ZEROOS documentation set.

## Source-of-truth hierarchy

1. `ZEROOS_MASTER_IMPLEMENTATION_PROMPT.md` — canonical implementation-agent execution contract.
2. `ZEROOS_MASTER_BLUEPRINT.md` — what ZEROOS is and the target system architecture.
3. `ZEROOS_MASTER_ROADMAP.md` — execution order and current engineering priority.
4. `PRD.md` — product requirements and feature acceptance.
5. `ARCHITECTURE.md` — subsystem boundaries and ownership.
6. `TECHSPEC.md` — technical contracts and implementation requirements.
7. `RULES.md` — non-negotiable engineering rules.
8. `AGENTS.md` — instructions for AI/human implementation agents.
9. `PHASES.md` — phase/stage/substage development plan.
10. `MEMORY.md` — memory/state/resource lifecycle contract.
11. Existing subsystem specifications — detailed contracts for boot, scheduler, processes, interrupts, synchronization, virtual memory, GDT/TSS and hardware.

## Current implementation priority

The repository is not to jump directly into UI, Android, Windows or AI implementation while the kernel scheduler/context lifecycle is unstable.

Current order:

```
Scheduler/context correctness
        ↓
QEMU/CI stability
        ↓
Process/thread lifetime
        ↓
Ring-3 userspace
        ↓
Syscall ABI
        ↓
IPC
        ↓
Memory maturity
        ↓
Drivers + storage + networking
        ↓
Graphics/compositor
        ↓
Desktop shell
        ↓
Native applications
        ↓
Security/update/recovery hardening
        ↓
Windows/Android compatibility
        ↓
AI + ecosystem
```

## Documentation change rule

When implementation changes a contract:
- update the closest subsystem document;
- update TECHSPEC/ARCHITECTURE if the boundary changes;
- update PRD if user-visible requirements change;
- update PHASES/MASTER_ROADMAP if execution order changes;
- update AGENTS/RULES if the engineering contract changes.

Documentation and code should describe the same system.

## Status vocabulary

- PLANNED — design exists, implementation not started.
- IN_PROGRESS — implementation underway.
- EXPERIMENTAL — usable for testing, contract may change.
- STABLE — tested contract with CI coverage.
- PRODUCTION — release-quality with recovery/security validation.
- DEPRECATED — retained only for migration.

## Feature lifecycle vocabulary

```
STOPPED
  ↓
DORMANT
  ↓
WARM
  ↓
ACTIVE
  ↓
THROTTLED
  ↓
SUSPENDED
  ↓
DORMANT / STOPPED
```

The exact states used by a subsystem may be smaller, but its lifecycle must be explicit.

## Resource contract

Every service must define:
- resident memory;
- active memory ceiling;
- CPU/scheduler policy;
- I/O priority;
- wake events;
- background policy;
- suspend/unload behavior;
- recovery behavior.

The engineering goal is near-zero unnecessary idle work, not an impossible literal zero-resource guarantee while active.

## Review checklist

Before merging a major subsystem:

- [ ] Requirements mapped to implementation.
- [ ] Ownership and lifecycle documented.
- [ ] Public API/ABI documented.
- [ ] Concurrency reviewed.
- [ ] Security boundary reviewed.
- [ ] Failure behavior defined.
- [ ] Resource behavior measured.
- [ ] Tests cover positive and negative paths.
- [ ] QEMU/hardware validation completed where relevant.
- [ ] Recovery/rollback behavior documented.
- [ ] Documentation updated with the code.

## Repository map

### Product
- PRD.md
- DESIGN_UI_UX.md

### Architecture
- ZEROOS_MASTER_BLUEPRINT.md
- ARCHITECTURE.md
- TECHSPEC.md

### Execution
- ZEROOS_MASTER_ROADMAP.md
- PHASES.md
- AGENTS.md

### Engineering governance
- RULES.md
- MEMORY.md

### Existing detailed subsystem documents
- BOOT_SPEC.md
- CPU_ARCHITECTURE.md
- HARDWARE.md
- GDT_TSS.md
- INTERRUPTS.md
- SCHEDULER.md
- PROCESS.md
- SYNCHRONIZATION.md
- VIRTUAL_MEMORY.md
- BUILD.md

## Final rule

If two documents conflict, do not silently choose one. Treat the conflict as an architecture issue, resolve it deliberately, then update the affected documents in the same change.


## Advanced-First Production Policy

Stage labels define dependency order, not quality level.

ZEROOS must NOT follow “basic foundation now, advanced later”. Every subsystem is designed against its intended production architecture from the beginning. If a dependency blocks activation, implement the dependency-independent production portion and isolate the remaining boundary without creating a throwaway API.

Production status requires, where applicable:
- correctness and invariant validation;
- ownership/lifetime/concurrency contracts;
- security boundaries;
- bounded CPU/RAM/I/O behavior;
- diagnostics and observability;
- failure isolation and recovery;
- unit/integration/negative/stress/fault tests;
- QEMU and supported-hardware validation;
- CI coverage;
- documentation synchronization.

Canonical implementation prompt: `docs/MASTER_IMPLEMENTATION_PROMPT.md`.

The master prompt is the operational execution contract; this index remains the navigation/source-of-truth map.
