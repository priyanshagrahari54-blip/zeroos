# ZEROOS Synchronization

ZEROOS has scheduler-independent spin/atomic primitives plus scheduler-integrated wait queues.

## Spinlocks

The low-level spinlock uses x86-64 atomic compare/exchange with acquire/release
ordering and PAUSE while contended. IRQ-safe locking saves RFLAGS, disables
interrupts, acquires the lock, and restores the previous interrupt state on
unlock.

This is intended for tiny critical sections that may be touched by both kernel
code and interrupt handlers. Sleeping locks are deliberately absent because
sleeping requires task/blocking state.

## Atomic counters and lock diagnostics

The atomic_u64 primitive provides load, store, fetch-add and fetch-sub with
explicit memory ordering. The timer uses it for tick accounting.

Spinlocks expose try-lock, bounded-acquisition and contention-count APIs for
negative tests and watchdog/diagnostic consumers. Unbounded `spin_lock()` is
reserved for short kernel-critical sections whose lock ordering is already
proven.

A writer-preference `rwlock` is available for read-mostly metadata. It is a
non-sleeping primitive; callers that may block must use a wait queue around a
higher-level condition instead of spinning indefinitely.

## Lock order

Kernel spinlocks are acquired irqsave and follow one documented order that is
never inverted:

    wait_queue::lock  ->  task_lock  ->  runqueue::lock  ->  memory_lock
    process_lock / thread_lock                         ->  memory_lock

- `task_lock` serializes scheduler metadata: task table transitions, the
  deadline queue, successor selection, per-CPU runqueue ownership and
  context-ownership publication. It is always acquired with interrupts
  disabled and is released before any `context_switch_ex()` handoff. A
  per-CPU handoff-quarantine slot remains published until the destination
  context has crossed the assembly boundary, so the remote reaper cannot free
  a zombie's stack while the outgoing context is still being saved. Each
  runqueue also has its own lock, acquired only after `task_lock`, for queue
  structure/accounting validation and future finer-grained operations.
- `memory_lock` serializes the physical page allocator bitmap, including
  frees from the scheduler reclaimer and VMM teardown paths.
- `process_lock` serializes process-table mutations and process/thread
  counters. `thread_lock` serializes thread-table mutations. `thread_reap()`
  holds `thread_lock` and then takes `process_lock` (detach); the reverse
  order is never used.

Every mutation of task lifecycle state, deadline-queue membership or context
ownership metadata happens under `task_lock`; wait-queue membership is owned
by the corresponding `wait_queue::lock`. A finite wait-queue waiter is linked
to both queues and carries an explicit timeout marker. Timer expiry records
such waiters under `task_lock`, releases it, then detaches and wakes them under
the documented wait-queue-to-task lock order. Event wake and endpoint close
remove a timed waiter from both queues before it resumes. No lock is held
across a context switch.

## Design boundary

Wait queues are implemented with real task blocking/wakeup, including finite
deadlines; they do not poll conditions once per tick. Mutexes and semaphores
remain higher-level primitives to add only when their ownership, cancellation,
and priority semantics have executable coverage.

## Cost

A spinlock is one 32-bit word and an atomic counter is one 64-bit word. No
dynamic allocation or background worker is created by this layer.
