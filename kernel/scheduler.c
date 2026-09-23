#include "scheduler.h"
#include "task.h"
#include "sync.h"
#include "cpu.h"
#include "apic.h"
#include "smp.h"

static struct atomic_u64 scheduler_ticks_count;

int scheduler_init(void) {
    atomic_u64_init(&scheduler_ticks_count,0);
    return 0;
}

void scheduler_tick(void) {
    atomic_u64_fetch_add(&scheduler_ticks_count,1);
    task_scheduler_tick();

    /* PIT/IOAPIC ownership remains on the BSP. APs normally own a calibrated
     * local LAPIC timer; only APs whose local clock event could not be
     * calibrated receive this targeted fallback IPI. Every recipient still
     * performs its own local tick, queue aging, timeout processing, and
     * preemption decision. No AP ever borrows the BSP current-task or
     * runqueue state. */
    if (cpu_current_id()==0 && task_scheduler_ready()) {
        uint32_t discovered=smp_discovered_count();
        for (uint32_t cpu=1; cpu<discovered; ++cpu) {
            const struct cpu_local *local=cpu_local_for_id(cpu);
            const struct smp_cpu_record *record=smp_cpu_record(cpu);
            if (!local || !record ||
                !__atomic_load_n(&local->online,__ATOMIC_ACQUIRE) ||
                local->scheduler_timer_ready)
                continue;
            (void)apic_send_ipi(record->apic_id,ZEROOS_SCHEDULER_TICK_VECTOR);
        }
    }
}

void scheduler_tick_remote(void) {
    if (!task_scheduler_ready())
        return;
    task_scheduler_tick();
}

void scheduler_yield(void) {
    task_yield();
}

int scheduler_sleep_until(uint64_t deadline) {
    return task_sleep_until(deadline);
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
