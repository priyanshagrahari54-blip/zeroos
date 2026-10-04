#include "scheduler.h"
#include "task.h"
#include "sync.h"
#include "cpu.h"
#include "timer.h"
#include "kstring.h"
#include "types.h"

/*
 * Register state verification: we use callee-saved registers to ensure
 * the context switch is not leaking or corrupting data.
 */
extern void serial_write_public(const char *text);
extern void serial_write_u64(uint64_t value);

static void verify_register_integrity(uint64_t rbx, uint64_t rbp, uint64_t r12, uint64_t r13, uint64_t r14, uint64_t r15, uint32_t task_id) {
    if (rbx != (uint64_t)task_id || rbp != (uint64_t)task_id ||
        r12 != (uint64_t)task_id || r13 != (uint64_t)task_id ||
        r14 != (uint64_t)task_id || r15 != (uint64_t)task_id) {

        serial_write_public("ZEROOS STRESS PANIC: Register corruption detected in task ");
        serial_write_u64(task_id);
        serial_write_public("\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
}

/*
 * Stress task: rapidly yields and verifies its own state.
 */
void stress_task_entry(void *arg) {
    uint32_t task_id=(uint32_t)(uintptr_t)arg;
    uint64_t tid=(uint64_t)task_id;

    // Load callee-saved registers with the Task ID
    __asm__ volatile (
        "movq %0, %%rbx\n\t"
        "movq %0, %%rbp\n\t"
        "movq %0, %%r12\n\t"
        "movq %0, %%r13\n\t"
        "movq %0, %%r14\n\t"
        "movq %0, %%r15\n\t"
        : : "r"(tid) : "rbx", "rbp", "r12", "r13", "r14", "r15"
    );

    for (int i = 0; i < 10000; ++i) {
        uint64_t rbx, rbp, r12, r13, r14, r15;

        __asm__ volatile (
            "movq %%rbx, %0\n\t"
            "movq %%rbp, %1\n\t"
            "movq %%r12, %2\n\t"
            "movq %%r13, %3\n\t"
            "movq %%r14, %4\n\t"
            "movq %%r15, %5\n\t"
            : "=m"(rbx), "=m"(rbp), "=m"(r12), "=m"(r13), "=m"(r14), "=m"(r15)
            : : "rbx", "rbp", "r12", "r13", "r14", "r15"
        );

        verify_register_integrity(rbx, rbp, r12, r13, r14, r15, task_id);

        /* Mix voluntary yield with timed sleep/preemption. */
        if (i % 10 == 0) {
            if (scheduler_sleep_ticks(1)!=0) {
                serial_write_public("ZEROOS STRESS PANIC: timed sleep failed.\n");
                for (;;) __asm__ volatile ("cli; hlt");
            }
        } else {
            scheduler_yield();
        }
    }

    serial_write_public("Stress task completed successfully: ");
    serial_write_u64(tid);
    serial_write_public("\n");
}

/*
 * Launch a batch of stress tasks across all CPUs.
 */
void run_scheduler_stress_test(void) {
    serial_write_public("ZEROOS: Starting Scheduler Stress Test...\n");

    for (uint32_t i = 0; i < 8; ++i) {
        uint64_t tid;
        if (task_create(&stress_task_entry, (void*)(uintptr_t)(i + 100), &tid) != 0) {
            serial_write_public("Failed to create stress task\n");
        }
    }

    serial_write_public("Stress tasks launched. Monitoring stability...\n");
}
