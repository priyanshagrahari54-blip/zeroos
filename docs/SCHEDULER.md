# ZEROOS Task, Scheduler, and Wait-Queue Architecture

ZEROOS has a kernel-task context layer, a bounded scheduler, and scheduler-owned
blocking/wakeup primitives.

## Task model

Each task contains a stable ID, lifecycle state, saved kernel stack pointer,
one kernel stack page, entry function, opaque argument, and intrusive wait-list
links. States are UNUSED, RUNNABLE, RUNNING, BLOCKED, and ZOMBIE.

## Context switching

kernel/context.S saves RBP, RBX, R12-R15 and RSP. The task return address
remains on its kernel stack, so restoring the stack and executing RET resumes
the task at its previous execution point.

## Scheduling policy

The current policy is bounded round-robin over 16 task slots. The task/context
mechanics are separate from policy so later fairness, latency, or per-CPU
runqueue work can evolve without replacing task stacks and context state.

## Blocking and wakeup

Wait queues use a spinlock plus an intrusive FIFO of task descriptors. The
waiter is first marked BLOCKED and linked while the queue lock is held. The
queue lock is released before the scheduler performs the context switch. This
keeps the queue lock out of the sleeping interval and prevents a waker from
being forced to wait for the blocked task to resume.

A wake-one or wake-all operation removes waiters from the queue before making
them RUNNABLE. Waking never performs a context switch itself, so the primitive
can be used by interrupt-oriented paths once the scheduler's interrupt
boundary is extended.

The condition itself remains owned by the caller. A producer changes the
condition and then wakes the queue; a waiter re-checks its condition after
resuming. The wait queue therefore provides scheduling synchronization rather
than becoming a hidden event state.

This follows the established wait-queue principle that the waiter is prepared
before sleeping, the condition is re-checked after wakeup, and wakeup follows
the state change. citeturn1search0turn1search1

## Resource model

There is no per-wait heap allocation. Each task carries its own intrusive wait
node, so adding a waiter costs only existing task memory plus the queue head.

## Current boundary

The implementation is kernel-task-only. Timeouts, interruptible sleep,
signals, user address spaces, priority inheritance, and SMP runqueues are not
exposed until their underlying scheduler/state machinery exists.

## Next stage

The next kernel stage is a real idle task, timer-driven preemption with complete
interrupt-frame switching, scheduler accounting, and then sleep/timeouts and
higher-level synchronization primitives such as completions and mutexes.
