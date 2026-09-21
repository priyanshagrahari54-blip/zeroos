#ifndef ZEROOS_THREAD_H
#define ZEROOS_THREAD_H

#include "types.h"
#include "task.h"

typedef uint64_t thread_id_t;

enum thread_state {
    THREAD_UNUSED=0,
    THREAD_NEW,
    THREAD_RUNNABLE,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_ZOMBIE
};

struct process;

struct thread {
    thread_id_t tid;
    uint32_t generation;
    enum thread_state state;

    struct process *process;
    task_entry_t entry;
    void *argument;

    /* Low-level scheduler context owned by the task layer. */
    uint64_t scheduler_task_id;

    uint64_t exit_status;
    struct thread *next_in_process;
};

int thread_system_init(void);

struct thread *thread_current(void);
struct thread *thread_lookup(thread_id_t tid);

int thread_create_kernel(struct process *process,
                         task_entry_t entry,
                         void *argument,
                         thread_id_t *tid_out);

int thread_exit(uint64_t exit_status);
int thread_reap(struct thread *thread, uint64_t *exit_status_out);

#endif
