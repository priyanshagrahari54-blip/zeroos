#include "task.h"
#include "memory.h"
#include "sync.h"
#include "interrupts.h"
#include "timer.h"

extern void context_switch_ex(uint64_t *old_sp, const uint64_t *new_sp,
                              struct interrupt_frame *new_frame);
extern void task_trampoline(void);
extern void serial_write_public(const char *text);
extern char __kernel_start;
extern char __kernel_end;

static void task_write_u64(uint64_t value);
static void task_debug_dump_all(const char *label);

#define ZEROOS_IDLE_SLOT 1
#define ZEROOS_DEFAULT_TIMESLICE 10U

static struct task tasks[ZEROOS_MAX_TASKS];
static struct task *current_task;

/*
 * Scheduler metadata lock. Lock order (never inverted):
 *   wait_queue::lock  ->  task_lock  ->  memory_lock
 *   process_lock / thread_lock      ->  memory_lock
 * task_lock is always acquired irqsave (or with interrupts already disabled).
 * It is released before any context_switch_ex() handoff: the handoff itself
 * is the atomic ownership transfer. UP atomicity of the switch window is
 * provided by IF=0; SMP runqueue ownership across the handoff is the marked
 * boundary that moves to per-runqueue locks in the SMP stage.
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

static int task_pointer_ok(const struct task *task) {
    uint64_t address;
    uint64_t base;
    uint64_t end;

    if (!task) return 0;
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
static void task_validate_table_at(const char *where,
                                   const struct task *handoff) {
    int running=0;

    if (!task_pointer_ok(current_task) ||
        !task_identity_ok(current_task) ||
        current_task->state!=TASK_RUNNING) {
        task_context_panic("ZEROOS PANIC: current task invariant failed.\n",
                           current_task);
    }

    for (int i=0;i<ZEROOS_MAX_TASKS;++i) {
        struct task *task=&tasks[i];

        if (!task_state_valid(task->state) || !task_identity_ok(task))
            task_context_panic(where,task);

        /*
         * Slot 0 is the bootstrap task. It intentionally has no allocated
         * task stack; scheduler_start parks it while the first real task runs.
         */
        if (i==0) {
            if (task->state==TASK_RUNNING)
                ++running;
            continue;
        }

        if (task->state==TASK_UNUSED)
            continue;

        if (task->priority>ZEROOS_TASK_PRIORITY_MAX ||
            task->base_priority>ZEROOS_TASK_PRIORITY_MAX ||
            task->cpu_affinity==0)
            task_context_panic("ZEROOS PANIC: scheduler policy metadata invalid.\n",
                               task);

        if (!task->stack_base ||
            (task->stack_base & (ZEROOS_PAGE_SIZE-1ULL))!=0 ||
            task->stack_base>=memory_max_physical())
            task_context_panic("ZEROOS PANIC: task stack metadata invalid.\n",
                               task);

        if (!task_stack_guard_ok(task))
            task_context_panic("ZEROOS PANIC: task stack guard corrupted.\n",
                               task);

        /*
         * A RUNNING task owns the CPU context directly and therefore cannot
         * simultaneously own a resumable interrupt frame. A frame pointer
         * retained after the interrupt return consumed the frame is a
         * corrupted ownership state and is fatal here: it is never cleared
         * silently.
         */
        if (task->state==TASK_RUNNING) {
            if (task->interrupt_frame)
                task_context_panic("ZEROOS PANIC: running task retains IRQ frame.\n",
                                   task);
            ++running;
            continue;
        }

        if (task->state==TASK_ZOMBIE) {
            if (task->interrupt_frame || task->wait_queue ||
                task->wait_next || task->sleep_next || task->sleep_armed)
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

        /*
         * The handoff task's saved context is mid-write. Queue membership
         * and identity were validated above; its resumable context becomes
         * visible atomically with the stack switch and is checked when it is
         * later selected as a dispatch target.
         */
        if (task==handoff)
            continue;

        if (task->interrupt_frame) {
            if (!task_frame_ok(task,task->interrupt_frame))
                task_context_panic("ZEROOS PANIC: task IRQ frame invalid.\n",
                                   task);
        } else {
            if (!task_saved_stack_ok(task))
                task_context_panic("ZEROOS PANIC: task saved stack invalid.\n",
                                   task);
            if (!task_saved_context_ok(task))
                task_saved_context_panic(task);
        }
    }

    if (running!=1)
        task_context_panic("ZEROOS PANIC: scheduler running-task count invalid.\n",
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

static void task_irq_restore(uint64_t flags) {
    __asm__ volatile ("pushq %0; popfq"
                      :
                      : "r"(flags)
                      : "memory", "cc");
}

static void task_idle_entry(void *argument) {
    (void)argument;
    for (;;) {
        __asm__ volatile ("sti; hlt" ::: "memory");
        task_yield();
    }
}

void task_trampoline_body(void) {
    struct task *task=current_task;
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
            task->state=TASK_RUNNABLE;
            task->need_resched=1;
        }
    }
}

