#include "scheduler.h"
#include "task.h"
#include "sync.h"
static struct atomic_u64 scheduler_ticks_count;
static uint32_t quantum_ticks;
int scheduler_init(void) {
    atomic_u64_init(&scheduler_ticks_count,0);
    quantum_ticks=0;
    return 0;
}
void scheduler_tick(void) {
    atomic_u64_fetch_add(&scheduler_ticks_count,1);
    if (++quantum_ticks>=10) quantum_ticks=0;
}
void scheduler_yield(void) { task_yield(); }
void scheduler_start(void) { task_start_first(); }
uint64_t scheduler_ticks(void) { return atomic_u64_load(&scheduler_ticks_count); }
