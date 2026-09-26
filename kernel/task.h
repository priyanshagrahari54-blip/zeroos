#ifndef ZEROOS_TASK_H
#define ZEROOS_TASK_H
#include "types.h"

#define ZEROOS_MAX_TASKS 48
/* Four contiguous pages: storage call chains (VFS -> journal -> cache ->
 * block -> driver) plus a nested IRQ frame must fit with headroom. */
#define ZEROOS_TASK_STACK_PAGES 4ULL
#define ZEROOS_TASK_STACK_SIZE (ZEROOS_TASK_STACK_PAGES * 4096ULL)
#define ZEROOS_TASK_STACK_PAINT 0x5354414b50414e54ULL
#define ZEROOS_TASK_STACK_GUARD 0x5a45524f5441534bULL
#define ZEROOS_TASK_PRIORITY_MIN 0U
#define ZEROOS_TASK_PRIORITY_DEFAULT 16U
#define ZEROOS_TASK_PRIORITY_MAX 31U
#define ZEROOS_TASK_AFFINITY_ANY (~0ULL)
#define ZEROOS_SCHEDULER_TICK_VECTOR 48U
#define ZEROOS_SCHEDULER_WAKE_VECTOR 49U
#define ZEROOS_SCHEDULER_OFFLINE_VECTOR 50U

struct interrupt_frame;
struct thread;
typedef void (*task_entry_t)(void *argument);

enum task_state {
    TASK_UNUSED=0,
    TASK_RUNNABLE,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_ZOMBIE
};

struct wait_queue;

/*
 * Context ownership contract (enforced by task_validate_table()):
 *
 *   RUNNING   The task owns the CPU. It never carries a resumable
 *             interrupt_frame: any live IRQ frame belongs to the active
 *             interrupt path and is consumed by that path's iretq/epilogue.
 *
 *   RUNNABLE  Suspended and resumable through exactly one live context:
 *             - interrupt_frame != 0: a hardware frame on this task's own
 *               stack, created by preemption. Resumption consumes the frame
 *               pointer and resumes with pop/iretq (exact interruption
 *               point), or
 *             - interrupt_frame == 0: a cooperative callee-saved context in
 *               saved_stack, created by context_switch_ex(). Resumption pops
 *               the callee-saved set and retq's into the frozen C
 *               continuation.
 *
 *   BLOCKED   Same resumable-context rule as RUNNABLE.
 *
 *   ZOMBIE    No resumable context; reclaimed from a later scheduler tick.
 *
 * Both context forms are legal dispatch targets for every scheduler entry
 * point (IRQ exit, yield, block, sleep, exit). A consumed frame pointer is
 * never retained and a stale frame pointer is never rebuilt or cleared
 * silently: violations are fatal diagnostics.
 */
struct task {
    uint64_t id;
    enum task_state state;
    uint64_t saved_stack;
    uint64_t stack_base;
    /* TSS.RSP0 value for this task while it can receive a privilege entry. */
    uint64_t kernel_stack_top;
    task_entry_t entry;
    void *argument;

    uint64_t runtime_ticks;
    uint64_t context_switches;
    uint32_t timeslice_ticks;
    uint32_t preempt_count;
    uint8_t need_resched;
    /* Split wait-queue/block transitions are externally observable on SMP;
     * this flag keeps the current CPU owner explicit until dispatch commits. */
    uint8_t scheduler_transition;
    uint8_t priority;
    uint8_t base_priority;
    uint8_t reserved_scheduler;
    uint64_t cpu_affinity;
    uint64_t runnable_age;

    /* Exactly one per-CPU runqueue owns a RUNNABLE task; RUNNING tasks are
     * owned by the current-task slot of their CPU instead. */
    uint32_t runqueue_cpu;
    struct task *run_next;

    /*
     * Active architectural interrupt frame while suspended by preemption.
     * Consumed (set to zero) at the exact moment a dispatch path hands the
     * frame to pop/iretq. See the ownership contract above.
     */
    struct interrupt_frame *interrupt_frame;

