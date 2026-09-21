# ZEROOS Process and Thread Architecture

## Purpose

ZEROOS now separates the higher-level process resource container from the
lower-level task scheduler context.

The process owns:
- process identity (PID)
- parent/child relationships
- the private address-space root
- process lifecycle state
- thread membership and thread counts
- process exit status

The thread owns:
- thread identity (TID)
- owning process
- thread lifecycle state
- entry function and argument
- the association to its scheduler task
- thread exit status

The scheduler task remains the low-level execution context used by the existing
scheduler and context-switch implementation. This keeps scheduler mechanics
independent from the future user-mode process ABI.

## Process identity

PIDs are 64-bit generation-tagged handles.

The low 16 bits encode the process-table slot plus one. The upper bits encode a
generation counter.

Therefore a process that is reaped and later reuses the same table slot gets a
different PID. process_lookup() validates both the slot and generation.

The generation counter is never reset when a descriptor is reaped. When it
reaches its maximum representable value, that slot is permanently unavailable
instead of wrapping and revalidating stale handles.

## Thread identity

TIDs use the same generation-tagged handle scheme independently of PIDs.

Thread IDs therefore cannot collide merely because a thread descriptor slot is
reused, and a previously valid TID is rejected after the descriptor is reaped.

The scheduler's legacy task ID remains separate from the public thread ID. The
thread stores the scheduler task ID as the low-level execution association.

## Parent and child ownership

Processes form an intrusive parent/child tree:

    parent
      |
      +-- child
      +-- child
      `-- child

A parent owns the child relationship until the child process is reaped.
Reaping removes the child from the parent's list and decrements the parent's
child count.

The current implementation does not yet implement orphan adoption, process
groups, sessions, or signals.

## Address-space ownership

Every process created through process_create() receives a dedicated
struct vmm_space.

The VMM currently gives each space a private user PML4 slot while sharing the
kernel mapping root used by the existing bootstrap architecture. The process
object is the lifecycle owner of that address-space root and destroys it
during process reaping.

User virtual-memory population, page-fault handling, demand paging, and user
stack construction are deliberately implemented in later Stage 1 work.

## Thread creation

thread_create_kernel() is the canonical process-owned kernel-thread creation
path at this stage.

Creation sequence:

    reserve process thread slot
             |
             v
    reserve thread descriptor
             |
             v
    disable current-thread preemption
             |
             v
    create scheduler task
             |
             v
    bind task -> thread
             |
             v
    attach thread to process
             |
             v
    re-enable preemption

Preemption is disabled around the task/ownership publication boundary so a
timer interrupt cannot run a newly created task before its process/thread
links are complete.

The primitive intentionally requires an already-running schedulable kernel
thread as its caller. A bootstrap-specific user-thread construction path will
be added together with the Ring-3 transition architecture rather than
overloading this API.

## Thread lifecycle

The current thread lifecycle is:

    UNUSED
       |
       v
      NEW
       |
       v
   RUNNABLE
       |
       v
    RUNNING
       |
       v
    ZOMBIE
       |
       v
    UNUSED

The low-level task can independently move through the scheduler's existing
RUNNABLE/RUNNING/BLOCKED/ZOMBIE states. The thread object records the
higher-level process/thread lifecycle without changing the proven scheduler
state machine.

When the final live thread exits:
- the thread becomes a zombie;
- the process live-thread count reaches zero;
- the process records the exit status;
- the process becomes PROCESS_ZOMBIE.

Thread descriptors are reaped separately. A process cannot be reaped until
all of its thread descriptors have been detached and it has no children.

## Reaping

Thread reaping:
- requires a zombie thread;
- requires the scheduler task association to be gone;
- removes the thread from its process;
- returns the thread exit status;
- resets the descriptor while retaining its generation.

Process reaping:
- requires PROCESS_ZOMBIE;
- requires zero live threads;
- requires zero retained thread descriptors;
- requires no thread creation in progress;
- requires no children;
- removes the process from its parent's child list;
- destroys the process address-space root;
- resets the descriptor while retaining its generation.

This explicit ordering avoids freeing process resources while a thread or
child still owns a reference to them.

## Locking and concurrency

process_lock protects process-table state and parent/child/thread-membership
metadata.

thread_lock protects thread-table identity and descriptor allocation.

The current creation path deliberately releases thread_lock before acquiring
process_lock, so the two table locks do not form a lock cycle. The scheduler
task lock remains owned by the task subsystem.

The architecture is currently single-CPU, but the ownership boundaries are
chosen so that the eventual SMP conversion can add per-CPU state without
merging process and scheduler policy.

## Current limitations

This subsystem is intentionally not the complete process ABI yet. Still
missing are:
- blocking wait() semantics
- parent-directed child wait
- process groups and sessions
- signals/events
- file-descriptor tables
- security credentials/capabilities
- resource limits and accounting
- user-thread creation
- kernel-stack/user-stack separation for Ring 3
- syscall ABI
- ELF loading and exec
- user fault containment

These are subsequent dependencies, not hidden inside the current process
object model.

## Certification

The runtime self-test exercises:
- process creation and PID lookup
- parent/child linkage
- private per-process address-space creation
- kernel-thread creation
- thread-to-process ownership
- thread exit and zombie state
- thread reaping
- process zombie transition
- process child unlinking
- process reaping
- stale PID/TID invalidation after reap
- descriptor reuse with generation-changing PID/TID values

CI requires the process/thread pass markers in addition to the existing
scheduler certification markers.