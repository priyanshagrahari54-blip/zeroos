# ZEROOS Task and Scheduler Architecture

ZEROOS now has a real kernel-task context layer and a scheduler boundary.

## Task model

Each task contains a stable ID, lifecycle state, saved kernel stack pointer,
one kernel stack page, entry function, and opaque argument.

States are UNUSED, RUNNABLE, RUNNING, BLOCKED, and ZOMBIE. BLOCKED is reserved
for the wait-queue stage.

## Context switching

kernel/context.S saves RBP, RBX, R12-R15 and RSP. The task return address
remains on its kernel stack, so restoring the stack and executing RET resumes
the task at its previous execution point.

A new task starts through task_trampoline(), which calls its registered entry
and converts normal return into task exit. The saved register set follows the
AMD64 SysV ABI, where RBX, RBP and R12-R15 are callee-saved. citeturn2search12

## Scheduling policy

The current policy is bounded round-robin over 16 task slots. This is a policy
boundary, not the final desktop scheduler. Linux separates scheduler core
mechanics from scheduling policy and runqueue operations, while modern Linux
uses more advanced fairness and latency mechanisms such as EEVDF. ZEROOS keeps
that separation so future policy changes do not require replacing task/context
infrastructure. citeturn0search1turn3search4

## Preemption boundary

Tasks can explicitly yield and context-switch now. Timer accounting is wired
through scheduler_tick(), but the PIT ISR does not yet perform an arbitrary
context switch. Interrupt-driven preemption requires ownership of the complete
interrupt return frame and will be added with that return-path design rather
than by performing a partial switch.

## Resource model

Each task currently consumes one physical 4 KiB kernel stack page plus a small
static task descriptor. No task-specific heap allocation is created.

## Next stage

The next kernel stage is wait queues, blocking/wakeup, a real idle task,
timer-driven preemption with complete interrupt-frame switching, scheduler
accounting, and eventually SMP/per-CPU runqueues.

An idle task is a standard scheduler concept: when no runnable task exists,
the CPU enters an idle loop rather than inventing work. citeturn3search9
