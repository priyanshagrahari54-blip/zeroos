#include "task.h"
#include "memory.h"
#include "gdt.h"
#include "sync.h"
#include "interrupts.h"
#include "timer.h"
#include "cpu.h"
#include "smp.h"
#include "tlb.h"
#include "apic.h"

extern void context_switch_ex(uint64_t *old_sp, const uint64_t *new_sp,
                              struct interrupt_frame *new_frame);
extern void task_trampoline(void);
extern void serial_write_public(const char *text);
extern char __kernel_start;
extern char __kernel_end;

static void task_write_u64(uint64_t value);
static void task_debug_dump_all(const char *label);
void task_handoff_complete(void);

#define ZEROOS_IDLE_SLOT 1
#define ZEROOS_DEFAULT_TIMESLICE 10U

static struct task tasks[ZEROOS_MAX_TASKS];
static struct task ap_idle_tasks[ZEROOS_MAX_CPUS];
static struct task *current_tasks[ZEROOS_MAX_CPUS];

/*
 * A cooperative handoff leaves the outgoing task's stack live until the
 * destination context has crossed the assembly boundary. Reclaiming a
 * zombie during that interval can free/reuse the stack while
 * context_switch_ex() is still writing its saved registers. This per-CPU
 * publication is protected by task_lock and cleared only by the destination
 * context (or by the frame handoff assembly hook).
 */
static struct task *handoff_tasks[ZEROOS_MAX_CPUS];

struct task_runqueue {
    struct spinlock lock;
    struct task *head;
    struct task *tail;
    uint32_t length;
};

static struct task_runqueue runqueues[ZEROOS_MAX_CPUS];
static uint8_t cpu_scheduler_started[ZEROOS_MAX_CPUS];
static volatile uint8_t cpu_offline_requested[ZEROOS_MAX_CPUS];
static volatile uint8_t cpu_offline_acknowledged[ZEROOS_MAX_CPUS];
static volatile uint8_t scheduler_ready;
static volatile uint64_t scheduler_task_cpu_mask;

static uint32_t task_cpu_index(void) {
    uint32_t cpu=cpu_current_id();
    return cpu<ZEROOS_MAX_CPUS ? cpu : 0;
}

/* The current-task ownership slot is per CPU; there is no SMP-global
 * current task. All callers are required to use this lvalue. */
#define current_task (current_tasks[task_cpu_index()])

/*
 * Scheduler metadata lock. Lock order (never inverted):
 *   wait_queue::lock  ->  task_lock  ->  runqueue::lock  ->  memory_lock
 *   process_lock / thread_lock      ->  memory_lock
 * task_lock is always acquired irqsave (or with interrupts already disabled).
 * A task is inserted into or removed from a runqueue only while task_lock is
 * held, so a remote wakeup cannot race a local dispatch. The per-CPU queue
 * lock still gives each queue an independent ownership boundary for future
 * lock-free/remote wakeup work and makes queue corruption diagnosable.
 *
 * It is released before any context_switch_ex() handoff: the handoff itself
 * is the atomic ownership transfer on the executing CPU.
 */
static struct spinlock task_lock;
static uint64_t next_task_id;
static struct task *sleep_head;
static uint64_t frame_resume_count;

static int task_stack_guard_ok(const struct task *task) {
    return task && task->stack_base &&
           *(const uint64_t *)(uint64_t)task->stack_base == ZEROOS_TASK_STACK_GUARD;
}

static int task_saved_stack_ok(const struct task *task) {
    uint64_t sp;
    if (!task || !task->stack_base || task->saved_stack==0)
        return 0;
    sp=task->saved_stack;
    return (sp & 7ULL)==0 &&
           sp >= task->stack_base &&
           sp < task->stack_base + ZEROOS_TASK_STACK_SIZE;
}

static int task_frame_ok(const struct task *task,
                         const struct interrupt_frame *frame) {
    uint64_t fp;
    if (!task || !task->stack_base || !frame)
        return 0;
    fp=(uint64_t)frame;
    return (fp & 7ULL)==0 &&
           fp >= task->stack_base &&
           fp + sizeof(*frame) <= task->stack_base + ZEROOS_TASK_STACK_SIZE;
}

static int task_saved_context_ok(const struct task *task) {
    uint64_t rip;
    uint64_t start=(uint64_t)&__kernel_start;
    uint64_t end=(uint64_t)&__kernel_end;

    if (!task_saved_stack_ok(task))
        return 0;

    /* context_switch_ex restores six callee-saved registers then retq. */
    rip=*(const uint64_t *)(task->saved_stack + 48ULL);
    return rip >= start && rip < end;
}

static void task_saved_context_panic(const struct task *task) {
    uint64_t rip=0;
    if (task && task->saved_stack &&
        task->stack_base && task->saved_stack + 48ULL < task->stack_base + ZEROOS_TASK_STACK_SIZE)
        rip=*(const uint64_t *)(task->saved_stack + 48ULL);

    task_debug_dump_all("saved-context invariant dump");
    serial_write_public("ZEROOS PANIC: invalid saved context RIP. task=");
    if (task) task_write_u64(task->id);
    serial_write_public(" stack=");
    if (task) task_write_u64(task->stack_base);
    serial_write_public(" saved=");
    if (task) task_write_u64(task->saved_stack);
    serial_write_public(" rip=");
    task_write_u64(rip);
    serial_write_public("\n");

    for (;;) __asm__ volatile ("cli; hlt");
}

static int task_is_secondary_idle(const struct task *task) {
    uint64_t address;
    uint64_t base;
    uint64_t end;

    if (!task) return 0;
    address=(uint64_t)task;
    base=(uint64_t)&ap_idle_tasks[0];
    end=(uint64_t)&ap_idle_tasks[ZEROOS_MAX_CPUS];
    return address>=base && address<end &&
           ((address-base) % sizeof(ap_idle_tasks[0]))==0;
}

static int task_is_idle(const struct task *task) {
    return task==&tasks[ZEROOS_IDLE_SLOT] || task_is_secondary_idle(task);
}

static struct task *task_idle_for_cpu(uint32_t cpu) {
    return cpu==0 ? &tasks[ZEROOS_IDLE_SLOT] : &ap_idle_tasks[cpu];
}

static uint32_t task_idle_cpu(const struct task *task) {
    if (task==&tasks[ZEROOS_IDLE_SLOT])
        return 0;
    if (task_is_secondary_idle(task)) {
        uint64_t address=(uint64_t)task;
        return (uint32_t)(((uint64_t)address-(uint64_t)&ap_idle_tasks[0])/
                          sizeof(ap_idle_tasks[0]));
    }
    return ZEROOS_MAX_CPUS;
}

static int task_pointer_ok(const struct task *task) {
    uint64_t address;
    uint64_t base;
    uint64_t end;

    if (!task) return 0;
    if (task_is_secondary_idle(task))
        return 1;
    address=(uint64_t)task;
    base=(uint64_t)&tasks[0];
    end=(uint64_t)&tasks[ZEROOS_MAX_TASKS];
    return address>=base && address<end &&
           ((address-base) % sizeof(tasks[0]))==0;
}

static int task_identity_ok(const struct task *task) {
    uint64_t address;
    uint64_t base;
    uint64_t slot;

    if (!task_pointer_ok(task))
        return 0;

    if (task_is_secondary_idle(task))
        return task->id==0 && task->state!=TASK_UNUSED &&
               task_idle_cpu(task)<ZEROOS_MAX_CPUS;

    address=(uint64_t)task;
    base=(uint64_t)&tasks[0];
    slot=(address-base)/sizeof(tasks[0]);

    if (slot==0)
        return task->id==0 && task->state!=TASK_UNUSED;
    if (slot==ZEROOS_IDLE_SLOT)
        return task->id==1 && task->state!=TASK_UNUSED;
    if (task->state==TASK_UNUSED)
        return task->id==0;

    return task->id>=2 && task->id<next_task_id;
}

static int task_state_valid(enum task_state state) {
    return state>=TASK_UNUSED && state<=TASK_ZOMBIE;
}

static void task_context_panic(const char *message,
                               const struct task *task) {
    task_debug_dump_all("task-table invariant dump");
    serial_write_public(message);
    if (task) {
        serial_write_public(" id=");
        task_write_u64(task->id);
        serial_write_public(" state=");
        task_write_u64((uint64_t)task->state);
        serial_write_public(" stack=");
        task_write_u64(task->stack_base);
        serial_write_public(" rsp0=");
        task_write_u64(task->kernel_stack_top);
        serial_write_public(" saved=");
        task_write_u64(task->saved_stack);
        serial_write_public("\n");
    }
    for (;;) __asm__ volatile ("cli; hlt");
}

static void task_write_u64(uint64_t value) {
    char buffer[21];
    int pos=20;
    buffer[pos]='\0';
    if (value==0) { serial_write_public("0"); return; }
    while (value>0 && pos>0) {
        buffer[--pos]=(char)('0'+(value%10));
        value/=10;
    }
    serial_write_public(&buffer[pos]);
}

