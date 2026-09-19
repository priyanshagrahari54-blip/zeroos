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

## Atomic counters

The atomic_u64 primitive provides load, store, fetch-add and fetch-sub with
explicit memory ordering. The timer uses it for tick accounting.

## Design boundary

Mutexes, semaphores and wait queues are not faked here. They will be introduced
with task blocking/wakeup so a waiter can actually sleep instead of polling.

Linux similarly distinguishes spinning from sleeping locks and connects wait
queues to task sleep/wakeup. citeturn0search1turn0search0turn0search2

## Cost

A spinlock is one 32-bit word and an atomic counter is one 64-bit word. No
dynamic allocation or background worker is created by this layer.
