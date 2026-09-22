#ifndef ZEROOS_TASK_H
#define ZEROOS_TASK_H
#include "types.h"

#define ZEROOS_MAX_TASKS 16
#define ZEROOS_TASK_STACK_SIZE 4096ULL
#define ZEROOS_TASK_STACK_GUARD 0x5a45524f5441534bULL
#define ZEROOS_TASK_PRIORITY_MIN 0U
#define ZEROOS_TASK_PRIORITY_DEFAULT 16U
#define ZEROOS_TASK_PRIORITY_MAX 31U
#define ZEROOS_TASK_AFFINITY_ANY (~0ULL)

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
    task_entry_t entry;
    void *argument;

    uint64_t runtime_ticks;
    uint64_t context_switches;
    uint32_t timeslice_ticks;
    uint32_t preempt_count;
    uint8_t need_resched;
    uint8_t priority;
    uint8_t base_priority;
    uint8_t reserved_scheduler;
    uint64_t cpu_affinity;
    uint64_t runnable_age;

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
struct task *task_current(void);
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

void task_start_first(void);
uint64_t task_count(void);

/*
 * Deterministic scheduler test/diagnostic hooks.
 * task_debug_validate() panics fatally on any task-table or context
 * ownership invariant violation and returns 0 when the table is sound.
 * task_frame_resume_count() reports how many dispatches resumed a task
 * through a live hardware interrupt frame.
 */
int task_debug_validate(void);
uint64_t task_frame_resume_count(void);

#endif
