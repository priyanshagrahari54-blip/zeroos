# ZEROOS Task, Scheduler, Idle, and Wait-Queue Architecture

ZEROOS has a kernel-task context layer, a bounded scheduler, a permanent idle
task, and scheduler-owned blocking/wakeup primitives.

## Task model

Each task contains a stable ID, lifecycle state, saved kernel stack pointer,
one kernel stack page, entry function, opaque argument, and intrusive wait-list
links. States are UNUSED, RUNNABLE, RUNNING, BLOCKED, and ZOMBIE.

Slot zero is the bootstrap execution context. Slot one is a permanent idle task.
Ordinary kernel tasks use the remaining slots.

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

Tasks receive an iret-compatible synthetic frame when created, so a task that
has not yet taken its first interrupt can still be selected by IRQ-exit
preemption. Once a task is actually interrupted, its hardware-generated frame
replaces the synthetic pointer.

The design follows the same architectural principle used by mature kernels:
interrupt entry/exit and scheduling state are explicit boundaries, and the
scheduler does not corrupt an in-flight interrupt frame. citeturn3search1turn2search13

## Resource model

The permanent idle task costs one 4 KiB kernel stack page plus one static task
descriptor. No heap allocation is used for idle execution or waiters.

## Next stage

With interrupt-exit preemption in place, the next scheduler work is to harden
accounting and lifecycle management, add timed sleep, reclaim ZOMBIE task
stacks, and then introduce a real per-CPU runqueue model before SMP support.
Higher-level synchronization primitives can build on the existing wait-queue
and preemption boundaries.
