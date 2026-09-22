# ZEROOS Task, Scheduler, Idle, and Wait-Queue Architecture

ZEROOS has a kernel-task context layer, a bounded scheduler, a permanent idle
task, and scheduler-owned blocking/wakeup primitives.

## Task model

Each task contains a stable ID, lifecycle state, saved kernel stack pointer,
one allocator-backed kernel stack page, its aligned TSS.RSP0 value, entry
function, opaque argument, and intrusive wait-list links. States are UNUSED,
RUNNABLE, RUNNING, BLOCKED, and ZOMBIE. Before every context handoff the
selected task's kernel stack top is published to the runtime TSS, making the
stack ownership boundary explicit for future privilege transitions.

Slot zero is the bootstrap execution context. Slot one is a permanent idle task.
Ordinary kernel tasks use the remaining slots.

## Context switching

kernel/context.S provides the single scheduler handoff primitive
`context_switch_ex(old_sp, new_sp, new_frame)`. It saves RBP, RBX, R12-R15
and the return address on the outgoing task's kernel stack and then resumes
the successor through either resumable context form:

- `new_frame != 0`: the successor was preempted and owns a live hardware
  interrupt frame on its own stack; the frame is resumed with the
  architectural pop/iretq sequence and the frame pointer is consumed
  (retired) at the exact moment the handoff takes ownership of it.
- `new_frame == 0`: the successor suspended cooperatively; its callee-saved
  registers are restored and RET resumes the task at its previous execution
  point.

Both forms are legal dispatch targets for every scheduler entry point
(IRQ exit, yield, block, sleep, exit). A task suspended with a hardware
frame is therefore never skipped by cooperative dispatch, and the idle task
is always resumable even when every ordinary task is frame-suspended.

## Context ownership contract

Exactly one of the following holds at every scheduler-observable point,
enforced fatally by task_validate_table() on every creation, dispatch and
timer tick:

- A RUNNING task owns the CPU and never carries a resumable
  `interrupt_frame`. A live IRQ frame while running belongs to the active
  interrupt path only and is consumed by that path's iretq/epilogue.
- A RUNNABLE or BLOCKED task owns exactly one resumable context: either a
  valid hardware interrupt frame (exact preemption point) or a valid
  cooperative saved context.
- A ZOMBIE task owns no resumable context and no queue links.

A consumed frame pointer is never retained, and a stale frame pointer is
never silently cleared: violations are fatal diagnostics. The table dump at
panic time reports every slot, saved stack and return address so the first
invalid transition can be located.

The bootstrap task in slot zero is a special pre-scheduler context. It runs on
the boot stack rather than a task-owned stack page, so a PIT interrupt can
arrive while task initialization is still in progress. Such interrupts are
returned through their original architectural frame without entering the task
scheduler. Once scheduler_start() transfers execution away from slot zero,
only task-owned contexts participate in scheduler validation and switching.

## Scheduling policy

The current UP policy is a bounded priority-aware round-robin over the
ordinary task slots. Priorities are explicit (0–31), CPU affinity is an
explicit bitmask, and equal effective priorities retain deterministic slot
order. Runnable aging promotes a waiting task after bounded wait time, which
prevents starvation without a polling worker or an unbounded queue.

The idle task is excluded from normal selection and is chosen only when no
ordinary task is runnable. Task mechanics remain separate from policy so the
verified task stacks and context ownership can move to per-CPU runqueues
without an ABI rewrite. `task_set_priority()` and `task_set_affinity()` reject
unsupported CPU masks rather than silently claiming an offline CPU.

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

Frame publication follows one rule: the interrupted frame becomes a task's
resumable `interrupt_frame` only at the moment the scheduler switches away
from that task. If the IRQ returns to the interrupted task instead, the frame
is consumed by that very iretq and is never recorded in the task table. This
keeps the RUNNING-without-frame invariant true at every observable point and
removes the failure mode where a task kept a stale frame pointer across its
own execution.

New tasks start with only their ABI-valid cooperative context. Any dispatch
path can restore that context through the tagged `saved_stack` path, and any
dispatch path can resume a preempted task through its live architectural
frame via the same handoff primitive. Selection therefore treats both context
forms uniformly: a runnable task is never skipped because of the way it was
suspended.

The design follows the same architectural principle used by mature kernels:
interrupt entry/exit and scheduling state are explicit boundaries, and the
scheduler does not corrupt an in-flight interrupt frame. citeturn3search1turn2search13

## Timed sleep and deadlines

Kernel tasks can sleep for a number of monotonic PIT ticks through
`task_sleep_ticks()` / `scheduler_sleep_ticks()`. Sleeping tasks are kept in
a time-ordered intrusive list using their task descriptor, so the common case
requires no heap allocation. The timer path wakes all expired deadlines
before the scheduler considers preemption.

The scheduler deadline unit remains tick-granular at 100 Hz for deterministic
wakeup semantics. `timer_monotonic_ns()` additionally exposes a high-resolution
clocksource for measurement and future clock-event programming; wall-clock RTC
samples are deliberately separate from deadline time. Deadline arithmetic uses
unsigned 64-bit monotonic ticks with signed-difference ordering, making normal
wraparound-safe comparisons possible for deadlines within the representable
half-range.

## Task lifecycle and reclamation

A task that returns from its entry function becomes `TASK_ZOMBIE`. Its kernel stack cannot be freed by the task itself because execution is still using that stack. The scheduler therefore reclaims zombie stacks from a later timer/scheduler context, resets the descriptor to `TASK_UNUSED`, and returns the physical page to the page allocator. This makes task slots reusable without allocating a separate reaper thread or permanent reaper stack.

The lifecycle is therefore `UNUSED → RUNNABLE → RUNNING → BLOCKED/RUNNABLE → ZOMBIE → UNUSED`. A blocked task cannot become a zombie until it is explicitly resumed and exits.

## Resource model

The permanent idle task costs one 4 KiB kernel stack page plus one static task
descriptor. No heap allocation is used for idle execution or waiters.

## Certification gate

The scheduler self-test reports independent success markers for cooperative
context switching, wait/wakeup, timed sleep, timer-only preemption,
zombie/slot-reuse lifecycle stress, the interrupt-frame ownership invariant,
the process/thread object model, and generation-tagged PID/TID reuse
protection, before an aggregate scheduler certification passed marker. The
aggregate certificate additionally requires the process/thread probe suite to
complete. CI requires all of these markers across three consecutive QEMU
boots and fails the boot test if a ZEROOS PANIC: is present in the serial
log. This makes scheduler certification a runtime-tested CI gate rather than
a documentation-only claim.

## Next stage

Scheduler certification now covers cooperative switching, callee-saved register
preservation, timer-only CPU-bound preemption, wait/wakeup, timed sleep, bounded
deadlock detection, zombie reclamation, slot reuse, the interrupt-frame
ownership contract, uniform dispatch of both resumable context forms, and the
pre-scheduler bootstrap interrupt boundary. The next scheduler stage is
long-duration fairness/latency measurement, followed by per-CPU runqueues as
part of SMP preparation (the switch-handoff window currently relies on IF=0
UP atomicity and is the marked boundary that moves to per-runqueue locks).
Higher-level synchronization can continue to build on the existing wait-queue
and preemption boundaries.
