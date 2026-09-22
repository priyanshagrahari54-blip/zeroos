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
  sleep queue, successor selection and context-ownership publication. It is
  always acquired with interrupts disabled and is released before any
  `context_switch_ex()` handoff. The handoff window itself is atomic on the
  current UP configuration because it runs with IF=0; per-runqueue ownership
  across the handoff is the marked boundary that moves to per-runqueue locks
  in the SMP stage.
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

## Design boundary

Mutexes, semaphores and wait queues are not faked here. They will be introduced
with task blocking/wakeup so a waiter can actually sleep instead of polling.

Linux similarly distinguishes spinning from sleeping locks and connects wait
queues to task sleep/wakeup. citeturn0search1turn0search0turn0search2

## Cost

A spinlock is one 32-bit word and an atomic counter is one 64-bit word. No
dynamic allocation or background worker is created by this layer.
