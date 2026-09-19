#include "task.h"
#include "memory.h"
#include "sync.h"

extern void context_switch(uint64_t *old_sp, uint64_t *new_sp);

#define ZEROOS_IDLE_SLOT 1
#define ZEROOS_DEFAULT_TIMESLICE 10U

static struct task tasks[ZEROOS_MAX_TASKS];
static struct task *current_task;
static struct spinlock task_lock;
static uint64_t next_task_id;

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

static void task_prepare_stack(struct task *task) {
    uint64_t top=task->stack_base+ZEROOS_TASK_STACK_SIZE;
    uint64_t *sp;

    top=(top & ~0xFULL)-8;
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

static int find_next_runnable(void) {
    int start=-1;
    int index;

    for (int i=0;i<ZEROOS_MAX_TASKS;++i)
        if (&tasks[i]==current_task) { start=i; break; }

    for (int step=1;step<=ZEROOS_MAX_TASKS;++step) {
        index=(start+step)%ZEROOS_MAX_TASKS;
        if (index!=0 && index!=ZEROOS_IDLE_SLOT &&
            tasks[index].state==TASK_RUNNABLE)
            return index;
    }

    if (tasks[ZEROOS_IDLE_SLOT].state==TASK_RUNNABLE)
        return ZEROOS_IDLE_SLOT;

    return -1;
}

static int switch_to_next(struct task *previous, int next) {
    if (!previous || next<0 || &tasks[next]==previous) return 0;

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
        tasks[i].wait_next=0;
        tasks[i].wait_queue=0;
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

    if (!previous || previous->preempt_count!=0) return;

    next=find_next_runnable();
    if (next<0 || &tasks[next]==previous) {
        previous->need_resched=0;
        return;
    }

    previous->need_resched=0;
    switch_to_next(previous,next);
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

int task_block(void) {
    struct task *previous=current_task;
    int next;

    if (!previous || previous==&tasks[0] ||
        previous==&tasks[ZEROOS_IDLE_SLOT] || previous->preempt_count!=0)
        return -1;

    if (previous->state==TASK_RUNNABLE)
        return 0;

    if (previous->state!=TASK_BLOCKED)
        return -1;

    next=find_next_runnable();
    if (next<0) {
        previous->state=TASK_RUNNING;
        return -1;
    }

    tasks[next].state=TASK_RUNNING;
    tasks[next].context_switches++;
    current_task=&tasks[next];
    context_switch(&previous->saved_stack,&current_task->saved_stack);
    return 0;
}

int task_wake(struct task *task) {
    if (!task || task==&tasks[0] || task==&tasks[ZEROOS_IDLE_SLOT] ||
        task->state!=TASK_BLOCKED)
        return -1;

    task->state=TASK_RUNNABLE;
    task->need_resched=1;
    return 0;
}

void task_exit(void) {
    struct task *previous=current_task;
    int next;

    if (!previous || previous==&tasks[0] ||
        previous==&tasks[ZEROOS_IDLE_SLOT])
        return;

    previous->state=TASK_ZOMBIE;
    previous->need_resched=0;
    next=find_next_runnable();

    if (next<0) {
        tasks[ZEROOS_IDLE_SLOT].state=TASK_RUNNABLE;
        next=ZEROOS_IDLE_SLOT;
    }

    tasks[next].state=TASK_RUNNING;
    tasks[next].context_switches++;
    current_task=&tasks[next];
    context_switch(&previous->saved_stack,&current_task->saved_stack);

    for (;;) __asm__ volatile ("cli; hlt");
}

void task_scheduler_tick(void) {
    struct task *task=current_task;

    if (!task) return;

    if (task->state==TASK_RUNNING && task!=&tasks[ZEROOS_IDLE_SLOT]) {
        ++task->runtime_ticks;
        if (task->timeslice_ticks==0 ||
            task->runtime_ticks % task->timeslice_ticks==0)
            task->need_resched=1;
    }

    /*
     * Timer IRQ entry has interrupts disabled. The outgoing task's interrupt
     * frame remains on its stack while context_switch saves the scheduler's
     * call context. When the task is selected again, execution returns to
     * this exact IRQ path and the normal ISR epilogue performs iretq.
     */
    if (!task->need_resched || task->preempt_count!=0)
        return;

    int next=find_next_runnable();
    if (next<0 || &tasks[next]==task) {
        task->need_resched=0;
        return;
    }

    task->need_resched=0;
    switch_to_next(task,next);
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

    tasks[0].state=TASK_BLOCKED;
    next=find_next_runnable();
    if (next<0) return;

    tasks[next].state=TASK_RUNNING;
    tasks[next].context_switches++;
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
