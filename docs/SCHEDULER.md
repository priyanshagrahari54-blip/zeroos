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

## SMP scheduling policy and ownership

The scheduler now has one current-task slot and one intrusive runqueue per
online CPU. `current_tasks[cpu]` is mirrored in the per-CPU CPU-local record
and is the only owner of a RUNNING task on that CPU. Ordinary RUNNABLE tasks
are present on exactly one `task_runqueue[cpu]`; a task is removed before it
becomes RUNNING and reinserted only after it becomes RUNNABLE again. Queue
length, head/tail links, and the task's `runqueue_cpu` owner are validated at
every scheduler checkpoint.

A global task metadata lock serializes state/lifecycle transitions, while each
runqueue has its own lock and is accessed in the fixed order
`task_lock -> runqueue_lock`. This prevents remote wakeups, queue stealing,
affinity migration, and local dispatch from observing a half-published task.
The global lock is released before `context_switch_ex()`, so no scheduler
lock is held across an architectural stack handoff.

Selection prefers the local queue, then steals a compatible task from another
online queue when the local queue is empty. Effective priority remains
priority plus bounded runnable aging; equal priorities are FIFO by queue
insertion. CPU affinity is checked both at enqueue and dispatch. A runnable
task whose current CPU is removed from its affinity mask is migrated to an
online compatible queue at its next handoff rather than executed on a
forbidden CPU.

The BSP bootstrap context is not a runqueue task. The BSP has a dedicated idle
task, and every AP has a separate idle context backed by its private AP
bootstrap stack. Thus two CPUs never execute the same idle stack. Idle contexts
are excluded from ordinary queues and selected only when their local/stealable
queues are empty. `task_set_priority()` and `task_set_affinity()` reject
invalid masks and never silently claim an unavailable CPU.

## Idle execution and AP entry

The BSP idle task executes HLT with interrupts enabled and yields after wakeup.
Each AP enters `task_start_secondary_cpu()` on its private bootstrap stack
immediately after SMP publication. APs wait with interrupts enabled until the
BSP publishes the scheduler start gate, then install their own idle current
slot, validate the per-CPU ownership record, and dispatch from their own
queue. A scheduler wake IPI releases an AP from HLT; it never performs a
context switch from interrupt context.

The same PIT remains the physical clock source on the BSP. Each AP first
calibrates and owns a periodic local LAPIC timer against the running PIT. If a
platform cannot calibrate that local clock event, the BSP uses a targeted
scheduler-tick IPI as the explicit fallback for that AP; the fallback is
recorded separately from the local-timer-ready state, is validated before the
AP is accepted by the startup self-test, and does not change task ownership.
Each AP accounts its own current task, wakes its own local/remote-owned
runnable work, and decides its own preemption at the common IRQ-exit boundary.
No AP ever borrows the BSP task or runqueue state.

The implementation intentionally keeps ticks enabled while idle. Tickless
idle is deferred until clock-event reprogramming and wakeup cancellation have
explicit ownership and recovery tests.

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

Timer-driven preemption occurs at the common IRQ-exit boundary rather than
inside the C timer handler. The BSP PIT handler accounts the global tick and
raises the BSP task's `need_resched` flag when its time slice expires. The
scheduler hook then delivers the same event as a dedicated scheduler IPI to
each online AP; the AP performs its own accounting and sets its own
`need_resched`. No AP runs the BSP scheduler tick function as a substitute for
its current-task ownership.

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

SMP reclamation is handoff-quiescent rather than merely state-based. Before releasing the scheduler metadata lock, a cooperative dispatch publishes the outgoing task in a per-CPU handoff-quarantine slot. The reaper will not free that task's stack while the slot is published. The destination clears the slot after it crosses the cooperative context boundary; a frame destination clears it from the assembly handoff immediately before loading the destination frame and executing `iretq`. This closes the race in which a remote timer could reclaim and reuse a zombie's stack while `context_switch_ex()` was still saving registers on it. The quarantine is protected by the same task lock and is included in the fatal scheduler ownership checks.

The lifecycle is therefore `UNUSED → RUNNABLE → RUNNING → BLOCKED/RUNNABLE → ZOMBIE → UNUSED`, with an explicit architectural handoff-quarantine interval between `ZOMBIE` and reclamation. A blocked task cannot become a zombie until it is explicitly resumed and exits.

## Resource model

The permanent idle task costs one 4 KiB kernel stack page plus one static task
descriptor. No heap allocation is used for idle execution or waiters.

## Certification gate

The scheduler self-test reports independent success markers for cooperative
context switching, wait/wakeup, timed sleep, timer-only preemption, equal-
priority fairness and wakeup-latency stress, zombie/slot-reuse lifecycle
stress, the interrupt-frame ownership invariant, the process/thread object
model, and generation-tagged PID/TID reuse protection, before an aggregate
scheduler certification passed marker. The
aggregate certificate additionally requires the process/thread probe suite and
`per-CPU scheduler ownership verified` marker. On multi-CPU boots the latter
requires a real non-idle task to execute on an AP, not merely that an AP was
reported online. CI requires all markers across three consecutive QEMU boots,
a four-vCPU boot, and NX-disabled compatibility boot; any `ZEROOS PANIC:` in a
serial log fails the gate.

## Current exit-gate scope

The implemented scheduler/SMP boundary covers:

- per-CPU current-task publication and private idle contexts;
- per-CPU runqueue insertion/removal, queue accounting, compatible stealing,
  affinity migration, and fixed lock ordering;
- AP scheduler entry after a release-published start gate;
- BSP-owned PIT tick distribution as per-CPU scheduler IPIs;
- local IRQ-exit preemption with either a cooperative context or a live
  interrupt frame;
- timeout wakeup, remote wakeup, zombie reclamation, and concurrent queue
  validation;
- runtime proof that ordinary work executed on a secondary CPU; and
- coordinated CPU hot-offline queue evacuation, AP TLB/CPU-local withdrawal,
  idle parking, bounded acknowledgement and scheduler validation.

The remaining Stage 1 scheduler work is not hidden: extended FPU state
switching and supported-hardware multi-vCPU validation remain required before
the Stage 1 exit gate. Equal-priority fairness/latency stress, the AP late-
token/failed-dispatch recovery contract, and the per-AP local LAPIC clock-
event contract (with an explicit targeted-IPI fallback) are now exercised by
the runtime certification, including a separate fault-injected QEMU boot that
must complete the bounded retry.
Higher-level synchronization continues to use the existing wait-queue and
preemption contracts.