/*
 * Full task-table and context-ownership validation.
 *
 * `handoff` names a task whose cooperative saved context is being written by
 * an in-flight context_switch_ex() handoff: its state transition is already
 * committed but its saved_stack becomes authoritative only atomically with
 * the stack switch. Every other task must satisfy the complete ownership
 * contract at every observable point.
 */
static int task_current_owner(const struct task *task) {
    for (uint32_t cpu=0; cpu<ZEROOS_MAX_CPUS; ++cpu)
        if (cpu_scheduler_started[cpu] && current_tasks[cpu]==task)
            return 1;
    return 0;
}

static int task_slot_for_pointer(const struct task *task) {
    uint64_t address;
    uint64_t base;

    if (!task || task_is_secondary_idle(task))
        return -1;
    address=(uint64_t)task;
    base=(uint64_t)&tasks[0];
    if (address<base || address>=(uint64_t)&tasks[ZEROOS_MAX_TASKS])
        return -1;
    return (int)((address-base)/sizeof(tasks[0]));
}

static void task_validate_table_at(const char *where,
                                   const struct task *handoff) {
    uint8_t queue_seen[ZEROOS_MAX_TASKS]={0};
    uint32_t running=0;
    uint32_t expected=0;

    for (uint32_t cpu=0; cpu<ZEROOS_MAX_CPUS; ++cpu) {
        struct task *current;
        if (!cpu_scheduler_started[cpu])
            continue;
        ++expected;
        current=current_tasks[cpu];
        if (!task_pointer_ok(current) || !task_identity_ok(current) ||
            ((current->state!=TASK_RUNNING) &&
             !(current->scheduler_transition &&
               (current->state==TASK_BLOCKED ||
                current->state==TASK_RUNNABLE))) ||
            (task_is_idle(current) && task_idle_cpu(current)!=cpu) ||
            !cpu_local_for_id(cpu) ||
            cpu_local_for_id(cpu)->scheduler_current!=current ||
            !cpu_local_for_id(cpu)->scheduler_started)
            task_context_panic("ZEROOS PANIC: per-CPU current task invariant failed.\n",
                               current);
        for (uint32_t other=0; other<cpu; ++other)
            if (current_tasks[other]==current)
                task_context_panic("ZEROOS PANIC: task owned by multiple CPUs.\n",
                                   current);
    }

    for (int i=0;i<ZEROOS_MAX_TASKS;++i) {
        struct task *task=&tasks[i];

        if (!task_state_valid(task->state) || !task_identity_ok(task))
            task_context_panic(where,task);

        /* Slot 0 is the bootstrap context and intentionally has no task
         * stack. It is current only before the BSP hands off to the scheduler. */
        if (i==0) {
            if (!task->kernel_stack_top ||
                (task->kernel_stack_top & 0xfULL)!=0)
                task_context_panic("ZEROOS PANIC: bootstrap kernel stack metadata invalid.\n",
                                   task);
            if (task->state==TASK_RUNNING)
                ++running;
            continue;
        }

        if (task->state==TASK_UNUSED) {
            if (task->run_next || task->runqueue_cpu!=0)
                task_context_panic("ZEROOS PANIC: unused task remains queued.\n",
                                   task);
            continue;
        }

        if (task->priority>ZEROOS_TASK_PRIORITY_MAX ||
            task->base_priority>ZEROOS_TASK_PRIORITY_MAX ||
            task->cpu_affinity==0 ||
            (ZEROOS_MAX_CPUS<64U &&
             (task->cpu_affinity & ~((1ULL<<ZEROOS_MAX_CPUS)-1ULL))!=0))
            task_context_panic("ZEROOS PANIC: scheduler policy metadata invalid.\n",
                               task);

        if (!task->stack_base ||
            (task->stack_base & (ZEROOS_PAGE_SIZE-1ULL))!=0 ||
            task->stack_base>=memory_max_physical() ||
            task->kernel_stack_top!=task->stack_base+ZEROOS_TASK_STACK_SIZE ||
            (task->kernel_stack_top & 0xfULL)!=0)
            task_context_panic("ZEROOS PANIC: task stack metadata invalid.\n",
                               task);

        if (!task_stack_guard_ok(task))
            task_context_panic("ZEROOS PANIC: task stack guard corrupted.\n",
                               task);

        if (task->state==TASK_RUNNING) {
            if (task->interrupt_frame || task->run_next ||
                task->runqueue_cpu>=ZEROOS_MAX_CPUS)
                task_context_panic("ZEROOS PANIC: running task queue/context ownership invalid.\n",
                                   task);
            ++running;
            continue;
        }

        if ((task->state==TASK_BLOCKED || task->state==TASK_RUNNABLE) &&
            task->scheduler_transition && task_current_owner(task)) {
            /* wait_queue_block() publishes BLOCKED before it can call the
             * context-switch path. Interrupts are disabled locally, but a
             * remote CPU may checkpoint the table during this short split
             * transition; the current CPU still owns the task. */
            ++running;
            continue;
        }

        if (task->state==TASK_ZOMBIE) {
            if (task->interrupt_frame || task->wait_queue ||
                task->wait_next || task->sleep_next || task->sleep_armed ||
                task->run_next)
                task_context_panic("ZEROOS PANIC: zombie task retains queue/context state.\n",
                                   task);
            continue;
        }

        if (task->wait_queue && task->sleep_armed)
            task_context_panic("ZEROOS PANIC: task is in wait and sleep queues.\n",
                               task);

        if (task->wait_queue && task->wait_next==task)
            task_context_panic("ZEROOS PANIC: task wait queue self-cycle.\n",
                               task);

        if (task->sleep_armed && task->sleep_next==task)
            task_context_panic("ZEROOS PANIC: task sleep queue self-cycle.\n",
                               task);

        if (task->state==TASK_RUNNABLE && !task_is_idle(task)) {
            int slot=task_slot_for_pointer(task);
            if (slot<0 || task->runqueue_cpu>=ZEROOS_MAX_CPUS)
                task_context_panic("ZEROOS PANIC: runnable task has no runqueue owner.\n",
                                   task);
        }

        /* The handoff task's saved context is mid-write. */
        if (task==handoff)
            continue;

        if (task->interrupt_frame) {
            if (!task_frame_ok(task,task->interrupt_frame))
                task_context_panic("ZEROOS PANIC: task IRQ frame invalid.\n",
                                   task);
        } else if (!task_is_idle(task) || task->saved_stack) {
            if (!task_saved_stack_ok(task))
                task_context_panic("ZEROOS PANIC: task saved stack invalid.\n",
                                   task);
            if (!task_saved_context_ok(task))
                task_saved_context_panic(task);
        }
    }

    for (uint32_t cpu=1; cpu<ZEROOS_MAX_CPUS; ++cpu) {
        struct task *idle=&ap_idle_tasks[cpu];
        if (idle->state==TASK_UNUSED)
            continue;
        if (!task_identity_ok(idle) || !idle->stack_base ||
            idle->kernel_stack_top!=idle->stack_base+ZEROOS_TASK_STACK_SIZE ||
            !task_stack_guard_ok(idle))
            task_context_panic("ZEROOS PANIC: AP idle context metadata invalid.\n",
                               idle);
        if (idle->state==TASK_RUNNING) {
            if (idle->interrupt_frame || idle->run_next ||
                idle->runqueue_cpu!=cpu)
                task_context_panic("ZEROOS PANIC: AP idle ownership invalid.\n",
                                   idle);
            ++running;
        } else if (idle->state==TASK_ZOMBIE || idle->run_next) {
            task_context_panic("ZEROOS PANIC: AP idle queue state invalid.\n",idle);
        } else if (idle!=handoff && idle->interrupt_frame==0 && idle->saved_stack!=0 &&
                   (!task_saved_stack_ok(idle) || !task_saved_context_ok(idle))) {
            task_saved_context_panic(idle);
        }
    }

    for (uint32_t cpu=0; cpu<ZEROOS_MAX_CPUS; ++cpu) {
        struct task_runqueue *queue=&runqueues[cpu];
        uint32_t length=0;
        struct task *previous=0;
        for (struct task *task=queue->head; task; task=task->run_next) {
            int slot=task_slot_for_pointer(task);
            if (slot<2 || slot>=ZEROOS_MAX_TASKS || queue_seen[slot] ||
                task->state!=TASK_RUNNABLE || task->runqueue_cpu!=cpu ||
                task->wait_queue || task->sleep_armed)
                task_context_panic("ZEROOS PANIC: per-CPU runqueue invariant failed.\n",
                                   task);
            if (previous==task)
                task_context_panic("ZEROOS PANIC: per-CPU runqueue self-cycle.\n",
                                   task);
            queue_seen[slot]=1;
            previous=task;
            ++length;
            if (length>ZEROOS_MAX_TASKS)
                task_context_panic("ZEROOS PANIC: per-CPU runqueue cycle detected.\n",
                                   task);
        }
        if (length!=queue->length || (length==0 && queue->tail!=0) ||
            (length!=0 && (!queue->tail || queue->tail->run_next)))
            task_context_panic("ZEROOS PANIC: per-CPU runqueue accounting failed.\n",
                               queue->tail);
    }

    for (int i=2;i<ZEROOS_MAX_TASKS;++i) {
        struct task *task=&tasks[i];
        if (task->state==TASK_RUNNABLE && !queue_seen[i])
            task_context_panic("ZEROOS PANIC: runnable task is absent from its runqueue.\n",
                               task);
        if (task->state!=TASK_RUNNABLE && queue_seen[i])
            task_context_panic("ZEROOS PANIC: non-runnable task is queued.\n",task);
    }

    if (running!=expected)
        task_context_panic("ZEROOS PANIC: per-CPU running-task count invalid.\n",
                           current_task);
}

