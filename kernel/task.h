#ifndef ZEROOS_TASK_H
#define ZEROOS_TASK_H
#include "types.h"

#define ZEROOS_MAX_TASKS 16
#define ZEROOS_TASK_STACK_SIZE 4096ULL

struct interrupt_frame;
typedef void (*task_entry_t)(void *argument);

enum task_state {
    TASK_UNUSED=0,
    TASK_RUNNABLE,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_ZOMBIE
};

struct wait_queue;

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
};

int task_system_init(void);
int task_create(task_entry_t entry, void *argument, uint64_t *task_id);
struct task *task_current(void);
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

#endif
