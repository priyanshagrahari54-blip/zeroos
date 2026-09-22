# ZEROOS — AGENTS.md
Version: 1.0 | Agent Operating Contract

## 1. Purpose
This document defines how AI coding agents, automation agents and human contributors must work inside ZEROOS. Agents are implementers and investigators, not permission to bypass engineering discipline.

## 2. Prime Directive
Never trade correctness for apparent progress.
A green-looking demo that hides a scheduler, memory, security or data-integrity fault is not progress.

## 3. Repository Orientation
Before changing code, inspect:
- docs/ARCHITECTURE.md
- docs/TECHSPEC.md
- docs/RULES.md
- docs/PHASES.md
- relevant subsystem documentation
- current CI workflow
- current test failures
- current public interfaces

The current kernel foundation must be treated as authoritative implementation context. Do not redesign existing primitives merely because a different design is easier to describe.

## 4. Agent Workflow
### Stage A — Discover
1. Identify the task and acceptance criteria.
2. Locate affected files.
3. Read adjacent interfaces and invariants.
4. Search for callers.
5. Determine whether the change affects ABI, memory ownership, scheduling, interrupt context or persistence.

### Stage B — Plan
Produce:
- problem statement,
- invariant(s),
- minimal change set,
- tests,
- failure cases,
- rollback strategy.

### Stage C — Implement
Prefer small commits.
Avoid unrelated formatting.
Do not silently alter public behavior.
Keep architecture notes synchronized with implementation.

### Stage D — Verify
Run the narrowest relevant test first, then full build/CI.
For kernel changes, require QEMU boot validation when the change can affect boot or runtime state.

### Stage E — Review
Check:
- ownership,
- concurrency,
- interrupt safety,
- lifetime,
- error propagation,
- resource usage,
- user-visible behavior,
- documentation.

## 5. Kernel Agent Rules
### 5.1 Scheduler
Never suppress or weaken a scheduler invariant to make CI pass.
For a task-switch fault, inspect:
- current task identity,
- runnable/running transitions,
- saved stack pointer,
- interrupt frame ownership,
- context-switch boundaries,
- IRQ return path,
- task creation context,
- preemption state.

### 5.2 Memory
Every allocation must have an owner and lifetime.
Every mapping must have permissions.
Do not introduce silent aliasing.
Do not assume physical memory is contiguous.
Do not use virtual-memory terms as substitutes for actual RAM behavior.

### 5.3 Interrupts
Interrupt handlers must have explicit context ownership.
Never return through a frame that has been invalidated or reused.
Keep IRQ-safe code paths minimal.

### 5.4 Processes/Threads
PIDs/TIDs are identifiers, not object ownership.
Use explicit object lifetime and generation rules.
Zombie/reap semantics must be documented.

## 6. Userspace Agent Rules
- No direct hardware access unless explicitly privileged.
- Syscalls must validate user pointers and lengths.
- Copy-in/copy-out rules must be explicit.
- Errors must be stable and documented.
- Crashes should terminate the faulty process rather than destabilize the kernel.

## 7. UI Agent Rules
- UI must remain original to ZEROOS.
- Familiarity is allowed; copying distinctive layouts/branding is not.
- Every animation has a reduced-motion/static fallback.
- Every screen must support keyboard navigation.
- Heavy effects are capability-dependent.
- UI work must not block core OS scheduling or I/O.

## 8. Performance Agent Rules
Use event-driven wakeups.
No periodic polling when an interrupt, queue, timer or notification mechanism can be used.
Services must declare:
- idle state,
- wake trigger,
- maximum wake frequency,
- memory footprint,
- I/O priority.

## 9. AI Agent Rules
AI may propose code but cannot waive invariants.
AI-generated patches require the same tests as human-written patches.
AI must state uncertainty instead of inventing APIs.
Never place secrets or credentials into prompts, logs or generated source.
AI runtime must be dormant unless requested by an eligible event.

## 10. Compatibility Agent Rules
Windows and Android layers are compatibility systems, not kernel shortcuts.
Keep translation boundaries explicit.
Never let compatibility code contaminate core kernel ABI.
Each runtime must have independent versioning and resource budgets.

## 11. Documentation Rules
Every new subsystem must answer:
- What does it do?
- What does it own?
- What does it depend on?
- What are its states?
- What are its invariants?
- How is it tested?
- How does it fail?
- How is it updated?
- How is it disabled/recovered?

## 12. Commit Rules
Commit messages should identify the subsystem and intent.
Examples:
- kernel: stabilize IRQ frame ownership
- memory: add page-map validation
- ui: add reduced-motion transition policy
- storage: add VFS mount lifetime checks

## 13. Stop Conditions
An agent must stop and report rather than guess when:
- an ABI is unclear,
- ownership is ambiguous,
- a test contradicts an invariant,
- required hardware behavior is unknown,
- a change would invalidate an existing documented contract.

## 14. Completion Checklist
[ ] Tests added
[ ] Existing tests pass
[ ] Failure path tested
[ ] Resource lifecycle documented
[ ] Public interfaces documented
[ ] No invariant weakened
[ ] CI/QEMU checked where applicable
[ ] Commit is scoped


## 15. Advanced-First Implementation Contract

Agents MUST NOT interpret stage names as permission to implement basic or disposable versions.

Required loop:

MATURITY AUDIT
→ PRODUCTION ARCHITECTURE
→ ROBUST IMPLEMENTATION
→ HARDENING
→ PERFORMANCE/RESOURCE REVIEW
→ FAILURE/RECOVERY TESTING
→ INTEGRATION
→ CI/QEMU/HARDWARE VERIFICATION

### Forbidden Agent Behavior

Do not:
- add a toy implementation because it is faster;
- create a placeholder API that will obviously be replaced;
- defer critical ownership/lifetime/security design;
- call a feature production because its happy path works;
- remove tests/assertions to obtain a green result;
- claim hardware compatibility without evidence.

### Dependency Rule

If a dependency is missing:
1. define the final production interface;
2. implement all independent production work;
3. isolate the blocked component;
4. test the boundary;
5. continue with other non-dependent production work.

### Production Review

Before declaring a subsystem complete, review:
- architecture;
- ownership/lifetime;
- concurrency;
- memory safety;
- interrupt safety;
- ABI/API;
- security;
- resource budgets;
- diagnostics;
- recovery;
- negative tests;
- stress/soak tests;
- CI;
- supported hardware.

The agent must report PARTIAL when any required production gate is not yet verified.
