#ifndef ZEROOS_TASK_H
#define ZEROOS_TASK_H
#include "types.h"
#define ZEROOS_MAX_TASKS 16
#define ZEROOS_TASK_STACK_SIZE 4096ULL
typedef void (*task_entry_t)(void *argument);
enum task_state { TASK_UNUSED=0, TASK_RUNNABLE, TASK_RUNNING, TASK_BLOCKED, TASK_ZOMBIE };
struct task {
    uint64_t id;
    enum task_state state;
    uint64_t saved_stack;
    uint64_t stack_base;
    task_entry_t entry;
    void *argument;
};
int task_system_init(void);
int task_create(task_entry_t entry, void *argument, uint64_t *task_id);
struct task *task_current(void);
void task_yield(void);
void task_exit(void);
void task_start_first(void);
uint64_t task_count(void);
#endif
