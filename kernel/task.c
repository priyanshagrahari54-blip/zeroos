#include "task.h"
#include "memory.h"
#include "sync.h"
#include "interrupts.h"
#include "timer.h"

extern void context_switch(uint64_t *old_sp, uint64_t *new_sp);
extern void serial_write_public(const char *text);

static void task_write_u64(uint64_t value);

#define ZEROOS_IDLE_SLOT 1
#define ZEROOS_DEFAULT_TIMESLICE 10U

static struct task tasks[ZEROOS_MAX_TASKS];
static struct task *current_task;
static struct spinlock task_lock;
static uint64_t next_task_id;
static struct task *sleep_head;

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

static int task_pointer_ok(const struct task *task) {
    return task && task>=&tasks[0] && task<&tasks[ZEROOS_MAX_TASKS];
}

static void task_context_panic(const char *message,
                               const struct task *task) {
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

static void task_stack_guard_panic(const struct task *task) {
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

static void task_trampoline(void) {
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
     * context_switch restores six callee-saved registers then retq. The
     * saved stack must be 0 mod 16 so that six 8-byte pops followed by retq
     * leave task_trampoline with RSP 8 mod 16, as required at a SysV ABI
     * function entry.
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
        task->interrupt_frame=0;
        task->wait_next=0;
        task->wait_queue=0;
        task->sleep_next=0;
        task->wake_tick=0;
        task->sleep_armed=0;
    }
}

static int find_next_runnable(int cooperative) {
    int start=-1;
    int index;

    for (int i=0;i<ZEROOS_MAX_TASKS;++i)
        if (&tasks[i]==current_task) { start=i; break; }

    for (int step=1;step<=ZEROOS_MAX_TASKS;++step) {
        index=(start+step)%ZEROOS_MAX_TASKS;
        if (index!=0 && index!=ZEROOS_IDLE_SLOT &&
            tasks[index].state==TASK_RUNNABLE &&
            (!cooperative || tasks[index].interrupt_frame==0))
            return index;
    }

    if (tasks[ZEROOS_IDLE_SLOT].state==TASK_RUNNABLE)
        return ZEROOS_IDLE_SLOT;

    return -1;
}

static int switch_to_next(struct task *previous, int next) {
    if (!previous || next<0 || &tasks[next]==previous) return 0;
    if (!task_stack_guard_ok(previous)) task_stack_guard_panic(previous);
    if (!task_stack_guard_ok(&tasks[next])) task_stack_guard_panic(&tasks[next]);

    /*
     * A cooperative switch resumes the task's saved RET frame, not an old
     * interrupt frame. A frame retained from an earlier preemption becomes
     * stale as soon as that task has resumed and later yields again.
     */
    previous->interrupt_frame=0;

    /* Cooperative selection excludes tasks with a live IRQ frame. */
    if (tasks[next].interrupt_frame!=0)
        return 0;

    previous->state=TASK_RUNNABLE;
    tasks[next].state=TASK_RUNNING;
    tasks[next].context_switches++;
    current_task=&tasks[next];

    context_switch(&previous->saved_stack,&current_task->saved_stack);
    return 1;
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
        tasks[i].interrupt_frame=0;
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
    return 0;
}

int task_create(task_entry_t entry, void *argument, uint64_t *task_id) {
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
    task->wait_next=0;
    task->wait_queue=0;
    task->sleep_next=0;
    task->wake_tick=0;
    task->sleep_armed=0;
    task_prepare_stack(task);

    if (task_id) *task_id=task->id;
    spin_unlock_irqrestore(&task_lock,flags);
    return 0;
}

struct task *task_current(void) {
    return current_task;
}

void task_yield(void) {
    struct task *previous=current_task;
    int next;
    uint64_t flags;

    if (!previous || previous->preempt_count!=0) return;
    flags=task_irq_save();

    next=find_next_runnable(1);
    if (next<0 || &tasks[next]==previous) {
        previous->need_resched=0;
        task_irq_restore(flags);
        return;
    }

    previous->need_resched=0;
    switch_to_next(previous,next);
    task_irq_restore(flags);
}

int task_prepare_block(void) {
    struct task *task=current_task;

    if (!task || task==&tasks[0] || task==&tasks[ZEROOS_IDLE_SLOT] ||
        task->state!=TASK_RUNNING || task->preempt_count!=0)
        return -1;

    task->state=TASK_BLOCKED;
    task->need_resched=0;
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

    if (previous->state==TASK_RUNNABLE) {
        task_irq_restore(flags);
        return 0;
    }

    if (previous->state!=TASK_BLOCKED) {
        task_irq_restore(flags);
        return -1;
    }

    next=find_next_runnable(1);
    if (next<0) {
        previous->state=TASK_RUNNING;
        task_irq_restore(flags);
        return -1;
    }

    tasks[next].state=TASK_RUNNING;
    tasks[next].context_switches++;
    previous->interrupt_frame=0;
    current_task=&tasks[next];
    context_switch(&previous->saved_stack,&current_task->saved_stack);
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

    flags=task_irq_save();
    if (task->sleep_armed)
        sleep_queue_remove_locked(task);
    task->state=TASK_RUNNABLE;
    task->need_resched=1;
    task_irq_restore(flags);
    return 0;
}

int task_sleep_until(uint64_t deadline) {
    struct task *task=current_task;
    uint64_t flags;

    if (!task || task==&tasks[0] || task==&tasks[ZEROOS_IDLE_SLOT] ||
        task->state!=TASK_RUNNING || task->preempt_count!=0)
        return -1;

    if ((long long)(deadline-timer_ticks())<=0)
        return 0;

    flags=task_irq_save();
    if (task->state!=TASK_RUNNING || task->preempt_count!=0) {
        task_irq_restore(flags);
        return -1;
    }

    if ((long long)(deadline-timer_ticks())<=0) {
        task_irq_restore(flags);
        return 0;
    }

    task->wake_tick=deadline;
    task->sleep_armed=1;
    task->need_resched=0;
    task->state=TASK_BLOCKED;
    sleep_queue_insert_locked(task);

    int next=find_next_runnable(1);
    if (next<0) {
        task->state=TASK_RUNNING;
        sleep_queue_remove_locked(task);
        task_irq_restore(flags);
        return -1;
    }

    tasks[next].state=TASK_RUNNING;
    tasks[next].context_switches++;
    task->interrupt_frame=0;
    current_task=&tasks[next];
    context_switch(&task->saved_stack,&current_task->saved_stack);
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
    previous->state=TASK_ZOMBIE;
    previous->need_resched=0;
    next=find_next_runnable(1);

    if (next<0) {
        tasks[ZEROOS_IDLE_SLOT].state=TASK_RUNNABLE;
        next=ZEROOS_IDLE_SLOT;
    }

    tasks[next].state=TASK_RUNNING;
    tasks[next].context_switches++;
    previous->interrupt_frame=0;
    current_task=&tasks[next];
    context_switch(&previous->saved_stack,&current_task->saved_stack);

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
    int next;

    if (!previous || !frame)
        return (uint64_t)frame;
    if (!task_pointer_ok(previous))
        task_context_panic("ZEROOS PANIC: invalid current task pointer.\n",previous);
    if (previous->state!=TASK_RUNNING)
        task_context_panic("ZEROOS PANIC: current task is not running.\n",previous);
    if (!task_stack_guard_ok(previous)) task_stack_guard_panic(previous);
    if (!task_frame_ok(previous,frame)) {
        serial_write_public("ZEROOS PANIC: invalid current IRQ frame.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    previous->interrupt_frame=frame;

    if (previous->preempt_count!=0)
        return (uint64_t)frame;

    /*
     * Normal tasks enter the IRQ-exit scheduler only after their time slice
     * expires (or another kernel path explicitly requests rescheduling).
     * Idle is the exception: if runnable work exists, leave idle immediately.
     */
    if (previous!=&tasks[ZEROOS_IDLE_SLOT] && !previous->need_resched)
        return (uint64_t)frame;

    next=find_next_runnable(0);

    if (next<0 || &tasks[next]==previous) {
        previous->need_resched=0;
        /*
         * The architectural frame is consumed by the iretq epilogue. Once
         * execution returns to the task, RSP points above the frame and the
         * normal downward-growing stack may overwrite it. Do not retain a
         * pointer to a frame that is no longer live.
         */
        previous->interrupt_frame=0;
        return (uint64_t)frame;
    }

    previous->need_resched=0;
    previous->state=TASK_RUNNABLE;
    tasks[next].state=TASK_RUNNING;
    tasks[next].context_switches++;
    current_task=&tasks[next];
    if (!task_pointer_ok(current_task) ||
        current_task->state!=TASK_RUNNING)
        task_context_panic("ZEROOS PANIC: invalid selected task.\n",current_task);
    if (tasks[next].interrupt_frame) {
        serial_write_public("ZEROOS: IRQ switch -> saved frame task ");
        task_write_u64(tasks[next].id);
        serial_write_public(".\n");
    }

    /*
     * A task with an active hardware frame can leave through iretq directly.
     * A task that has since yielded cooperatively has no live interrupt frame;
     * its saved_stack contains the callee-saved context and return address
     * expected by context_switch(). The low-bit tag tells isr_common which
     * exit protocol to use, avoiding any stale-frame reuse.
     */
    if (tasks[next].interrupt_frame) {
        struct interrupt_frame *target_frame=tasks[next].interrupt_frame;
        if (!task_frame_ok(&tasks[next],target_frame)) {
            serial_write_public("ZEROOS PANIC: invalid target IRQ frame.\n");
            for (;;) __asm__ volatile ("cli; hlt");
        }
        /*
         * target_frame is consumed exactly once by iretq. Clear the task's
         * metadata before leaving IRQ context so future cooperative code
         * cannot mistake this dead stack frame for a resumable context.
         */
        tasks[next].interrupt_frame=0;
        return (uint64_t)target_frame;
    }

    if (!task_saved_stack_ok(&tasks[next])) {
        serial_write_public("ZEROOS PANIC: invalid target saved stack.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    return tasks[next].saved_stack | 1ULL;
}

void task_scheduler_tick(void) {
    struct task *task=current_task;
    uint64_t now;

    if (task && (task->state!=TASK_RUNNING || task->id==0 ||
                 task->stack_base==0 || task->saved_stack==0))
        task_stack_guard_panic(task);

    if (!task) return;

    now=timer_ticks();
    if (!task_stack_guard_ok(task)) task_stack_guard_panic(task);
    {
        uint64_t flags=spin_lock_irqsave(&task_lock);
        sleep_queue_wake_expired_locked(now);
        reap_zombies_locked();
        spin_unlock_irqrestore(&task_lock,flags);
    }

    if (task->state==TASK_RUNNING && task!=&tasks[ZEROOS_IDLE_SLOT]) {
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

uint32_t task_preempt_count(void) {
    return current_task ? current_task->preempt_count : 0;
}

uint8_t task_need_resched(void) {
    return current_task ? current_task->need_resched : 0;
}

void task_start_first(void) {
    int next;
    uint64_t flags=task_irq_save();

    tasks[0].state=TASK_BLOCKED;
    next=find_next_runnable(1);
    if (next<0) {
        task_irq_restore(flags);
        return;
    }

    tasks[next].state=TASK_RUNNING;
    tasks[next].context_switches++;
    tasks[0].interrupt_frame=0;
    current_task=&tasks[next];
    context_switch(&tasks[0].saved_stack,&current_task->saved_stack);

    for (;;) __asm__ volatile ("cli; hlt");
}

uint64_t task_count(void) {
    uint64_t count=0;
    for (int i=0;i<ZEROOS_MAX_TASKS;++i)
        if (tasks[i].state!=TASK_UNUSED)
            ++count;
    return count;
}
