#ifndef ZEROOS_TASK_H
#define ZEROOS_TASK_H
#include "types.h"

#define ZEROOS_MAX_TASKS 16
#define ZEROOS_TASK_STACK_SIZE 4096ULL
#define ZEROOS_TASK_STACK_GUARD 0x5a45524f5441534bULL

struct interrupt_frame;
struct process;
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
 * Ring-3 entry frame for user-mode threads. Filled at task creation and
 * consumed exactly once by user_task_entry (kernel/context.S), which
 * iretqs into user mode. Offsets 0-47 are fixed by this struct and are
 * referenced positionally by the assembly entry.
 */
struct user_entry_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
    void *arg;
};

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

    /*
     * Active architectural interrupt frame, valid only while this task is
     * runnable because it was preempted and has not yet resumed. A null value
     * means the task resumes through its cooperative saved_stack context.
     */
    struct interrupt_frame *interrupt_frame;

    struct task *wait_next;
    struct wait_queue *wait_queue;
    struct task *sleep_next;
    uint64_t wake_tick;
    uint8_t sleep_armed;

    /*
     * Non-zero when this task is the main thread of a user-mode process.
     * The process owns the address space; the task owns the CPU context.
     */
    struct process *process;
};

/*
 * The SYSCALL trampoline (kernel/syscall_entry.S) reads these task fields
 * by fixed offset (id 0, state 8, saved_stack 16, stack_base 24). The
 * `state` field is an enum (4 bytes), so saved_stack is padded to an
 * 8-byte boundary. Fail the build if the layout ever drifts from what the
 * assembly assumes.
 */
typedef char task_syscall_entry_layout_check[
    (__builtin_offsetof(struct task, id) == 0U &&
     __builtin_offsetof(struct task, state) == 8U &&
     __builtin_offsetof(struct task, saved_stack) == 16U &&
     __builtin_offsetof(struct task, stack_base) == 24U) ? 1 : -1];

int task_system_init(void);
int task_create(task_entry_t entry, void *argument, uint64_t *task_id);
/* Main thread of a user-mode process; first execution iretqs into ring 3. */
int task_create_user(uint64_t user_rip, uint64_t user_rsp, uint64_t user_arg,
                     uint64_t *task_id);
/* Per-slot ring-3 entry frame; consumed once by the assembly user entry. */
struct user_entry_frame *task_get_user_frame(struct task *task);
struct task *task_current(void);
struct task *task_find_by_id(uint64_t id);
void task_yield(void);
int task_prepare_block(void);
int task_block(void);
/* Block while interrupts are already disabled; restores the supplied flags when resumed. */
int task_block_irqsave(uint64_t flags);
int task_wake(struct task *task);
int task_sleep_until(uint64_t deadline);
int task_sleep_ticks(uint64_t ticks);
void task_exit(void);
int task_discard_new(uint64_t tid);
int task_reap_finished(uint64_t tid);

/*
 * Called from the common interrupt-exit path.
 *
 * Return value encoding:
 *   bit 0 clear: architectural interrupt_frame pointer; exit with iretq.
 *   bit 0 set:   cooperative saved-stack pointer; restore context and retq.
 *
 * Both forms are required because a runnable task can either have a live
 * hardware interrupt frame or only a cooperative context saved by
 * context_switch().
 */
uint64_t task_reschedule_from_interrupt(struct interrupt_frame *frame);

void task_scheduler_tick(void);
int task_preempt_disable(void);
int task_preempt_enable(void);
uint32_t task_preempt_count(void);
uint8_t task_need_resched(void);

void task_start_first(void);
uint64_t task_count(void);
int task_debug_validate(void);

#endif
