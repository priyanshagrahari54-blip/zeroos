#include "task.h"
#include "memory.h"
#include "sync.h"
#include "interrupts.h"
#include "timer.h"
#include "gdt.h"
#include "process.h"

extern void context_switch(uint64_t *old_sp, uint64_t *new_sp);
extern void task_trampoline(void);
extern void user_task_entry(void);
extern void serial_write_public(const char *text);
extern char __kernel_start;
extern char __kernel_end;

/* Per-slot ring-3 entry frames; consumed once by user_task_entry. */
static struct user_entry_frame user_frames[ZEROOS_MAX_TASKS];

static void task_write_u64(uint64_t value);

#define ZEROOS_IDLE_SLOT 1
#define ZEROOS_DEFAULT_TIMESLICE 10U

static struct task tasks[ZEROOS_MAX_TASKS];
/*
 * Exported (non-static) so the SYSCALL entry trampoline can read the current
 * task pointer with a plain RIP-relative load: a call would push a return
 * address onto the user stack, which must not be touched before the entry
 * has validated it and moved to the kernel stack.
 */
struct task *current_task;
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

static int task_saved_context_ok(const struct task *task) {
    uint64_t rip;
    uint64_t start=(uint64_t)&__kernel_start;
    uint64_t end=(uint64_t)&__kernel_end;

    if (!task_saved_stack_ok(task))
        return 0;

    /* context_switch restores six callee-saved registers then retq. */
    rip=*(const uint64_t *)(task->saved_stack + 48ULL);
    return rip >= start && rip < end;
}