static void reap_zombies_locked(void) {
    for (int i=2;i<ZEROOS_MAX_TASKS;++i) {
        struct task *task=&tasks[i];
        if (task->state!=TASK_ZOMBIE || !task->stack_base)
            continue;
        page_free((void *)task->stack_base);
        task->id=0;
        task->state=TASK_UNUSED;
        task->saved_stack=0;
        task->stack_base=0;
        task->entry=0;
        task->argument=0;
        task->runtime_ticks=0;
        task->context_switches=0;
        task->timeslice_ticks=ZEROOS_DEFAULT_TIMESLICE;
        task->preempt_count=0;
        task->need_resched=0;
        task->priority=ZEROOS_TASK_PRIORITY_DEFAULT;
        task->base_priority=ZEROOS_TASK_PRIORITY_DEFAULT;
        task->reserved_scheduler=0;
        task->cpu_affinity=ZEROOS_TASK_AFFINITY_ANY;
        task->runnable_age=0;
        task->interrupt_frame=0;
        task->thread=0;
        task->wait_next=0;
        task->wait_queue=0;
        task->sleep_next=0;
        task->wake_tick=0;
        task->sleep_armed=0;
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

static int task_can_run_on_boot_cpu(const struct task *task) {
    return (task->cpu_affinity & 1ULL)!=0;
}

static int find_next_runnable(void) {
    int start=-1;
    int selected=-1;
    uint8_t selected_priority=0;

    for (int i=0;i<ZEROOS_MAX_TASKS;++i)
        if (&tasks[i]==current_task) { start=i; break; }

    /*
     * Bounded priority-aware round robin. Aging promotes a runnable task
     * after bounded wait time, preventing starvation without introducing a
     * second scheduler policy that would later need replacement by SMP
     * runqueues. Equal effective priorities retain deterministic slot order.
     */
    for (int step=1;step<=ZEROOS_MAX_TASKS;++step) {
        int index=(start+step)%ZEROOS_MAX_TASKS;
        struct task *candidate=&tasks[index];
        if (index==0 || index==ZEROOS_IDLE_SLOT ||
            candidate->state!=TASK_RUNNABLE ||
            !task_can_run_on_boot_cpu(candidate))
            continue;
        uint8_t priority=effective_priority(candidate);
        if (selected<0 || priority>selected_priority) {
            selected=index;
            selected_priority=priority;
        }
    }

    if (selected>=0)
        return selected;

    if (tasks[ZEROOS_IDLE_SLOT].state==TASK_RUNNABLE &&
        task_can_run_on_boot_cpu(&tasks[ZEROOS_IDLE_SLOT]))
        return ZEROOS_IDLE_SLOT;

    return -1;
}

/*
 * The cooperative handoff. The caller holds task_lock (interrupts disabled)
 * and has already assigned previous's suspension state
 * (RUNNABLE/BLOCKED/ZOMBIE).
 *
 * Ownership transition performed here:
 *   - previous was RUNNING, so it owns no interrupt frame (fatal otherwise);
 *     its cooperative context is written by context_switch_ex();
 *   - tasks[next]'s resumable context is consumed: a hardware frame pointer
 *     is retired at the exact moment this dispatch takes ownership of the
 *     frame for pop/iretq.
 *
 * task_lock is released before the assembly handoff and is NOT held on
 * return. Returns only when previous is later resumed through its saved
 * cooperative context.
 */
static void dispatch_locked(struct task *previous, int next,
                            const char *where) {
    struct task *target=&tasks[next];
    struct interrupt_frame *target_frame;

    if (previous->interrupt_frame)
        task_context_panic("ZEROOS PANIC: dispatched task retains IRQ frame.\n",
                           previous);

    target_frame=target->interrupt_frame;
    if (target_frame) {
        if (!task_frame_ok(target,target_frame))
            task_context_panic("ZEROOS PANIC: target IRQ frame invalid.\n",
                               target);
        target->interrupt_frame=0;
        ++frame_resume_count;
    } else if (!task_saved_stack_ok(target) ||
               !task_saved_context_ok(target)) {
        task_saved_context_panic(target);
    }

    target->state=TASK_RUNNING;
    target->runnable_age=0;
    ++target->context_switches;
    current_task=target;

    task_validate_table_at(where,previous);
    spin_unlock(&task_lock);

    /*
     * The target context is now exclusively owned by this handoff:
     * target_frame -> iretq, or saved_stack -> cooperative restore + retq.
     */
    context_switch_ex(&previous->saved_stack,
                      &target->saved_stack,
                      target_frame);
}

int task_system_init(void) {
    void *idle_stack;

    for (int i=0;i<ZEROOS_MAX_TASKS;++i) {
        tasks[i].id=0;
        tasks[i].state=TASK_UNUSED;
        tasks[i].saved_stack=0;
        tasks[i].stack_base=0;
        tasks[i].entry=0;
        tasks[i].argument=0;
        tasks[i].runtime_ticks=0;
        tasks[i].context_switches=0;
        tasks[i].timeslice_ticks=ZEROOS_DEFAULT_TIMESLICE;
        tasks[i].preempt_count=0;
        tasks[i].need_resched=0;
        tasks[i].priority=ZEROOS_TASK_PRIORITY_DEFAULT;
        tasks[i].base_priority=ZEROOS_TASK_PRIORITY_DEFAULT;
        tasks[i].reserved_scheduler=0;
        tasks[i].cpu_affinity=ZEROOS_TASK_AFFINITY_ANY;
        tasks[i].runnable_age=0;
        tasks[i].interrupt_frame=0;
        tasks[i].thread=0;
        tasks[i].wait_next=0;
        tasks[i].wait_queue=0;
        tasks[i].sleep_next=0;
        tasks[i].wake_tick=0;
        tasks[i].sleep_armed=0;
    }

    tasks[0].id=0;
    tasks[0].state=TASK_RUNNING;

    idle_stack=page_alloc();
    if (!idle_stack) return -1;

    tasks[ZEROOS_IDLE_SLOT].id=1;
    tasks[ZEROOS_IDLE_SLOT].state=TASK_RUNNABLE;
    tasks[ZEROOS_IDLE_SLOT].stack_base=(uint64_t)idle_stack;
    tasks[ZEROOS_IDLE_SLOT].entry=task_idle_entry;
    tasks[ZEROOS_IDLE_SLOT].timeslice_ticks=ZEROOS_DEFAULT_TIMESLICE;
    task_prepare_stack(&tasks[ZEROOS_IDLE_SLOT]);

    spinlock_init(&task_lock);
    current_task=&tasks[0];
    next_task_id=2;
    sleep_head=0;
    frame_resume_count=0;
    return 0;
}

int task_create(task_entry_t entry, void *argument, uint64_t *task_id) {
    return task_create_owned(entry,argument,0,task_id);
}

int task_create_owned(task_entry_t entry, void *argument,
                      struct thread *thread, uint64_t *task_id) {
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
    task->state=TASK_RUNNABLE;
    task->stack_base=(uint64_t)stack;
    task->entry=entry;
    task->argument=argument;
    task->runtime_ticks=0;
    task->context_switches=0;
    task->timeslice_ticks=ZEROOS_DEFAULT_TIMESLICE;
    task->preempt_count=0;
    task->need_resched=0;
    task->priority=ZEROOS_TASK_PRIORITY_DEFAULT;
    task->base_priority=ZEROOS_TASK_PRIORITY_DEFAULT;
    task->reserved_scheduler=0;
    task->cpu_affinity=ZEROOS_TASK_AFFINITY_ANY;
    task->runnable_age=0;
    task->thread=thread;
    task->wait_next=0;
    task->wait_queue=0;
    task->sleep_next=0;
    task->wake_tick=0;
    task->sleep_armed=0;
    task_prepare_stack(task);

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

static void task_debug_dump_all(const char *label) {
    serial_write_public("ZEROOS DEBUG: ");
    serial_write_public(label);
    serial_write_public("\n");

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
    int next;
    uint64_t flags;

    if (!previous || previous->preempt_count!=0) return;
    flags=task_irq_save();
    spin_lock(&task_lock);

    next=find_next_runnable();
    if (next<0 || &tasks[next]==previous) {
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

    if (!task || task==&tasks[0] || task==&tasks[ZEROOS_IDLE_SLOT] ||
        task->state!=TASK_RUNNING || task->preempt_count!=0)
        return -1;

    flags=spin_lock_irqsave(&task_lock);
    task->state=TASK_BLOCKED;
    task->need_resched=0;
    spin_unlock_irqrestore(&task_lock,flags);
    return 0;
}

static int task_block_locked(uint64_t flags) {
    struct task *previous=current_task;
    int next;

    if (!previous || previous==&tasks[0] ||
        previous==&tasks[ZEROOS_IDLE_SLOT] || previous->preempt_count!=0) {
        task_irq_restore(flags);
        return -1;
    }

    spin_lock(&task_lock);

    /* Already made runnable again before the switch (lost-wakeup guard). */
    if (previous->state==TASK_RUNNABLE) {
        spin_unlock(&task_lock);
        task_irq_restore(flags);
        return 0;
    }

    if (previous->state!=TASK_BLOCKED) {
        spin_unlock(&task_lock);
        task_irq_restore(flags);
        return -1;
    }

    next=find_next_runnable();
    if (next<0) {
        previous->state=TASK_RUNNING;
        spin_unlock(&task_lock);
        task_irq_restore(flags);
        return -1;
    }

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
    if (!task || task==&tasks[0] || task==&tasks[ZEROOS_IDLE_SLOT] ||
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
    spin_unlock_irqrestore(&task_lock,flags);
    return 0;
}

int task_sleep_until(uint64_t deadline) {
    struct task *task=current_task;
    uint64_t flags;
    int next;

    if (!task || task==&tasks[0] || task==&tasks[ZEROOS_IDLE_SLOT] ||
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
    if (next<0) {
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
    int next;

    if (!previous || previous==&tasks[0] ||
        previous==&tasks[ZEROOS_IDLE_SLOT])
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

    if (next<0) {
        tasks[ZEROOS_IDLE_SLOT].state=TASK_RUNNABLE;
        next=ZEROOS_IDLE_SLOT;
    }

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
    int next;

    if (!previous || !frame)
        return (uint64_t)frame;
    if (!task_pointer_ok(previous))
        task_context_panic("ZEROOS PANIC: invalid current task pointer.\n",previous);
    if (!task_identity_ok(previous))
        task_context_panic("ZEROOS PANIC: invalid current task identity.\n",previous);
    if (previous->state!=TASK_RUNNING)
        task_context_panic("ZEROOS PANIC: current task is not running.\n",previous);

    /*
     * Slot 0 is the pre-scheduler bootstrap context and runs on the boot
     * stack, not a task-owned stack page. Interrupts are enabled before
     * scheduler_start(), so a PIT tick can legitimately arrive here. Return
     * the architectural frame unchanged; bootstrap is not preemptible into
     * the task scheduler yet.
     */
    if (previous==&tasks[0])
        return (uint64_t)frame;

    if (!task_stack_guard_ok(previous)) task_stack_guard_panic(previous);
    if (!task_frame_ok(previous,frame)) {
        serial_write_public("ZEROOS PANIC: invalid current IRQ frame.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    /*
     * A preempt-disabled task remains the active CPU owner. The interrupt
     * frame is consumed immediately by this IRQ return and must not be
     * published as a resumable task-owned frame. Publishing it here would
     * leave stale metadata behind and make later scheduler validation confuse
     * the consumed IRQ frame with a suspended context.
     */
    if (previous->preempt_count!=0)
        return (uint64_t)frame;

    spin_lock(&task_lock);

    /*
     * The frame is published as previous's suspension context ONLY when this
     * path actually switches away from previous. On every other path the
     * frame is consumed by this very iretq, and a RUNNING task must never
     * retain a pointer to it. Publishing first and clearing later would
     * create a window in which the ownership state is a lie.
     *
     * Normal tasks enter the IRQ-exit scheduler only after their time slice
     * expires (or another kernel path explicitly requests rescheduling).
     * Idle is the exception: if runnable work exists, leave idle immediately.
     */
    if (previous!=&tasks[ZEROOS_IDLE_SLOT] && !previous->need_resched) {
        spin_unlock(&task_lock);
        return (uint64_t)frame;
    }

    next=find_next_runnable();

    if (next<0 || &tasks[next]==previous) {
        previous->need_resched=0;
        spin_unlock(&task_lock);
        return (uint64_t)frame;
    }

    previous->need_resched=0;
    previous->state=TASK_RUNNABLE;
    /*
     * Exact preemption point retained for every task, idle included: this
     * frame is the task's single resumable context until a dispatch consumes
     * it. Idle is resumed through its real interruption point like any other
     * task; its earlier cooperative context is never resurrected.
     */
    previous->interrupt_frame=frame;

    /*
     * Capture and retire the target's resumable context before publishing
     * the new current_task. A consumed frame pointer is never retained.
     */
    target=&tasks[next];
    target_frame=target->interrupt_frame;
    if (target_frame) {
        if (!task_frame_ok(target,target_frame))
            task_context_panic("ZEROOS PANIC: target IRQ frame invalid.\n",
                               target);
        target->interrupt_frame=0;
        ++frame_resume_count;
    } else if (!task_saved_stack_ok(target) ||
               !task_saved_context_ok(target)) {
        task_saved_context_panic(target);
    }

    target->state=TASK_RUNNING;
    target->runnable_age=0;
    ++target->context_switches;
    current_task=target;
    task_validate_table("ZEROOS PANIC: IRQ dispatch invariant failed.\n");
    spin_unlock(&task_lock);

    /*
     * The target context is now exclusively owned by the IRQ-exit path:
     * target_frame -> iretq, or saved_stack -> cooperative restore + retq.
     */
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
    task_validate_table("ZEROOS PANIC: scheduler table invariant failed.\n");

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
    if (!task_pointer_ok(task) || !(affinity & 1ULL))
        return -1;
    uint64_t flags=spin_lock_irqsave(&task_lock);
    if (task->state==TASK_UNUSED || !task_identity_ok(task)) {
        spin_unlock_irqrestore(&task_lock,flags);
        return -1;
    }
    task->cpu_affinity=affinity;
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

void task_start_first(void) {
    int next;
    uint64_t flags=task_irq_save();

    spin_lock(&task_lock);
    tasks[0].state=TASK_BLOCKED;
    next=find_next_runnable();
    if (next<0) {
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

int task_debug_validate(void) {
    task_validate_table("ZEROOS PANIC: explicit scheduler checkpoint failed.\n");
    return 0;
}

uint64_t task_frame_resume_count(void) {
    return frame_resume_count;
}

uint64_t task_count(void) {
    uint64_t count=0;
    for (int i=0;i<ZEROOS_MAX_TASKS;++i)
        if (tasks[i].state!=TASK_UNUSED)
            ++count;
    return count;
}
