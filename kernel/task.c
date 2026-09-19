#include "task.h"
#include "memory.h"
#include "sync.h"
extern void context_switch(uint64_t *old_sp, uint64_t *new_sp);
extern void serial_write_public(const char *text);

static struct task tasks[ZEROOS_MAX_TASKS];
static struct task *current_task;
static struct spinlock task_lock;
static uint64_t next_task_id;

static void task_trampoline(void) {
    struct task *task=current_task;
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
    for (int i=0;i<ZEROOS_MAX_TASKS;++i)
        if (&tasks[i]==current_task) { start=i; break; }

    for (int step=1;step<=ZEROOS_MAX_TASKS;++step) {
        int index=(start+step)%ZEROOS_MAX_TASKS;
        if (tasks[index].state==TASK_RUNNABLE)
            return index;
    }
    return -1;
}

int task_system_init(void) {
    for (int i=0;i<ZEROOS_MAX_TASKS;++i) {
        tasks[i].id=0; tasks[i].state=TASK_UNUSED; tasks[i].saved_stack=0;
        tasks[i].stack_base=0; tasks[i].entry=0; tasks[i].argument=0;
    }
    spinlock_init(&task_lock);
    current_task=0;
    next_task_id=1;
    return 0;
}

int task_create(task_entry_t entry, void *argument, uint64_t *task_id) {
    if (!entry) return -1;
    uint64_t flags=spin_lock_irqsave(&task_lock);
    int slot=-1;
    for (int i=0;i<ZEROOS_MAX_TASKS;++i)
        if (tasks[i].state==TASK_UNUSED) { slot=i; break; }
    if (slot<0) { spin_unlock_irqrestore(&task_lock,flags); return -1; }

    void *stack=page_alloc();
    if (!stack) { spin_unlock_irqrestore(&task_lock,flags); return -1; }

    struct task *task=&tasks[slot];
    task->id=next_task_id++;
    task->state=TASK_RUNNABLE;
    task->stack_base=(uint64_t)stack;
    task->entry=entry;
    task->argument=argument;
    task_prepare_stack(task);
    if (task_id) *task_id=task->id;
    spin_unlock_irqrestore(&task_lock,flags);
    return 0;
}

struct task *task_current(void) { return current_task; }

void task_yield(void) {
    struct task *previous=current_task;
    int next=find_next_runnable();
    if (!previous || next<0) return;
    previous->state=TASK_RUNNABLE;
    tasks[next].state=TASK_RUNNING;
    current_task=&tasks[next];
    context_switch(&previous->saved_stack,&current_task->saved_stack);
}

void task_exit(void) {
    struct task *previous=current_task;
    int next;
    if (!previous) return;
    previous->state=TASK_ZOMBIE;
    next=find_next_runnable();
    if (next<0) {
        serial_write_public("ZEROOS: all kernel tasks exited; CPU entering idle.\n");
        for (;;) __asm__ volatile ("sti; hlt");
    }
    tasks[next].state=TASK_RUNNING;
    current_task=&tasks[next];
    context_switch(&previous->saved_stack,&current_task->saved_stack);
    for (;;) __asm__ volatile ("cli; hlt");
}

void task_start_first(void) {
    int next=find_next_runnable();
    if (next<0) return;
    tasks[next].state=TASK_RUNNING;
    current_task=&tasks[next];
    static uint64_t bootstrap_stack;
    context_switch(&bootstrap_stack,&current_task->saved_stack);
    for (;;) __asm__ volatile ("sti; hlt");
}

uint64_t task_count(void) {
    uint64_t count=0;
    for (int i=0;i<ZEROOS_MAX_TASKS;++i)
        if (tasks[i].state!=TASK_UNUSED) ++count;
    return count;
}