    /*
     * Canonical higher-level owner; null for legacy kernel tasks and after
     * the owning thread has exited (the link is never a dangling pointer).
     */
    struct thread *thread;

    struct task *wait_next;
    struct wait_queue *wait_queue;
    struct task *sleep_next;
    uint64_t wake_tick;
    uint8_t sleep_armed;
};

int task_system_init(void);
int task_create(task_entry_t entry, void *argument, uint64_t *task_id);
int task_create_owned(task_entry_t entry, void *argument, struct thread *thread,
                      uint64_t *task_id);
/* Create an owned task in a non-runnable staging state. The caller must
 * publish it only after higher-level ownership links are complete. */
int task_create_owned_staged(task_entry_t entry, void *argument,
                             struct thread *thread, uint64_t *task_id);
int task_publish_staged(uint64_t task_id);
struct task *task_current(void);
struct task *task_lookup(uint64_t task_id);
/* Clears the current task's thread-owner link at the thread lifetime boundary. */
void task_detach_thread(void);
void task_yield(void);
int task_prepare_block(void);
int task_block(void);
/* Block while interrupts are already disabled; restores the supplied flags when resumed. */
int task_block_irqsave(uint64_t flags);
int task_wake(struct task *task);
int task_sleep_until(uint64_t deadline);
int task_sleep_ticks(uint64_t ticks);
void task_exit(void);

/*
 * Called from the common interrupt-exit path.
 *
 * Return value encoding:
 *   bit 0 clear: architectural interrupt_frame pointer; exit with iretq.
 *   bit 0 set:   cooperative saved-stack pointer; restore context and retq.
 *
 * Both forms are required because a runnable task can either have a live
 * hardware interrupt frame or only a cooperative context saved by
 * context_switch_ex().
 */
uint64_t task_reschedule_from_interrupt(struct interrupt_frame *frame);

void task_scheduler_tick(void);
int task_preempt_disable(void);
int task_preempt_enable(void);
int task_set_priority(struct task *task, uint8_t priority);
int task_set_affinity(struct task *task, uint64_t affinity);
uint8_t task_priority(const struct task *task);
uint64_t task_affinity(const struct task *task);
uint32_t task_preempt_count(void);
uint8_t task_need_resched(void);

/* Called by the destination context after a cooperative/frame handoff so
 * zombie stack reclamation cannot race the assembly transition. */
void task_handoff_complete(void);
void task_start_first(void);
/* APs wait here until the BSP publishes the scheduler start gate. */
void task_start_secondary_cpu(void);
void task_publish_scheduler_start(void);
int task_scheduler_ready(void);
uint64_t task_count(void);
/* Deepest observed use of the current task's kernel stack in bytes, measured
 * from the untouched paint pattern (observability for deep I/O paths). */
uint64_t task_stack_high_water(const struct task *task);

/*
 * Deterministic scheduler test/diagnostic hooks.
 * task_debug_validate() panics fatally on any task-table or context
 * ownership invariant violation and returns 0 when the table is sound.
 * task_frame_resume_count() reports how many dispatches resumed a task
 * through a live hardware interrupt frame.
 */
int task_debug_validate(void);
uint64_t task_frame_resume_count(void);
/* CPUs on which a real (non-idle) task has executed since scheduler start. */
uint64_t task_scheduler_task_cpu_mask(void);
/* Quiesce one secondary CPU, drain its runnable queue, and park it. */
int task_cpu_offline(uint32_t cpu_id);
/* Called only by the target CPU's offline IPI path. */
uint64_t task_cpu_offline_from_interrupt(struct interrupt_frame *frame);
/* AP idle/bootstrap loops use this to complete a pending hot-offline. */
int task_cpu_offline_pending(void);
void task_cpu_offline_park(void);

/* Tag on an interrupt_dispatch() frame return (frames are 16-byte aligned):
 * the IRQ path switched tasks and isr.S must call task_handoff_complete()
 * after moving RSP onto the destination frame. Bit 0 tags a cooperative
 * saved-stack return. */
#define ZEROOS_IRQ_RESUME_HANDOFF 2ULL

#endif
