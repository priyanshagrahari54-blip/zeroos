# ZEROOS Synchronization

ZEROOS now has a scheduler-independent synchronization layer.

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

    wait_queue::lock  ->  task_lock  ->  memory_lock
    process_lock / thread_lock      ->  memory_lock

- `task_lock` serializes scheduler metadata: task table transitions, the
  sleep queue, successor selection, per-CPU runqueue ownership and
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

Every mutation of task lifecycle state, sleep-queue membership or context
ownership metadata happens under `task_lock`; wait-queue membership is owned
by the corresponding `wait_queue::lock`. A lock is never held across a
context switch.

## Wait queues and blocking

Wait queues are now production: `wait_queue_prepare` publishes current task as blocked while holding outer condition lock (e.g., `ipc_lock`), with interrupts disabled, closing lost-wakeup window. `wait_queue_commit` performs scheduler block transition. `wait_queue_wake_one/all` pops waiter and calls `task_wake`. Used by IPC message, pipe, event, and child wait.

Timed variants currently use 1-tick polling via `task_sleep_ticks(1)` for timeout path because scheduler invariant forbids task being in both wait_queue and sleep queue simultaneously (`wait_queue && sleep_armed` panics). Infinite timeout uses event-driven wait_queue. Documented limitation, bounded latency.

## IPC synchronization

- Message queue: send blocks when peer count >= QUEUE_DEPTH, receive blocks when count==0. Condition check and waiter publication serialized by ipc_lock.
- Pipe byte-stream: write blocks when free==0, read blocks when count==0. Partial transfers: write min(free, requested), read min(available, requested). PEEK does not consume nor wake writer.
- Event: coalescing bit, signal sets bit and wakes one waiter, wait consumes bit unless PEEK.
- Close/cancellation: endpoint_destroy wakes all send and receive waiters on both endpoints.
- No lost wakeups: condition and waiter publication under same lock, wake after state change.

## Design boundary

Mutexes, semaphores, rwlock sleep variants are future; current production primitives are spinlocks, atomic_u64, rwlock (non-sleeping), and wait queues for blocking.

## Cost

A spinlock is one 32-bit word, atomic_u64 one 64-bit word, wait_queue head/tail pointers plus spinlock. No dynamic allocation or background worker in data path. Pipe ring buffer 2048 bytes per endpoint static, no alloc.
