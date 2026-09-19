#include "scheduler.h"
#include "task.h"
#include "sync.h"

static struct atomic_u64 scheduler_ticks_count;

int scheduler_init(void) {
    atomic_u64_init(&scheduler_ticks_count,0);
    return 0;
}

void scheduler_tick(void) {
    atomic_u64_fetch_add(&scheduler_ticks_count,1);
    task_scheduler_tick();
}

void scheduler_yield(void) {
    task_yield();
}

int scheduler_sleep_ticks(uint64_t ticks) {
    return task_sleep_ticks(ticks);
}

void scheduler_start(void) {
    task_start_first();
}

uint64_t scheduler_ticks(void) {
    return atomic_u64_load(&scheduler_ticks_count);
}