static void task_validate_table(const char *where) {
    task_validate_table_at(where, 0);
}

static void task_stack_guard_panic(const struct task *task) {
    task_debug_dump_all("stack-guard invariant dump");
    (void)task;
    serial_write_public("ZEROOS PANIC: task stack guard corrupted.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

static uint64_t task_irq_save(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli"
                      : "=r"(flags)
                      :
                      : "memory");
    return flags;
}

static int task_choose_cpu_locked(const struct task *task);
static void runqueue_append_locked(uint32_t cpu, struct task *task);

static void task_irq_restore(uint64_t flags) {
    __asm__ volatile ("pushq %0; popfq"
                      :
                      : "r"(flags)
                      : "memory", "cc");
}

static void task_idle_entry(void *argument) {
    (void)argument;
    for (;;) {
        if (task_cpu_offline_pending())
            task_cpu_offline_park();
        __asm__ volatile ("sti; hlt" ::: "memory");
        task_yield();
    }
}

void task_handoff_complete(void) {
    uint32_t cpu=task_cpu_index();
    uint64_t flags;

    if (cpu>=ZEROOS_MAX_CPUS)
        return;
    flags=spin_lock_irqsave(&task_lock);
    /* The marker is a one-shot publication for the current CPU. The
     * destination is deliberately not compared with the outgoing task: the
     * two pointers must be different for a real handoff. */
    handoff_tasks[cpu]=0;
    spin_unlock_irqrestore(&task_lock,flags);
}

void task_trampoline_body(void) {
    struct task *task=current_task;
    task_handoff_complete();
    __atomic_fetch_or(&scheduler_task_cpu_mask,1ULL<<task_cpu_index(),
                      __ATOMIC_ACQ_REL);
    __asm__ volatile ("sti" ::: "memory");
    task->entry(task->argument);
    task_exit();
    for (;;) __asm__ volatile ("cli; hlt");
}

/*
 * New tasks begin with an ABI-valid cooperative context. A hardware interrupt
 * frame is created only by an actual interrupt and is owned exclusively by
 * the IRQ-exit path until that task resumes.
 */
static void task_prepare_stack(struct task *task) {
    uint64_t top=task->stack_base+ZEROOS_TASK_STACK_SIZE;
    uint64_t *sp;

    *(uint64_t *)(uint64_t)task->stack_base=ZEROOS_TASK_STACK_GUARD;

    /*
     * context_switch_ex restores six callee-saved registers then retq. The
     * saved stack must be 0 mod 16 so that six 8-byte pops followed by retq
     * leave the assembly task_trampoline wrapper with RSP 8 mod 16. The
     * wrapper then reserves interrupt headroom before calling the C body.
     */
    top=(top & ~0xFULL)-8ULL;
    sp=(uint64_t *)top;
    *--sp=(uint64_t)task_trampoline;
    *--sp=0;
    *--sp=0;
    *--sp=0;
    *--sp=0;
    *--sp=0;
    *--sp=0;
    task->saved_stack=(uint64_t)sp;
}

static int deadline_before(uint64_t a, uint64_t b) {
    return (long long)(a-b)<0;
}

static void sleep_queue_insert_locked(struct task *task) {
    struct task **cursor=&sleep_head;
    while (*cursor && !deadline_before(task->wake_tick,(*cursor)->wake_tick))
        cursor=&(*cursor)->sleep_next;
    task->sleep_next=*cursor;
    *cursor=task;
}

static void sleep_queue_remove_locked(struct task *task) {
    struct task **cursor=&sleep_head;
    while (*cursor && *cursor!=task)
        cursor=&(*cursor)->sleep_next;
    if (*cursor==task) {
        *cursor=task->sleep_next;
        task->sleep_next=0;
    }
    task->wake_tick=0;
    task->sleep_armed=0;
}

static void sleep_queue_wake_expired_locked(uint64_t now) {
    while (sleep_head && (long long)(now-sleep_head->wake_tick)>=0) {
        struct task *task=sleep_head;
        sleep_head=task->sleep_next;
        task->sleep_next=0;
        task->wake_tick=0;
        task->sleep_armed=0;
        if (task->state==TASK_BLOCKED) {
            int owner;
            task->state=TASK_RUNNABLE;
            task->need_resched=1;
            owner=task_choose_cpu_locked(task);
            if (owner<0)
                task_context_panic("ZEROOS PANIC: timed wake lost CPU affinity.\n",task);
            runqueue_append_locked((uint32_t)owner,task);
        }
    }
}

static void reap_zombies_locked(void) {
    for (int i=2;i<ZEROOS_MAX_TASKS;++i) {
        struct task *task=&tasks[i];
        if (task->state!=TASK_ZOMBIE || !task->stack_base)
            continue;
        /* A zombie is not reclaimable until the CPU that dispatched away
         * from it has completed the architectural handoff. In particular,
         * this protects the outgoing cooperative stack from page_free()
         * racing context_switch_ex() on another CPU. */
        for (uint32_t cpu=0; cpu<ZEROOS_MAX_CPUS; ++cpu) {
            if (handoff_tasks[cpu]==task)
                goto defer_zombie_reclaim;
        }
        if (task_current_owner(task))
            task_context_panic("ZEROOS PANIC: zombie task still owns a CPU.\n",task);
        page_free((void *)task->stack_base);
        task->id=0;
        task->state=TASK_UNUSED;
        task->saved_stack=0;
        task->stack_base=0;
        task->kernel_stack_top=0;
        task->entry=0;
        task->argument=0;
        task->runtime_ticks=0;
        task->context_switches=0;
        task->timeslice_ticks=ZEROOS_DEFAULT_TIMESLICE;
        task->preempt_count=0;
        task->need_resched=0;
        task->scheduler_transition=0;
        task->priority=ZEROOS_TASK_PRIORITY_DEFAULT;
        task->base_priority=ZEROOS_TASK_PRIORITY_DEFAULT;
        task->reserved_scheduler=0;
        task->cpu_affinity=ZEROOS_TASK_AFFINITY_ANY;
        task->runnable_age=0;
        task->runqueue_cpu=0;
        task->run_next=0;
        task->interrupt_frame=0;
        task->thread=0;
        task->wait_next=0;
        task->wait_queue=0;
        task->sleep_next=0;
        task->wake_tick=0;
        task->sleep_armed=0;
        continue;

    defer_zombie_reclaim:
        /* The next scheduler tick retries after the destination context has
         * published completion. */
        continue;
    }
}

/*
 * Single successor selection shared by every scheduler entry point. Any
 * TASK_RUNNABLE task is a legal target regardless of its resumable context
 * form (hardware interrupt frame or cooperative saved stack), so a
 * frame-suspended task is never skipped by cooperative dispatch and a
 * blocking task can always find a successor.
 */
static uint8_t effective_priority(const struct task *task) {
    uint64_t aging=task->runnable_age/4ULL;
    uint64_t value=(uint64_t)task->priority+aging;
    if (value>ZEROOS_TASK_PRIORITY_MAX)
        value=ZEROOS_TASK_PRIORITY_MAX;
    return (uint8_t)value;
}

static uint64_t task_valid_cpu_mask(void) {
    return ZEROOS_MAX_CPUS>=64U ? ~0ULL : ((1ULL<<ZEROOS_MAX_CPUS)-1ULL);
}

static int task_can_run_on_cpu(const struct task *task, uint32_t cpu) {
    return task && cpu<ZEROOS_MAX_CPUS &&
           (task->cpu_affinity & (1ULL<<cpu))!=0 &&
           !__atomic_load_n(&cpu_offline_requested[cpu],__ATOMIC_ACQUIRE) &&
           cpu_local_for_id(cpu) &&
           __atomic_load_n(&cpu_local_for_id(cpu)->online,__ATOMIC_ACQUIRE);
}

static void runqueue_append_locked(uint32_t cpu, struct task *task) {
    struct task_runqueue *queue=&runqueues[cpu];

    if (task_is_idle(task) || task->state!=TASK_RUNNABLE || task->run_next ||
        task->runqueue_cpu>=ZEROOS_MAX_CPUS)
        task_context_panic("ZEROOS PANIC: invalid runqueue insertion.\n",task);
    spin_lock(&queue->lock);
    task->runqueue_cpu=cpu;
    task->run_next=0;
    if (queue->tail)
        queue->tail->run_next=task;
    else
        queue->head=task;
    queue->tail=task;
    ++queue->length;
    spin_unlock(&queue->lock);
}

static void runqueue_recompute_tail_locked(struct task_runqueue *queue);

static void runqueue_remove_locked(struct task *task) {
    struct task_runqueue *queue;
    struct task **cursor;

    if (!task || task_is_idle(task) || task->runqueue_cpu>=ZEROOS_MAX_CPUS)
        return;
    queue=&runqueues[task->runqueue_cpu];
    spin_lock(&queue->lock);
    cursor=&queue->head;
    while (*cursor && *cursor!=task)
        cursor=&(*cursor)->run_next;
    if (*cursor==task) {
        *cursor=task->run_next;
        if (queue->tail==task)
            runqueue_recompute_tail_locked(queue);
        if (queue->length)
            --queue->length;
        task->run_next=0;
        task->runqueue_cpu=0;
    }
    spin_unlock(&queue->lock);
}

/* GCC statement expressions are deliberately avoided in the public kernel
 * style; this helper is used by the removal path to keep tail accounting
 * explicit and bounded. */
static void runqueue_recompute_tail_locked(struct task_runqueue *queue) {
    struct task *tail=queue->head;
    if (!tail) {
        queue->tail=0;
        return;
    }
    while (tail->run_next)
        tail=tail->run_next;
    queue->tail=tail;
}

static int task_choose_cpu_locked(const struct task *task) {
    uint32_t selected=ZEROOS_MAX_CPUS;
    uint32_t selected_length=~0U;

    for (uint32_t cpu=0; cpu<ZEROOS_MAX_CPUS; ++cpu) {
        if (!task_can_run_on_cpu(task,cpu))
            continue;
        if (runqueues[cpu].length<selected_length) {
            selected=cpu;
            selected_length=runqueues[cpu].length;
        }
    }
    return selected<ZEROOS_MAX_CPUS ? (int)selected : -1;
}

static int task_has_online_alternative_locked(const struct task *task,
                                               uint32_t offline_cpu) {
    if (!task || task_is_idle(task))
        return 1;
    for (uint32_t cpu=0; cpu<ZEROOS_MAX_CPUS; ++cpu) {
        if (cpu==offline_cpu)
            continue;
        if (task_can_run_on_cpu(task,cpu))
            return 1;
    }
    return 0;
}

/* Drain a queue while task_lock is held. Queue locks are never held while
 * selecting the destination, preserving task_lock -> runqueue_lock order. */
static int runqueue_evacuate_locked(uint32_t offline_cpu) {
    struct task_runqueue *queue=&runqueues[offline_cpu];

    for (;;) {
        struct task *task;
        int owner;

        spin_lock(&queue->lock);
        task=queue->head;
        if (task) {
            queue->head=task->run_next;
            if (queue->length)
                --queue->length;
            task->run_next=0;
            task->runqueue_cpu=0;
            if (!queue->head)
                queue->tail=0;
        }
        spin_unlock(&queue->lock);
        if (!task)
            return 0;

        owner=task_choose_cpu_locked(task);
        if (owner<0)
            task_context_panic("ZEROOS PANIC: CPU-offline queue evacuation lost affinity.\n",
                               task);
        runqueue_append_locked((uint32_t)owner,task);
    }
}

static struct task *runqueue_pick_locked(uint32_t cpu) {
    struct task_runqueue *queue=&runqueues[cpu];
    struct task *selected=0;
    uint8_t selected_priority=0;

    spin_lock(&queue->lock);
    for (struct task *candidate=queue->head; candidate;
         candidate=candidate->run_next) {
        if (candidate->state!=TASK_RUNNABLE ||
            !task_can_run_on_cpu(candidate,cpu))
            continue;
        uint8_t priority=effective_priority(candidate);
        if (!selected || priority>selected_priority) {
            selected=candidate;
            selected_priority=priority;
        }
    }
    spin_unlock(&queue->lock);
    return selected;
}

/* A local queue is preferred; an idle CPU may steal a compatible runnable
 * task from another queue while task_lock serializes the ownership transfer. */
static struct task *find_next_runnable(void) {
    uint32_t cpu=task_cpu_index();
    struct task *selected=runqueue_pick_locked(cpu);

    if (!selected) {
        for (uint32_t other=0; other<ZEROOS_MAX_CPUS; ++other) {
            if (other==cpu || !cpu_local_for_id(other) ||
                !__atomic_load_n(&cpu_local_for_id(other)->online,
                                 __ATOMIC_ACQUIRE))
                continue;
            selected=runqueue_pick_locked(other);
            if (selected)
                break;
        }
    }

    if (selected)
        return selected;
    return task_idle_for_cpu(cpu);
}

/*
 * The cooperative handoff. The caller holds task_lock (interrupts disabled)
 * and has already assigned previous's suspension state
 * (RUNNABLE/BLOCKED/ZOMBIE).
 *
 * Ownership transition performed here:
 *   - previous was RUNNING, so it owns no interrupt frame (fatal otherwise);
 *     its cooperative context is written by context_switch_ex();
 *   - a RUNNABLE previous is returned to exactly one per-CPU runqueue;
 *   - target is removed from its owning queue (or is this CPU's idle context)
 *     and becomes the sole current-task owner of this CPU;
 *   - a target interrupt frame is retired at the exact moment this dispatch
 *     takes ownership of the frame for pop/iretq.
 *
 * task_lock is released before the assembly handoff and is NOT held on
 * return. Returns only when previous is later resumed through its saved
 * cooperative context.
 */
static void dispatch_locked(struct task *previous, struct task *target,
                            const char *where) {
    struct interrupt_frame *target_frame;
    uint32_t cpu=task_cpu_index();

    if (!previous || !target || previous->interrupt_frame)
        task_context_panic("ZEROOS PANIC: dispatched task retains IRQ frame.\n",
                           previous);
    if (target==previous || (task_is_idle(target) && task_idle_cpu(target)!=cpu))
        task_context_panic("ZEROOS PANIC: cross-CPU dispatch target invalid.\n",
                           target);

    if (previous->state==TASK_RUNNABLE && !task_is_idle(previous)) {
        int owner=task_choose_cpu_locked(previous);
        if (owner<0)
            task_context_panic("ZEROOS PANIC: runnable task lost CPU affinity.\n",
                               previous);
        runqueue_append_locked((uint32_t)owner,previous);
    } else if (previous->state!=TASK_RUNNING && !task_is_idle(previous)) {
        previous->run_next=0;
        previous->runqueue_cpu=0;
    }

    if (!task_is_idle(target)) {
        runqueue_remove_locked(target);
        if (target->state!=TASK_RUNNABLE)
            task_context_panic("ZEROOS PANIC: dispatch target is not runnable.\n",
                               target);
    } else if (target->state!=TASK_RUNNABLE && target->state!=TASK_RUNNING) {
        task_context_panic("ZEROOS PANIC: idle dispatch target state invalid.\n",
                           target);
    }

    target_frame=target->interrupt_frame;
    if (target_frame) {
        if (!task_frame_ok(target,target_frame))
            task_context_panic("ZEROOS PANIC: target IRQ frame invalid.\n",
                               target);
        target->interrupt_frame=0;
        ++frame_resume_count;
    } else if (target->saved_stack) {
        if (!task_saved_stack_ok(target) || !task_saved_context_ok(target))
            task_saved_context_panic(target);
    } else if (!task_is_idle(target)) {
        task_saved_context_panic(target);
    }

    target->state=TASK_RUNNING;
    target->runqueue_cpu=cpu;
    target->run_next=0;
    target->runnable_age=0;
    ++target->context_switches;
    current_task=target;
    if (cpu_local_for_id(cpu)) {
        cpu_local_for_id(cpu)->scheduler_current=target;
        ++cpu_local_for_id(cpu)->scheduler_epoch;
    }

    /* The target's scheduler stack is also its protected privilege-entry
     * stack. Publish RSP0 before the target can execute or receive an IRQ. */
    if (gdt_set_kernel_stack(target->kernel_stack_top)!=0)
        task_context_panic("ZEROOS PANIC: target kernel stack publication failed.\n",
                           target);

    if (handoff_tasks[cpu])
        task_context_panic("ZEROOS PANIC: scheduler handoff already pending.\n",
                           handoff_tasks[cpu]);
    handoff_tasks[cpu]=previous;
    task_validate_table_at(where,previous);
    spin_unlock(&task_lock);

    context_switch_ex(&previous->saved_stack,
                      &target->saved_stack,
                      target_frame);

    /* A cooperative destination resumes at this call site. The frame
     * destination clears the same marker in context.S before iretq. */
    task_handoff_complete();
}

int task_system_init(void) {
    void *idle_stack;
    uint32_t online=cpu_online_count();

    if (online==0 || online>ZEROOS_MAX_CPUS)
        return -1;

    for (int i=0;i<ZEROOS_MAX_TASKS;++i) {
        tasks[i].id=0;
        tasks[i].state=TASK_UNUSED;
        tasks[i].saved_stack=0;
        tasks[i].stack_base=0;
        tasks[i].kernel_stack_top=0;
        tasks[i].entry=0;
        tasks[i].argument=0;
        tasks[i].runtime_ticks=0;
        tasks[i].context_switches=0;
        tasks[i].timeslice_ticks=ZEROOS_DEFAULT_TIMESLICE;
        tasks[i].preempt_count=0;
        tasks[i].need_resched=0;
        tasks[i].scheduler_transition=0;
        tasks[i].priority=ZEROOS_TASK_PRIORITY_DEFAULT;
        tasks[i].base_priority=ZEROOS_TASK_PRIORITY_DEFAULT;
        tasks[i].reserved_scheduler=0;
        tasks[i].cpu_affinity=ZEROOS_TASK_AFFINITY_ANY & task_valid_cpu_mask();
        tasks[i].runnable_age=0;
        tasks[i].runqueue_cpu=0;
        tasks[i].run_next=0;
        tasks[i].interrupt_frame=0;
        tasks[i].thread=0;
        tasks[i].wait_next=0;
        tasks[i].wait_queue=0;
        tasks[i].sleep_next=0;
        tasks[i].wake_tick=0;
        tasks[i].sleep_armed=0;
    }
    for (uint32_t cpu=0; cpu<ZEROOS_MAX_CPUS; ++cpu) {
        runqueues[cpu].head=0;
        runqueues[cpu].tail=0;
        runqueues[cpu].length=0;
        spinlock_init(&runqueues[cpu].lock);
        current_tasks[cpu]=0;
        handoff_tasks[cpu]=0;
        cpu_scheduler_started[cpu]=0;
        cpu_offline_requested[cpu]=0;
        cpu_offline_acknowledged[cpu]=0;
        if (cpu_local_for_id(cpu)) {
            cpu_local_for_id(cpu)->scheduler_current=0;
            cpu_local_for_id(cpu)->scheduler_started=0;
            cpu_local_for_id(cpu)->scheduler_epoch=0;
        }
        ap_idle_tasks[cpu]=(struct task){0};
    }

    tasks[0].id=0;
    tasks[0].state=TASK_RUNNING;
    tasks[0].kernel_stack_top=gdt_kernel_stack();
    tasks[0].runqueue_cpu=0;
    if (tasks[0].kernel_stack_top==0 ||
        (tasks[0].kernel_stack_top & 0xfULL)!=0)
        return -1;

    idle_stack=page_alloc();
    if (!idle_stack) return -1;

    tasks[ZEROOS_IDLE_SLOT].id=1;
    tasks[ZEROOS_IDLE_SLOT].state=TASK_RUNNABLE;
    tasks[ZEROOS_IDLE_SLOT].stack_base=(uint64_t)idle_stack;
    tasks[ZEROOS_IDLE_SLOT].kernel_stack_top=(uint64_t)idle_stack+
                                              ZEROOS_TASK_STACK_SIZE;
    tasks[ZEROOS_IDLE_SLOT].entry=task_idle_entry;
    tasks[ZEROOS_IDLE_SLOT].timeslice_ticks=ZEROOS_DEFAULT_TIMESLICE;
    tasks[ZEROOS_IDLE_SLOT].cpu_affinity=1ULL;
    tasks[ZEROOS_IDLE_SLOT].runqueue_cpu=0;
    task_prepare_stack(&tasks[ZEROOS_IDLE_SLOT]);

    for (uint32_t cpu=1; cpu<online; ++cpu) {
        uint64_t stack=smp_bootstrap_stack(cpu);
        struct task *idle=&ap_idle_tasks[cpu];
        if (!stack || (stack & (ZEROOS_PAGE_SIZE-1ULL))!=0)
            return -1;
        idle->id=0;
        idle->state=TASK_RUNNABLE;
        idle->stack_base=stack;
        idle->kernel_stack_top=stack+ZEROOS_TASK_STACK_SIZE;
        idle->timeslice_ticks=ZEROOS_DEFAULT_TIMESLICE;
        idle->cpu_affinity=1ULL<<cpu;
        idle->runqueue_cpu=cpu;
        *(uint64_t *)(uint64_t)stack=ZEROOS_TASK_STACK_GUARD;
    }

    spinlock_init(&task_lock);
    current_tasks[0]=&tasks[0];
    cpu_scheduler_started[0]=1;
    if (cpu_local_for_id(0)) {
        cpu_local_for_id(0)->scheduler_current=&tasks[0];
        cpu_local_for_id(0)->scheduler_started=1;
    }
    scheduler_ready=0;
    next_task_id=2;
    sleep_head=0;
    frame_resume_count=0;
    scheduler_task_cpu_mask=0;
    return 0;
}

int task_create(task_entry_t entry, void *argument, uint64_t *task_id) {
    return task_create_owned(entry,argument,0,task_id);
}

static int task_create_owned_internal(task_entry_t entry, void *argument,
                                       struct thread *thread,
                                       uint64_t *task_id,
                                       int publish) {
    if (!entry) return -1;

    uint64_t flags=spin_lock_irqsave(&task_lock);
    int slot=-1;

    for (int i=2;i<ZEROOS_MAX_TASKS;++i) {
        if (tasks[i].state==TASK_UNUSED) { slot=i; break; }
    }

    if (slot<0) {
        spin_unlock_irqrestore(&task_lock,flags);
        return -1;
    }

    void *stack=page_alloc();
    if (!stack) {
        spin_unlock_irqrestore(&task_lock,flags);
        return -1;
    }

    struct task *task=&tasks[slot];
    task->id=next_task_id++;
    /* Staged owned tasks are blocked but have no wait-queue membership. They
     * cannot execute until the caller has published every higher-level link. */
    task->state=publish ? TASK_RUNNABLE : TASK_BLOCKED;
    task->stack_base=(uint64_t)stack;
    task->kernel_stack_top=(uint64_t)stack+ZEROOS_TASK_STACK_SIZE;
    task->entry=entry;
    task->argument=argument;
    task->runtime_ticks=0;
    task->context_switches=0;
    task->timeslice_ticks=ZEROOS_DEFAULT_TIMESLICE;
    task->preempt_count=0;
    task->need_resched=0;
    task->scheduler_transition=0;
    task->priority=ZEROOS_TASK_PRIORITY_DEFAULT;
    task->base_priority=ZEROOS_TASK_PRIORITY_DEFAULT;
    task->reserved_scheduler=0;
    task->cpu_affinity=ZEROOS_TASK_AFFINITY_ANY & task_valid_cpu_mask();
    task->runnable_age=0;
    task->runqueue_cpu=0;
    task->run_next=0;
    task->thread=thread;
    task->wait_next=0;
    task->wait_queue=0;
    task->sleep_next=0;
    task->wake_tick=0;
    task->sleep_armed=0;
    task_prepare_stack(task);

    if (publish) {
        int owner=task_choose_cpu_locked(task);
        if (owner<0) {
            page_free(stack);
            task->id=0;
            task->state=TASK_UNUSED;
            spin_unlock_irqrestore(&task_lock,flags);
            return -1;
        }
        runqueue_append_locked((uint32_t)owner,task);
    }

    if (!task_saved_context_ok(task))
        task_saved_context_panic(task);

    /*
     * Validate the whole task table after every creation while interrupts are
     * disabled. This makes a metadata/context overwrite attributable to the
     * creation boundary instead of a much later scheduler failure.
     */
    task_validate_table("ZEROOS PANIC: task creation invariant failed.\n");

    if (task_id) *task_id=task->id;
    spin_unlock_irqrestore(&task_lock,flags);
    return 0;
}

int task_create_owned(task_entry_t entry, void *argument,
                      struct thread *thread, uint64_t *task_id) {
    return task_create_owned_internal(entry,argument,thread,task_id,1);
}

int task_create_owned_staged(task_entry_t entry, void *argument,
                             struct thread *thread, uint64_t *task_id) {
    return task_create_owned_internal(entry,argument,thread,task_id,0);
}

int task_publish_staged(uint64_t task_id) {
    uint64_t flags;
    struct task *task=0;
    int owner;

    if (!task_id) return -1;
    flags=spin_lock_irqsave(&task_lock);
    for (int i=2;i<ZEROOS_MAX_TASKS;++i) {
        if (tasks[i].id==task_id) {
            task=&tasks[i];
            break;
        }
    }
    if (!task || task->state!=TASK_BLOCKED || task->run_next ||
        task->runqueue_cpu!=0 || task->wait_queue || task->sleep_armed) {
        spin_unlock_irqrestore(&task_lock,flags);
        return -1;
    }

    owner=task_choose_cpu_locked(task);
    if (owner<0) {
        spin_unlock_irqrestore(&task_lock,flags);
        return -1;
    }
    task->state=TASK_RUNNABLE;
    runqueue_append_locked((uint32_t)owner,task);
    task_validate_table("ZEROOS PANIC: staged task publication invariant failed.\n");
    spin_unlock_irqrestore(&task_lock,flags);
    return 0;
}

static void task_debug_dump_all(const char *label) {
    serial_write_public("ZEROOS DEBUG: ");
    serial_write_public(label);
    serial_write_public(" cpu=");
    task_write_u64(task_cpu_index());
    serial_write_public("\n");
    for (uint32_t cpu=0; cpu<ZEROOS_MAX_CPUS; ++cpu) {
        struct task *current=current_tasks[cpu];
        if (!cpu_scheduler_started[cpu] && !current)
            continue;
        serial_write_public("  cpu=");
        task_write_u64(cpu);
        serial_write_public(" started=");
        task_write_u64(cpu_scheduler_started[cpu]);
        serial_write_public(" current=");
        task_write_u64(current ? current->id : 0);
        serial_write_public(" state=");
        task_write_u64(current ? (uint64_t)current->state : 0);
        serial_write_public(" rq=");
        task_write_u64(runqueues[cpu].length);
        serial_write_public(" epoch=");
        task_write_u64(cpu_local_for_id(cpu) ?
                       cpu_local_for_id(cpu)->scheduler_epoch : 0);
        serial_write_public("\n");
    }

    for (int i=0;i<ZEROOS_MAX_TASKS;++i) {
        struct task *task=&tasks[i];
        if (task->state==TASK_UNUSED)
            continue;

        serial_write_public("  slot=");
        task_write_u64((uint64_t)i);
        serial_write_public(" id=");
        task_write_u64(task->id);
        serial_write_public(" state=");
        task_write_u64((uint64_t)task->state);
        serial_write_public(" stack=");
        task_write_u64(task->stack_base);
        serial_write_public(" saved=");
        task_write_u64(task->saved_stack);

        if (task->saved_stack &&
            task->stack_base &&
            task->saved_stack + 48ULL < task->stack_base + ZEROOS_TASK_STACK_SIZE) {
            serial_write_public(" rip=");
            task_write_u64(*(const uint64_t *)(task->saved_stack + 48ULL));
        }
        serial_write_public("\n");
    }
}

struct task *task_current(void) {
    return current_task;
}

void task_detach_thread(void) {
    uint64_t flags=spin_lock_irqsave(&task_lock);
    if (current_task)
        current_task->thread=0;
    spin_unlock_irqrestore(&task_lock,flags);
}

void task_yield(void) {
    struct task *previous=current_task;
    struct task *next;
    uint64_t flags;

    if (!previous || previous->preempt_count!=0) return;
    flags=task_irq_save();
    spin_lock(&task_lock);

    next=find_next_runnable();
    if (!next || next==previous) {
        previous->need_resched=0;
        spin_unlock(&task_lock);
        task_irq_restore(flags);
        return;
    }

    previous->need_resched=0;
    previous->state=TASK_RUNNABLE;
    dispatch_locked(previous,next,
                    "ZEROOS PANIC: task yield invariant failed.\n");
    task_irq_restore(flags);
}

int task_prepare_block(void) {
    struct task *task=current_task;
    uint64_t flags;

    if (!task || task==&tasks[0] || task_is_idle(task) ||
        task->state!=TASK_RUNNING || task->preempt_count!=0)
        return -1;

    flags=spin_lock_irqsave(&task_lock);
    task->scheduler_transition=1;
    task->state=TASK_BLOCKED;
    task->need_resched=0;
    spin_unlock_irqrestore(&task_lock,flags);
    return 0;
}

static int task_block_locked(uint64_t flags) {
    struct task *previous=current_task;
    struct task *next;

    if (!previous || previous==&tasks[0] || task_is_idle(previous) ||
        previous->preempt_count!=0) {
        task_irq_restore(flags);
        return -1;
    }

    spin_lock(&task_lock);

    /* Already made runnable again before the switch (lost-wakeup guard).
     * The remote wake may have inserted the still-current task into a
     * runqueue; remove it before restoring RUNNING ownership. */
    if (previous->state==TASK_RUNNABLE) {
        runqueue_remove_locked(previous);
        previous->state=TASK_RUNNING;
        previous->scheduler_transition=0;
        spin_unlock(&task_lock);
        task_irq_restore(flags);
        return 0;
    }

    if (previous->state!=TASK_BLOCKED) {
        previous->scheduler_transition=0;
        spin_unlock(&task_lock);
        task_irq_restore(flags);
        return -1;
    }

    next=find_next_runnable();
    if (!next) {
        previous->state=TASK_RUNNING;
        previous->scheduler_transition=0;
        spin_unlock(&task_lock);
        task_irq_restore(flags);
        return -1;
    }

    previous->scheduler_transition=0;
    dispatch_locked(previous,next,
                    "ZEROOS PANIC: task block invariant failed.\n");
    task_irq_restore(flags);
    return 0;
}

int task_block(void) {
    uint64_t flags=task_irq_save();
    return task_block_locked(flags);
}

int task_block_irqsave(uint64_t flags) {
    return task_block_locked(flags);
}

int task_wake(struct task *task) {
    uint64_t flags;
    if (!task || task==&tasks[0] || task_is_idle(task) ||
        task->state!=TASK_BLOCKED)
        return -1;

    flags=spin_lock_irqsave(&task_lock);
    if (task->state!=TASK_BLOCKED) {
        spin_unlock_irqrestore(&task_lock,flags);
        return -1;
    }
    if (task->sleep_armed)
        sleep_queue_remove_locked(task);
    task->state=TASK_RUNNABLE;
    task->need_resched=1;
    {
        int owner=task_choose_cpu_locked(task);
        if (owner<0) {
            task->state=TASK_BLOCKED;
            spin_unlock_irqrestore(&task_lock,flags);
            return -1;
        }
        runqueue_append_locked((uint32_t)owner,task);
    }
    spin_unlock_irqrestore(&task_lock,flags);
    return 0;
}

int task_sleep_until(uint64_t deadline) {
    struct task *task=current_task;
    uint64_t flags;
    struct task *next;

    if (!task || task==&tasks[0] || task_is_idle(task) ||
        task->state!=TASK_RUNNING || task->preempt_count!=0)
        return -1;

    if ((long long)(deadline-timer_ticks())<=0)
        return 0;

    flags=task_irq_save();
    spin_lock(&task_lock);

    if (task->state!=TASK_RUNNING || task->preempt_count!=0) {
        spin_unlock(&task_lock);
        task_irq_restore(flags);
        return -1;
    }

    if ((long long)(deadline-timer_ticks())<=0) {
        spin_unlock(&task_lock);
        task_irq_restore(flags);
        return 0;
    }

    task->wake_tick=deadline;
    task->sleep_armed=1;
    task->need_resched=0;
    task->state=TASK_BLOCKED;
    sleep_queue_insert_locked(task);

    next=find_next_runnable();
    if (!next) {
        task->state=TASK_RUNNING;
        sleep_queue_remove_locked(task);
        spin_unlock(&task_lock);
        task_irq_restore(flags);
        return -1;
    }

    dispatch_locked(task,next,
                    "ZEROOS PANIC: task sleep invariant failed.\n");
    task_irq_restore(flags);
    return 0;
}

int task_sleep_ticks(uint64_t ticks) {
    if (ticks==0)
        return 0;
    return task_sleep_until(timer_ticks()+ticks);
}

void task_exit(void) {
    struct task *previous=current_task;
    struct task *next;

    if (!previous || previous==&tasks[0] || task_is_idle(previous))
        return;

    (void)task_irq_save();
    spin_lock(&task_lock);

    previous->state=TASK_ZOMBIE;
    previous->need_resched=0;
    /*
     * No clearing of previous->interrupt_frame here: a RUNNING task owns no
     * frame (dispatch_locked() panics if one exists), and silently erasing a
     * stale pointer would mask an ownership violation.
     */
    next=find_next_runnable();
    if (!next)
        next=task_idle_for_cpu(task_cpu_index());

    dispatch_locked(previous,next,
                    "ZEROOS PANIC: task exit invariant failed.\n");

    /* Unreachable: a zombie is never selected as a dispatch target. */
    for (;;) __asm__ volatile ("cli; hlt");
}

/*
 * This is the only timer-driven context-switch path. It runs while the CPU
 * is still on the interrupt stack, so the old task's complete architectural
 * state remains intact. The assembly epilogue later loads the returned frame
 * as its new iret source.
 */
uint64_t task_reschedule_from_interrupt(struct interrupt_frame *frame) {
    struct task *previous=current_task;
    struct task *target;
    struct interrupt_frame *target_frame;
    uint32_t cpu=task_cpu_index();
    int offlining=cpu_offline_requested[cpu] &&
                  cpu_local_for_id(cpu) &&
                  !__atomic_load_n(&cpu_local_for_id(cpu)->online,
                                   __ATOMIC_ACQUIRE);

    if (!previous || !frame)
        return (uint64_t)frame;
    if (!task_pointer_ok(previous))
        task_context_panic("ZEROOS PANIC: invalid current task pointer.\n",previous);
    if (!task_identity_ok(previous))
        task_context_panic("ZEROOS PANIC: invalid current task identity.\n",previous);
    if (previous->state!=TASK_RUNNING)
        task_context_panic("ZEROOS PANIC: current task is not running.\n",previous);

    /* Bootstrap is not a scheduler participant. */
    if (previous==&tasks[0])
        return (uint64_t)frame;

    if (!task_stack_guard_ok(previous))
        task_stack_guard_panic(previous);
    if (!task_frame_ok(previous,frame)) {
        serial_write_public("ZEROOS PANIC: invalid current IRQ frame.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    if (previous->preempt_count!=0)
        return (uint64_t)frame;

    spin_lock(&task_lock);

    if (!task_is_idle(previous) && !previous->need_resched) {
        spin_unlock(&task_lock);
        return (uint64_t)frame;
    }

    /* Select while previous is still RUNNING so it cannot be selected from
     * its own queue. Only after a different target is known is the live IRQ
     * frame published as previous's resumable context. */
    target=offlining ? task_idle_for_cpu(cpu) : find_next_runnable();
    if (!target || target==previous) {
        previous->need_resched=0;
        spin_unlock(&task_lock);
        return (uint64_t)frame;
    }

    previous->need_resched=0;
    previous->state=TASK_RUNNABLE;
    previous->interrupt_frame=frame;
    if (!task_is_idle(previous)) {
        int owner=task_choose_cpu_locked(previous);
        if (owner<0)
            task_context_panic("ZEROOS PANIC: preempted task lost CPU affinity.\n",
                               previous);
        runqueue_append_locked((uint32_t)owner,previous);
    }

    if (!task_is_idle(target)) {
        runqueue_remove_locked(target);
        if (target->state!=TASK_RUNNABLE)
            task_context_panic("ZEROOS PANIC: IRQ target is not runnable.\n",target);
    }

    target_frame=target->interrupt_frame;
    if (target_frame) {
        if (!task_frame_ok(target,target_frame))
            task_context_panic("ZEROOS PANIC: target IRQ frame invalid.\n",
                               target);
        target->interrupt_frame=0;
        ++frame_resume_count;
    } else if (target->saved_stack) {
        if (!task_saved_stack_ok(target) || !task_saved_context_ok(target))
            task_saved_context_panic(target);
    } else if (!task_is_idle(target)) {
        task_saved_context_panic(target);
    }

    target->state=TASK_RUNNING;
    target->runqueue_cpu=task_cpu_index();
    target->run_next=0;
    target->runnable_age=0;
    ++target->context_switches;
    current_task=target;
    if (cpu_local_for_id(task_cpu_index())) {
        cpu_local_for_id(task_cpu_index())->scheduler_current=target;
        ++cpu_local_for_id(task_cpu_index())->scheduler_epoch;
    }
    if (gdt_set_kernel_stack(target->kernel_stack_top)!=0)
        task_context_panic("ZEROOS PANIC: IRQ target kernel stack publication failed.\n",
                           target);
    task_validate_table_at("ZEROOS PANIC: IRQ dispatch invariant failed.\n",previous);
    spin_unlock(&task_lock);

    if (target_frame)
        return (uint64_t)target_frame;
    return target->saved_stack | 1ULL;
}

void task_scheduler_tick(void) {
    struct task *task=current_task;
    uint64_t now;

    if (task && task->state!=TASK_RUNNING)
        task_context_panic("ZEROOS PANIC: timer observed non-running current task.\n",
                           task);

    if (!task) return;

    /*
     * Slot 0 is the boot-time bootstrap context. It has no task stack page
     * and therefore no task stack guard or saved cooperative context to
     * validate. The scheduler tick still services global timeout/reclamation
     * state, but bootstrap itself is never time-slice preempted.
     */
    if (task!=&tasks[0] && !task_stack_guard_ok(task))
        task_stack_guard_panic(task);

    now=timer_ticks();
    {
        uint64_t flags=spin_lock_irqsave(&task_lock);
        sleep_queue_wake_expired_locked(now);
        for (int i=2;i<ZEROOS_MAX_TASKS;++i) {
            struct task *candidate=&tasks[i];
            if (candidate->state==TASK_RUNNABLE) {
                if (candidate->runnable_age!=~0ULL)
                    ++candidate->runnable_age;
            } else if (candidate->state==TASK_RUNNING) {
                candidate->runnable_age=0;
            }
        }
        reap_zombies_locked();
        spin_unlock_irqrestore(&task_lock,flags);
    }

    /*
     * The running task's interrupt frame is owned by the in-flight interrupt
     * path and is never recorded in the task table while the task is
     * RUNNING. Any frame pointer observed here is corrupted ownership state
     * and is fatal: it is never "retired" silently.
     */
    {
        uint64_t validate_flags=spin_lock_irqsave(&task_lock);
        task_validate_table("ZEROOS PANIC: scheduler table invariant failed.\n");
        spin_unlock_irqrestore(&task_lock,validate_flags);
    }

    if (task==&tasks[0])
        return;

    if (task!=&tasks[ZEROOS_IDLE_SLOT]) {
        ++task->runtime_ticks;
        if (task->timeslice_ticks==0 ||
            task->runtime_ticks % task->timeslice_ticks==0)
            task->need_resched=1;
    }
}

int task_preempt_disable(void) {
    struct task *task=current_task;
    if (!task || task->preempt_count==0xffffffffU) return -1;
    ++task->preempt_count;
    return 0;
}

int task_preempt_enable(void) {
    struct task *task=current_task;
    if (!task || task->preempt_count==0) return -1;
    --task->preempt_count;
    if (task->preempt_count==0 && task->need_resched)
        task_yield();
    return 0;
}

int task_set_priority(struct task *task, uint8_t priority) {
    if (!task_pointer_ok(task) || priority>ZEROOS_TASK_PRIORITY_MAX)
        return -1;
    uint64_t flags=spin_lock_irqsave(&task_lock);
    if (task->state==TASK_UNUSED || !task_identity_ok(task)) {
        spin_unlock_irqrestore(&task_lock,flags);
        return -1;
    }
    task->priority=priority;
    task->base_priority=priority;
    task->runnable_age=0;
    spin_unlock_irqrestore(&task_lock,flags);
    return 0;
}

int task_set_affinity(struct task *task, uint64_t affinity) {
    uint64_t flags;
    int online_target=0;
    if (!task_pointer_ok(task) || affinity==0 ||
        (affinity & ~task_valid_cpu_mask())!=0)
        return -1;
    for (uint32_t cpu=0; cpu<ZEROOS_MAX_CPUS; ++cpu)
        if ((affinity&(1ULL<<cpu)) && cpu_local_for_id(cpu) &&
            __atomic_load_n(&cpu_local_for_id(cpu)->online,__ATOMIC_ACQUIRE))
            online_target=1;
    if (!online_target)
        return -1;
    if (task==&tasks[0])
        return affinity==1ULL ? 0 : -1;
    if (task_is_idle(task))
        return -1;
    flags=spin_lock_irqsave(&task_lock);
    if (task->state==TASK_UNUSED || !task_identity_ok(task)) {
        spin_unlock_irqrestore(&task_lock,flags);
        return -1;
    }
    if (task->state==TASK_RUNNABLE)
        runqueue_remove_locked(task);
    task->cpu_affinity=affinity;
    if (task->state==TASK_RUNNABLE) {
        int owner=task_choose_cpu_locked(task);
        if (owner<0) {
            task->cpu_affinity=ZEROOS_TASK_AFFINITY_ANY & task_valid_cpu_mask();
            owner=task_choose_cpu_locked(task);
            if (owner<0) {
                spin_unlock_irqrestore(&task_lock,flags);
                return -1;
            }
        }
        runqueue_append_locked((uint32_t)owner,task);
    } else if (task->state==TASK_RUNNING &&
               !(task->cpu_affinity & (1ULL<<task_cpu_index()))) {
        task->need_resched=1;
    }
    spin_unlock_irqrestore(&task_lock,flags);
    return 0;
}

uint8_t task_priority(const struct task *task) {
    return task && task_pointer_ok(task) ? task->priority : 0;
}

uint64_t task_affinity(const struct task *task) {
    return task && task_pointer_ok(task) ? task->cpu_affinity : 0;
}

uint32_t task_preempt_count(void) {
    return current_task ? current_task->preempt_count : 0;
}

uint8_t task_need_resched(void) {
    return current_task ? current_task->need_resched : 0;
}

int task_cpu_offline_pending(void) {
    uint32_t cpu=task_cpu_index();
    return cpu>0 && cpu<ZEROOS_MAX_CPUS &&
           __atomic_load_n(&cpu_offline_requested[cpu],__ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&cpu_offline_acknowledged[cpu],__ATOMIC_ACQUIRE);
}

void task_cpu_offline_park(void) {
    uint32_t cpu=task_cpu_index();
    struct task *idle;

    if (cpu==0 || cpu>=ZEROOS_MAX_CPUS)
        return;
    idle=task_idle_for_cpu(cpu);
    if (!__atomic_load_n(&cpu_offline_requested[cpu],__ATOMIC_ACQUIRE) ||
        current_task!=idle)
        return;

    __atomic_store_n(&cpu_offline_acknowledged[cpu],1,__ATOMIC_RELEASE);
    serial_write_public("ZEROOS: AP scheduler CPU quiesced and parked (cpu=");
    task_write_u64(cpu);
    serial_write_public(").\\n");
    for (;;) {
        __asm__ volatile ("cli; hlt" : : : "memory");
    }
}

uint64_t task_cpu_offline_from_interrupt(struct interrupt_frame *frame) {
    uint32_t cpu=task_cpu_index();
    struct task *current=current_task;

    if (!frame || cpu==0 || cpu>=ZEROOS_MAX_CPUS ||
        !__atomic_load_n(&cpu_offline_requested[cpu],__ATOMIC_ACQUIRE))
        return (uint64_t)frame;
    /* A preemption-disabled critical section must finish before the CPU can
     * transfer its current task to another runqueue. The request remains
     * published and a later interrupt retries it. */
    if (!current || current->preempt_count!=0)
        return (uint64_t)frame;
    if (tlb_unregister_current_cpu(cpu)!=0)
        return (uint64_t)frame;
    if (cpu_mark_offline(cpu)!=0 || smp_mark_cpu_offline(cpu)!=0) {
        serial_write_public("ZEROOS PANIC: AP CPU-offline ownership transition failed.\\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    current->need_resched=1;
    return task_reschedule_from_interrupt(frame);
}

int task_cpu_offline(uint32_t cpu_id) {
    uint64_t flags;
    const struct smp_cpu_record *record;
    uint64_t start;
    uint64_t last_signal=0;

    if (cpu_current_id()!=0 || cpu_id==0 || cpu_id>=ZEROOS_MAX_CPUS ||
        smp_online_count()<=1 || !task_scheduler_ready() ||
        !cpu_local_for_id(cpu_id) ||
        !__atomic_load_n(&cpu_local_for_id(cpu_id)->online,__ATOMIC_ACQUIRE))
        return -1;

    flags=spin_lock_irqsave(&task_lock);
    if (cpu_offline_requested[cpu_id] || !cpu_scheduler_started[cpu_id]) {
        spin_unlock_irqrestore(&task_lock,flags);
        return -2;
    }
    /* Reject a transition that would strand any live schedulable task. */
    for (int i=2; i<ZEROOS_MAX_TASKS; ++i) {
        struct task *task=&tasks[i];
        if (task->state!=TASK_UNUSED && task->state!=TASK_ZOMBIE &&
            !task_has_online_alternative_locked(task,cpu_id)) {
            spin_unlock_irqrestore(&task_lock,flags);
            return -3;
        }
    }

    __atomic_store_n(&cpu_offline_requested[cpu_id],1,__ATOMIC_RELEASE);
    __atomic_store_n(&cpu_offline_acknowledged[cpu_id],0,__ATOMIC_RELEASE);
    if (runqueue_evacuate_locked(cpu_id)!=0) {
        __atomic_store_n(&cpu_offline_requested[cpu_id],0,__ATOMIC_RELEASE);
        spin_unlock_irqrestore(&task_lock,flags);
        return -4;
    }
    spin_unlock_irqrestore(&task_lock,flags);

    record=smp_cpu_record(cpu_id);
    if (!record || apic_send_ipi(record->apic_id,
                                 ZEROOS_SCHEDULER_OFFLINE_VECTOR)!=0) {
        __atomic_store_n(&cpu_offline_requested[cpu_id],0,__ATOMIC_RELEASE);
        return -5;
    }

    start=timer_ticks();
    for (;;) {
        if (__atomic_load_n(&cpu_offline_acknowledged[cpu_id],
                            __ATOMIC_ACQUIRE))
            break;
        if (timer_ticks()-start>100ULL) {
            serial_write_public("ZEROOS: AP CPU-offline acknowledgement timed out.\\n");
            /* Keep the request published: cancelling a transition after the
             * AP has removed its TLB ownership would be unsafe. */
            return -6;
        }
        if (timer_ticks()-last_signal>=10ULL) {
            (void)apic_send_ipi(record->apic_id,
                                ZEROOS_SCHEDULER_OFFLINE_VECTOR);
            last_signal=timer_ticks();
        }
        cpu_relax();
    }

    if (__atomic_load_n(&cpu_local_for_id(cpu_id)->online,__ATOMIC_ACQUIRE) ||
        tlb_cpu_is_online(cpu_id) || smp_online_count()>=2)
        return -7;
    return 0;
}

void task_start_first(void) {
    struct task *next;
    uint64_t flags=task_irq_save();

    spin_lock(&task_lock);
    tasks[0].state=TASK_BLOCKED;
    next=find_next_runnable();
    if (!next) {
        tasks[0].state=TASK_RUNNING;
        spin_unlock(&task_lock);
        task_irq_restore(flags);
        return;
    }

    dispatch_locked(&tasks[0],next,
                    "ZEROOS PANIC: scheduler start invariant failed.\n");

    /* Unreachable: nothing ever dispatches back to the bootstrap slot. */
    for (;;) __asm__ volatile ("cli; hlt");
}

void task_start_secondary_cpu(void) {
    uint32_t cpu=task_cpu_index();
    struct task *idle;

    if (cpu==0 || cpu>=ZEROOS_MAX_CPUS)
        return;

    while (!task_scheduler_ready())
        __asm__ volatile ("sti; hlt" : : : "memory");

    idle=&ap_idle_tasks[cpu];
    {
        uint64_t flags=task_irq_save();
        struct task *next;
        spin_lock(&task_lock);
        if (idle->state!=TASK_RUNNABLE && idle->state!=TASK_RUNNING) {
            spin_unlock(&task_lock);
            task_irq_restore(flags);
            return;
        }
        current_tasks[cpu]=idle;
        cpu_scheduler_started[cpu]=1;
        if (cpu_local_for_id(cpu)) {
            cpu_local_for_id(cpu)->scheduler_current=idle;
            cpu_local_for_id(cpu)->scheduler_started=1;
        }
        idle->state=TASK_RUNNING;
        idle->runqueue_cpu=cpu;
        if (gdt_set_kernel_stack(idle->kernel_stack_top)!=0)
            task_context_panic("ZEROOS PANIC: AP idle stack publication failed.\n",idle);
        next=find_next_runnable();
        if (next==idle) {
            task_validate_table("ZEROOS PANIC: AP scheduler start invariant failed.\n");
            spin_unlock(&task_lock);
            task_irq_restore(flags);
        } else {
            idle->state=TASK_RUNNABLE;
            dispatch_locked(idle,next,
                            "ZEROOS PANIC: AP scheduler start invariant failed.\n");
            task_irq_restore(flags);
        }
    }

    /* A cooperative handoff returns here when this CPU's idle context is
     * selected again. The idle context never enters a shared task stack. */
    for (;;) {
        if (task_cpu_offline_pending())
            task_cpu_offline_park();
        __asm__ volatile ("sti; hlt" : : : "memory");
        task_yield();
    }
}

void task_publish_scheduler_start(void) {
    __atomic_store_n(&scheduler_ready,1,__ATOMIC_RELEASE);
    for (uint32_t cpu=1; cpu<ZEROOS_MAX_CPUS; ++cpu) {
        const struct cpu_local *local=cpu_local_for_id(cpu);
        const struct smp_cpu_record *record=smp_cpu_record(cpu);
        if (!local || !record ||
            !__atomic_load_n(&local->online,__ATOMIC_ACQUIRE))
            continue;
        (void)apic_send_ipi(record->apic_id,ZEROOS_SCHEDULER_WAKE_VECTOR);
    }
}

int task_scheduler_ready(void) {
    return __atomic_load_n(&scheduler_ready,__ATOMIC_ACQUIRE)!=0;
}

int task_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&task_lock);
    task_validate_table("ZEROOS PANIC: explicit scheduler checkpoint failed.\n");
    spin_unlock_irqrestore(&task_lock,flags);
    return 0;
}

uint64_t task_frame_resume_count(void) {
    return frame_resume_count;
}

uint64_t task_scheduler_task_cpu_mask(void) {
    return __atomic_load_n(&scheduler_task_cpu_mask,__ATOMIC_ACQUIRE);
}

uint64_t task_count(void) {
    uint64_t count=0;
    for (int i=0;i<ZEROOS_MAX_TASKS;++i)
        if (tasks[i].state!=TASK_UNUSED)
            ++count;
    return count;
}