static void task_saved_context_panic(const struct task *task) {
    uint64_t rip=0;
    if (task && task->saved_stack &&
        task->stack_base &&
        task->saved_stack + 48ULL < task->stack_base + ZEROOS_TASK_STACK_SIZE)
        rip=*(const uint64_t *)(task->saved_stack + 48ULL);

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

static void task_validate_table(const char *where) {
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

        if (!task->stack_base ||
            (task->stack_base & (ZEROOS_PAGE_SIZE-1ULL))!=0 ||
            task->stack_base>=memory_max_physical())
            task_context_panic("ZEROOS PANIC: task stack metadata invalid.\n",
                               task);

        if (!task_stack_guard_ok(task))
            task_context_panic("ZEROOS PANIC: task stack guard corrupted.\n",
                               task);

        /*
         * saved_stack is a suspended cooperative context, not a permanent
         * snapshot of a task's stack. Once a task resumes, normal execution
         * may overwrite the old frame, so a RUNNING task's saved_stack must
         * never be validated as if it were live context.
         *
         * A RUNNING task owns the CPU context directly and therefore cannot
         * simultaneously own a live interrupt frame. Suspended runnable/
         * blocked tasks must have either a valid interrupt frame or a valid
         * cooperative saved context. ZOMBIE tasks have no resumable context.
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

static void task_stack_guard_panic(const struct task *task) {
    (void)task;
    serial_write_public("ZEROOS PANIC: task stack guard corrupted.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

struct user_entry_frame *task_get_user_frame(struct task *task) {
    if (!task) return 0;
    uint64_t slot = ((uint64_t)task - (uint64_t)&tasks[0]) / sizeof(tasks[0]);
    if (slot >= ZEROOS_MAX_TASKS) return 0;
    return &user_frames[slot];
}

/*
 * Update the per-CPU state that belongs to the task about to run: its
 * address space (CR3, PCID-tagged when available) and the ring-3 stack
 * pointer (TSS.RSP0) used when an exception or interrupt is taken while
 * that task executes in user mode.
 */
static void task_update_cpu_state(struct task *task) {
    struct process *process = task->process;
    uint64_t root;
    uint16_t pcid = 0;

    if (process) {
        root = process->space.root_physical;
        pcid = process->space.has_pcid ? process->space.pcid : 0;
    } else {
        root = vmm_root();
    }

    if (!vmm_pcid_enabled() && root != vmm_active_root())
        vmm_flush_tlb();
    vmm_load_root(root, pcid);
    gdt_set_rsp0(task->stack_base + ZEROOS_TASK_STACK_SIZE - 512ULL);
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
static void task_prepare_stack(struct task *task, int user) {
    uint64_t top=task->stack_base+ZEROOS_TASK_STACK_SIZE;
    uint64_t *sp;

    *(uint64_t *)(uint64_t)task->stack_base=ZEROOS_TASK_STACK_GUARD;

    /*
     * context_switch restores six callee-saved registers then retq. The
     * saved stack must be 0 mod 16 so that six 8-byte pops followed by retq
     * leave the assembly task_trampoline wrapper with RSP 8 mod 16. The
     * wrapper then reserves interrupt headroom before calling the C body.
     *
     * User-mode tasks resume into user_task_entry instead of the trampoline;
     * it iretqs into ring 3 and never returns to the kernel directly (the
     * only exit path is the exit syscall).
     */
    top=(top & ~0xFULL)-8ULL;
    sp=(uint64_t *)top;
    *--sp=(uint64_t)(user ? user_task_entry : task_trampoline);
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
        if (task==current_task || task->state!=TASK_ZOMBIE || !task->stack_base)
            continue;
        page_free((void *)task->stack_base);
        /*
         * The process object (if any) is owned by the process layer and is
         * reaped separately; drop only the task-side reference.
         */
        if (task->process) task->process->thread=0;
        task->process=0;
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

    if (tasks[ZEROOS_IDLE_SLOT].state==TASK_RUNNABLE &&
        (!cooperative || tasks[ZEROOS_IDLE_SLOT].interrupt_frame==0))
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
    if (!task_identity_ok(previous) || !task_identity_ok(&tasks[next]))
        task_context_panic("ZEROOS PANIC: task identity invariant failed.\n",
                           previous);
    if (!task_saved_context_ok(&tasks[next]))
        task_saved_context_panic(&tasks[next]);

    previous->state=TASK_RUNNABLE;
    tasks[next].state=TASK_RUNNING;
    tasks[next].context_switches++;
    current_task=&tasks[next];

    /*
     * Install the target's address space (CR3) and ring-3 stack pointer
     * (TSS.RSP0) BEFORE the context switch: the target's first
     * instructions may transition to user mode, and every one of them
     * must already see the target's translation root. All references in
     * between (task table, both saved contexts) are kernel memory,
     * mapped identically in every root.
     */
    task_update_cpu_state(current_task);
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
        tasks[i].process=0;
    }
    for (int i=0;i<ZEROOS_MAX_TASKS;++i) {
        user_frames[i].rip=0;
        user_frames[i].cs=0;
        user_frames[i].rflags=0;
        user_frames[i].rsp=0;
        user_frames[i].ss=0;
        user_frames[i].arg=0;
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
    task_prepare_stack(&tasks[ZEROOS_IDLE_SLOT], 0);

    spinlock_init(&task_lock);
    current_task=&tasks[0];
    next_task_id=2;
    sleep_head=0;
    return 0;
}

int task_create_internal(task_entry_t entry, void *argument, int user,
                         uint64_t user_rip, uint64_t user_rsp, uint64_t user_arg,
                         uint64_t *task_id) {
    if (!entry && !user) return -1;

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
    task->process=0;
    task_prepare_stack(task, user);

    if (user) {
        struct user_entry_frame *frame=&user_frames[slot];
        frame->rip=user_rip;
        frame->cs=ZEROOS_USER_CS_RING3;
        frame->rflags=0x202ULL; /* IF + reserved bit 1; TF must be clear */
        frame->rsp=user_rsp;
        frame->ss=ZEROOS_USER_SS_RING3;
        frame->arg=(void *)user_arg;
    }

    if (!task_saved_context_ok(task))
        task_saved_context_panic(task);

    /*
     * Validate the whole task table after every creation while interrupts
     * are disabled. This makes a metadata/context overwrite attributable to
     * the creation boundary instead of a much later scheduler failure.
     */
    task_validate_table("ZEROOS PANIC: task creation invariant failed.\n");

    if (task_id) *task_id=task->id;
    spin_unlock_irqrestore(&task_lock,flags);
    return 0;
}

int task_create(task_entry_t entry, void *argument, uint64_t *task_id) {
    return task_create_internal(entry, argument, 0, 0, 0, 0, task_id);
}

/*
 * Create the main thread of a user-mode process. The task's kernel stack
 * serves kernel execution on behalf of the user thread (including
 * interrupts taken while in ring 3, via TSS.RSP0); the user-mode stack is
 * part of the process address space and is supplied via user_rsp.
 */
int task_create_user(uint64_t user_rip, uint64_t user_rsp, uint64_t user_arg,
                     uint64_t *task_id) {
    return task_create_internal(0, 0, 1, user_rip, user_rsp, user_arg, task_id);
}



struct task *task_current(void) {
    return current_task;
}

struct task *task_find_by_id(uint64_t id) {
    for (int i = 0; i < ZEROOS_MAX_TASKS; ++i)
        if (tasks[i].state != TASK_UNUSED && tasks[i].id == id)
            return &tasks[i];
    return 0;
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
    task_validate_table("ZEROOS PANIC: task block invariant failed.\n");
    task_update_cpu_state(current_task);
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
    task_validate_table("ZEROOS PANIC: task sleep invariant failed.\n");
    task_update_cpu_state(current_task);
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
    uint64_t flags;

    if (!previous || previous==&tasks[0] ||
        previous==&tasks[ZEROOS_IDLE_SLOT])
        return;

    flags=task_irq_save();
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
    if (!task_saved_context_ok(&tasks[next]))
        task_saved_context_panic(&tasks[next]);
    task_validate_table("ZEROOS PANIC: task exit invariant failed.\n");
    task_update_cpu_state(current_task);
    context_switch(&previous->saved_stack,&current_task->saved_stack);
    task_irq_restore(flags);

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
    int dying;

    /* Bootstrap runs on the boot stack, not a task-owned frame. Timer
     * delivery is legal between task_system_init and task_start_first. */
    if (!previous || !frame || previous==&tasks[0])
        return (uint64_t)frame;
    if (!task_pointer_ok(previous))
        task_context_panic("ZEROOS PANIC: invalid current task pointer.\n",previous);
    if (!task_identity_ok(previous))
        task_context_panic("ZEROOS PANIC: invalid current task identity.\n",previous);
    /*
     * A task may already be ZOMBIE here when a user-mode fault killed its
     * process before the IRQ-exit path ran; the task is being discarded and
     * the scheduler must switch away from it unconditionally.
     */
    dying=(previous->state==TASK_ZOMBIE);
    if (previous->state!=TASK_RUNNING && !dying)
        task_context_panic("ZEROOS PANIC: current task is not running.\n",previous);
    if (!task_stack_guard_ok(previous)) task_stack_guard_panic(previous);
    if (!task_frame_ok(previous,frame)) {
        serial_write_public("ZEROOS PANIC: invalid current IRQ frame.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    if (previous->preempt_count!=0 && !dying)
        return (uint64_t)frame;

    /*
     * Normal tasks enter the IRQ-exit scheduler only after their time slice
     * expires (or another kernel path explicitly requests rescheduling).
     * Idle is the exception: if runnable work exists, leave idle immediately.
     *
     * The frame is only captured as the task's interrupt frame once a real
     * switch is committed: on the no-switch paths the frame is consumed by
     * the iretq epilogue and must not be retained (a retained pointer to a
     * consumed frame would be resumed a second time).
     */
    if (!dying && previous!=&tasks[ZEROOS_IDLE_SLOT] && !previous->need_resched)
        return (uint64_t)frame;

    previous->interrupt_frame=frame;

    next=find_next_runnable(0);

    if (next<0 || &tasks[next]==previous) {
        previous->need_resched=0;
        /*
         * The architectural frame is consumed by the iretq epilogue. Once
         * execution returns to the task, RSP points above the frame and the
         * normal downward-growing stack may overwrite it. Do not retain a
         * pointer to a frame that is no longer live.
         */
        if (!dying)
            previous->interrupt_frame=0;
        return (uint64_t)frame;
    }

    previous->need_resched=0;
    if (dying) {
        /*
         * The dying task's frame is never resumed (it was captured above
         * only to satisfy the ownership invariants); clear the reference so
         * the zombie invariant "zombies retain no context" holds from the
         * next validation tick.
         */
        previous->interrupt_frame=0;
    } else {
        previous->state=TASK_RUNNABLE;
    }

    /*
     * Idle is a special non-progressing task. If it was interrupted while
     * hlt/yield was running and we are switching away from it, its precise
     * hardware frame is not a required continuation point. Retire that frame
     * and keep the idle task resumable through its stable cooperative context.
     * Ordinary tasks must retain their interrupt frame for exact preemption
     * resume.
     */
    if (previous==&tasks[ZEROOS_IDLE_SLOT])
        previous->interrupt_frame=0;

    /*
     * Capture and retire a target interrupt frame before publishing the new
     * current_task. This makes the context ownership transition atomic with
     * respect to all scheduler diagnostics: a TASK_RUNNING task never
     * advertises a frame that is about to be consumed by iretq.
     */
    struct interrupt_frame *target_frame=tasks[next].interrupt_frame;
    if (target_frame) {
        if (!task_frame_ok(&tasks[next],target_frame)) {
            serial_write_public("ZEROOS PANIC: invalid target IRQ frame.\n");
            for (;;) __asm__ volatile ("cli; hlt");
        }
        serial_write_public("ZEROOS: IRQ switch -> saved frame task ");
        task_write_u64(tasks[next].id);
        serial_write_public(".\n");
        tasks[next].interrupt_frame=0;
    } else if (!task_saved_stack_ok(&tasks[next]) ||
               !task_saved_context_ok(&tasks[next])) {
        task_saved_context_panic(&tasks[next]);
    }

    tasks[next].state=TASK_RUNNING;
    tasks[next].context_switches++;
    current_task=&tasks[next];
    if (!task_pointer_ok(current_task) ||
        current_task->state!=TASK_RUNNING)
        task_context_panic("ZEROOS PANIC: invalid selected task.\n",current_task);

    /*
     * The target context is now exclusively owned by the IRQ-exit path:
     * target_frame -> iretq, or saved_stack -> cooperative restore + retq.
     * The CPU's address space and ring-3 stack pointer must already be the
     * target's before the epilogue returns into that context.
     */
    task_update_cpu_state(&tasks[next]);

    if (target_frame)
        return (uint64_t)target_frame;

    return tasks[next].saved_stack | 1ULL;
}

void task_scheduler_tick(void) {
    struct task *task=current_task;
    uint64_t now;

    if (!task) return;

    /*
     * Before task_start_first() parks the bootstrap task, the timer tick
     * hook is already registered and the bootstrap task (slot 0) is RUNNING
     * without a task stack. A tick landing in that window is legal, not a
     * corrupted guard; do nothing until a real task owns the CPU.
     */
    if (task == &tasks[0])
        return;

    if (task->state != TASK_RUNNING || task->stack_base == 0 ||
        task->saved_stack == 0)
        task_stack_guard_panic(task);

    /*
     * interrupt_frame is a pending resume context only while a task is
     * suspended. A RUNNING task is executing on the CPU; its current hardware
     * IRQ frame belongs to the active interrupt path and will be captured by
     * task_reschedule_from_interrupt() after the timer hook returns. If a
     * previous resume frame was left behind, retire that stale metadata before
     * validating the task table.
     */
    if (task->state==TASK_RUNNING && task->interrupt_frame) {
        serial_write_public("ZEROOS: retiring stale IRQ frame from running task ");
        task_write_u64(task->id);
        serial_write_public(".\n");
        task->interrupt_frame=0;
    }

    now=timer_ticks();
    if (!task_stack_guard_ok(task)) task_stack_guard_panic(task);
    {
        uint64_t flags=spin_lock_irqsave(&task_lock);
        sleep_queue_wake_expired_locked(now);
        reap_zombies_locked();
        spin_unlock_irqrestore(&task_lock,flags);
    }

    task_validate_table("ZEROOS PANIC: scheduler table invariant failed.\n");

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
    if (tasks[next].interrupt_frame!=0 ||
        !task_saved_context_ok(&tasks[next]))
        task_saved_context_panic(&tasks[next]);
    task_validate_table("ZEROOS PANIC: scheduler start invariant failed.\n");
    task_update_cpu_state(current_task);
    context_switch(&tasks[0].saved_stack,&current_task->saved_stack);

    for (;;) __asm__ volatile ("cli; hlt");
}

int task_debug_validate(void) {
    task_validate_table("ZEROOS PANIC: explicit scheduler checkpoint failed.\n");
    return 0;
}

uint64_t task_count(void) {
    uint64_t count=0;
    for (int i=0;i<ZEROOS_MAX_TASKS;++i)
        if (tasks[i].state!=TASK_UNUSED)
            ++count;
    return count;
}

/* Cancel only a never-selected thread. Used to roll back an unpublished
 * process; no live stack, wait queue or interrupt frame may be abandoned. */
int task_discard_new(uint64_t tid) {
    uint64_t flags=spin_lock_irqsave(&task_lock);
    struct task *task=task_find_by_id(tid);
    int result=-1;
    if (task && task!=current_task && task->state==TASK_RUNNABLE &&
        task->context_switches==0 && task->interrupt_frame==0) {
        page_free((void *)task->stack_base);
        if (task->process) task->process->thread=0;
        for (unsigned i=0;i<sizeof(*task);++i) ((uint8_t *)task)[i]=0;
        result=0;
    }
    spin_unlock_irqrestore(&task_lock,flags);
    return result;
}

/* A process reaper need not wait for another timer tick to release its
 * terminal thread. The executing stack is never eligible for reclamation. */
int task_reap_finished(uint64_t tid) {
    uint64_t flags=spin_lock_irqsave(&task_lock);
    struct task *task=task_find_by_id(tid);
    int result=-1;
    if (task && task!=current_task && task->state==TASK_ZOMBIE) {
        page_free((void *)task->stack_base);
        if (task->process) task->process->thread=0;
        for (unsigned i=0;i<sizeof(*task);++i) ((uint8_t *)task)[i]=0;
        result=0;
    }
    spin_unlock_irqrestore(&task_lock,flags);
    return result;
}
