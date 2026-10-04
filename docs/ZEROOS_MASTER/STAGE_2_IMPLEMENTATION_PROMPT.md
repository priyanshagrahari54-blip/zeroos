# ZEROOS — STAGE 2 IMPLEMENTATION PROMPT
Version: 1.0 — Production-Grade Userspace
Repository: https://github.com/priyanshagrahari54-blip/zeroos

## Mission
Continue from current `main`. PR #3 is already merged. Inspect the real tree and current CI first. Do not restart or replace working kernel foundations.

Stage 2 builds the production userspace boundary described by `docs/MASTER_IMPLEMENTATION_PROMPT.md`. Dependency order is not permission to make a toy implementation.

## Mandatory workflow
DISCOVER → AUDIT → DESIGN → IMPLEMENT → BUILD → UNIT TEST → INTEGRATION TEST → NEGATIVE TEST → STRESS → FAULT TEST → PROFILE → VERIFY → DOCUMENT → COMMIT → CI.

Read before architecture changes:
- docs/MASTER_INDEX.md
- docs/MASTER_IMPLEMENTATION_PROMPT.md
- docs/ARCHITECTURE.md
- docs/RULES.md
- docs/TECHSPEC.md
- docs/PHASES.md
- docs/MEMORY.md
- docs/PROCESS.md
- docs/SYNCHRONIZATION.md
- docs/VIRTUAL_MEMORY.md
and inspect actual callers/tests.

## 1. Ring-3 execution
Implement and harden:
- isolated per-process address spaces
- validated user stack
- privilege transition and safe return
- syscall entry/exit
- canonical-address and range/overflow validation
- hardened copy-in/copy-out
- invalid-user-access termination without kernel corruption
- user/kernel mapping policy
- NX/W^X enforcement
- process/thread cleanup

Audit interrupt-frame ownership, CR3/address-space ownership, task lifetime, and exit/reap races.

## 2. Syscall ABI
Define a versioned, explicit ABI:
- stable syscall numbers
- ABI/version identification
- structure size/version fields
- stable error codes
- pointer and length validation
- overflow checks
- blocking/nonblocking semantics
- timeout/cancellation semantics
- permission/capability checks
- restart/interruption behavior where appropriate
- descriptor/handle lifetime rules

Do not expose internal kernel structures directly.

## 3. ELF/runtime
Build a production executable path:
- ELF header/program-header validation
- segment bounds/alignment/permission validation
- supported relocations
- user stack construction
- argv/env
- auxiliary metadata
- initial process state
- dynamic-linking architecture
- shared-library model
- runtime-loader contract
- clean failure for malformed binaries

Never execute unvalidated segment mappings.

## 4. Process and service manager
Implement:
- init/bootstrap process
- service supervision
- dependency graph
- deterministic startup/shutdown ordering
- restart policy
- crash detection
- health checks
- resource budgets
- service identity
- diagnostics
- clean descendant/resource cleanup

A crashed userspace service must not crash the kernel.

## 5. IPC — real bounded byte-stream pipe
Make the current record pipe a real bounded byte-stream primitive:
- fixed bounded capacity
- ring-buffer byte semantics
- partial read/write
- full-buffer backpressure
- empty-buffer blocking
- correct reader/writer wakeups
- no lost wakeups
- timeout-aware read/write
- cancellation
- peer-close detection
- EOF/error semantics
- safe endpoint lifetime/refcounting
- concurrent readers/writers
- descriptor close while blocked
- process exit while blocked
- `PEEK`: inspect unread bytes without consuming; repeated PEEK observes the same bytes; define exact interaction with timeout, close, partial length and concurrent consumers
- user-copy validation
- bounded memory/resource accounting

Also retain/implement the general IPC architecture:
- queues
- events
- shared memory
- sockets
- handles
- permissions
- backpressure
- cancellation
- timeout.

Document exact semantics; do not leave edge cases implicit.

## 6. Userspace runtime
Provide stable wrappers/contracts for:
- memory
- processes/threads
- filesystem access
- IPC
- synchronization
- environment/session
- errors
- handles/descriptors.

Keep kernel policy out of userspace libraries where possible.

## 7. Security
Implement:
- credentials
- capability/permission checks
- sandbox boundaries
- resource limits
- secure handles
- process isolation
- safe parsing
- least privilege.

## 8. Resource/lifecycle contract
Every service/process/IPC object defines:
owner, lifetime, state machine, CPU policy, memory budget, I/O policy, wake events, startup, shutdown, suspend/resume, failure and recovery.

Use:
STOPPED / DORMANT / WARM / ACTIVE / THROTTLED / SUSPENDED
where applicable.

No polling when event-driven waiting is possible.

## 9. Testing gates
Required where applicable:
- unit
- integration
- negative
- concurrency
- timeout/cancellation
- fault injection
- stress
- soak
- resource
- security
- recovery
- QEMU
- real hardware when applicable.

Pipe tests must cover empty/full, partial transfers, PEEK, timeout, close/EOF, multiple readers/writers, blocked endpoint exit, invalid pointers, races, and memory exhaustion.

## 10. Completion
Update architecture/ABI/requirements/lifecycle docs. Build with zero warnings. Do not disable tests to obtain green CI. Record exact tests, stress, faults, resource impact, security implications, known limitations and next action. Commit only coherent changes.

Definition of done: multiple isolated production-style userspace processes/services can execute, communicate, block/wake, timeout, fail, restart and terminate without kernel corruption, with verified IPC semantics.
