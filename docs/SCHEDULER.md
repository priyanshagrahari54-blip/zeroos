# ZEROOS Task, Scheduler, Idle, and Wait-Queue Architecture

ZEROOS has a kernel-task context layer, a bounded scheduler, a permanent idle
task, and scheduler-owned blocking/wakeup primitives.

## Task model

Each task contains a stable ID, lifecycle state, saved kernel stack pointer,
one kernel stack page, entry function, opaque argument, and intrusive wait-list
links. States are UNUSED, RUNNABLE, RUNNING, BLOCKED, and ZOMBIE.

Slot zero is the bootstrap execution context. Slot one is a permanent idle task.
Ordinary kernel tasks use the remaining slots.

## User-mode tasks

A task can be the main thread of a user-mode process. The process owns the
address space and the process's memory; the task owns the CPU context.
Such a task is created with a ring-3 entry frame (user RIP/CS/RFLAGS/RSP/SS
plus a single argument). Its first execution enters through an assembly
user entry that `iretq`s directly into the process address space at CPL 3
with IF enabled, so user code is timer-preemptible from the first
instruction.

Two ownership rules keep the CPU state consistent:

- On every context switch the scheduler loads the target task's address
  space (CR3, PCID-tagged when available) and sets TSS RSP0 to the target
  task's interrupt headroom. The address space and the ring-3 stack pointer
  always belong to the task about to run.
- A task's live `interrupt_frame` is captured by the IRQ-exit scheduler
  only when a real switch is committed. On the no-switch paths the frame is
  consumed by the `iretq` epilogue and is never retained; a retained
  pointer to a consumed frame would be resumed a second time.

A user task leaves user mode only through a syscall (SYSCALL entry) or by
taking an exception/interrupt. The `exit` syscall and a contained user
fault both turn the task into a zombie and switch away through the normal
switch paths; the process's address space is released later by the process
layer.

## Context switching

kernel/context.S saves RBP, RBX, R12-R15 and RSP. The task return address
remains on its kernel stack, so restoring the stack and executing RET resumes
the task at its previous execution point.

## Scheduling policy

The current policy is bounded round-robin over the ordinary task slots. The
idle task is excluded from normal round-robin selection and is chosen only when
no ordinary task is runnable. The task/context mechanics remain separate from
policy so later fairness, latency, or per-CPU runqueue work can evolve without
replacing task stacks and context state.

## Idle execution

The idle task executes HLT with interrupts enabled and yields after wakeup.
This gives ZEROOS a real no-work execution context instead of treating the
bootstrap continuation as an idle task. A dedicated idle task is a standard
scheduler model: the CPU runs it when there is no other runnable work, and the
idle loop can enter processor idle states. citeturn1search0

The current implementation intentionally keeps the PIT tick enabled while
idle. Tickless idle can be added later after the timer subsystem can reprogram
the next wakeup event safely; avoiding scheduler ticks during long idle
intervals is an established power optimization. citeturn1search1turn1search4

## Blocking and wakeup

Wait queues use a spinlock plus an intrusive FIFO of task descriptors. The
waiter is first marked BLOCKED and linked while the queue lock is held. The
queue lock is released before the scheduler performs the context switch.

A wake-one or wake-all operation removes waiters from the queue before making
them RUNNABLE. Waking never performs a context switch itself.

The condition itself remains owned by the caller. A producer changes the
condition and then wakes the queue; a waiter re-checks its condition after
resuming.

## Preemption boundary

Timer-driven preemption now occurs at the common IRQ-exit boundary rather than
inside the C timer handler. The timer tick only accounts CPU time and raises
the task's `need_resched` flag when its time slice expires.

The common ISR saves the complete architectural register frame and passes its
address through `interrupt_dispatch()`. After the IRQ handler and PIC EOI,
the task layer may select another runnable task and return that task's saved
interrupt frame. The assembly epilogue then loads the selected frame and uses
the normal `popq` + `iretq` path. This prevents the earlier unsafe pattern
where a normal C-call-frame context switch was attempted while an interrupt
return frame was still owned by the interrupted task.

New tasks start with only their ABI-valid cooperative context. The IRQ-exit
scheduler can restore that context directly through the tagged `saved_stack`
path. A live `interrupt_frame` exists only for a task that has actually been
preempted. Cooperative scheduling deliberately skips such tasks until an IRQ
exit can restore their live architectural frame; this prevents a stale
cooperative return address from being mistaken for the interrupted execution
point.

The design follows the same architectural principle used by mature kernels:
interrupt entry/exit and scheduling state are explicit boundaries, and the
scheduler does not corrupt an in-flight interrupt frame. citeturn3search1turn2search13

## Timed sleep and deadlines

Kernel tasks can sleep for a number of monotonic PIT ticks through `task_sleep_ticks()` / `scheduler_sleep_ticks()`. Sleeping tasks are kept in a time-ordered intrusive list using their task descriptor, so the common case requires no heap allocation. The timer path wakes all expired deadlines before the scheduler considers preemption.

The current timer is intentionally tick-granular at 100 Hz: a five-tick sleep has a nominal 50 ms duration and wakeup occurs on the first tick at or after its deadline. Deadline arithmetic uses unsigned 64-bit monotonic ticks with signed-difference ordering, making normal wraparound-safe comparisons possible for deadlines within the representable half-range.

This is deliberately a low-overhead timeout mechanism rather than the final high-resolution timer subsystem. Mature timer architectures separate low-resolution timeout scheduling from high-resolution event timers; ZEROOS can add a clocksource/clockevent layer and high-resolution timers later without changing the task sleep API. citeturn0search0turn0search2

## Task lifecycle and reclamation

A task that returns from its entry function becomes `TASK_ZOMBIE`. Its kernel stack cannot be freed by the task itself because execution is still using that stack. The scheduler therefore reclaims zombie stacks from a later timer/scheduler context, resets the descriptor to `TASK_UNUSED`, and returns the physical page to the page allocator. This makes task slots reusable without allocating a separate reaper thread or permanent reaper stack.

The lifecycle is therefore `UNUSED → RUNNABLE → RUNNING → BLOCKED/RUNNABLE → ZOMBIE → UNUSED`. A blocked task cannot become a zombie until it is explicitly resumed and exits.

## Resource model

The permanent idle task costs one 4 KiB kernel stack page plus one static task
descriptor. No heap allocation is used for idle execution or waiters.

## Next stage

Scheduler certification now covers cooperative switching, callee-saved register preservation, timer-only CPU-bound preemption, mixed cooperative/preemptive transitions, wait/wakeup, timed sleep, zombie reclamation, slot reuse, and timer-driven preemption of ring-3 user tasks. The next scheduler stage is long-duration fairness/latency measurement, followed by per-CPU runqueues as part of SMP preparation. Higher-level synchronization can continue to build on the existing wait-queue and preemption boundaries.
