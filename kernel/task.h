#ifndef ZEROOS_TASK_H
#define ZEROOS_TASK_H
#include "types.h"

#define ZEROOS_MAX_TASKS 16
#define ZEROOS_TASK_STACK_SIZE 4096ULL

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

    struct task *wait_next;
    struct wait_queue *wait_queue;
};

int task_system_init(void);
int task_create(task_entry_t entry, void *argument, uint64_t *task_id);
struct task *task_current(void);
void task_yield(void);
int task_prepare_block(void);
int task_block(void);
int task_wake(struct task *task);
void task_exit(void);

void task_scheduler_tick(void);
int task_preempt_disable(void);
int task_preempt_enable(void);
uint32_t task_preempt_count(void);
uint8_t task_need_resched(void);

void task_start_first(void);
uint64_t task_count(void);

#endif
