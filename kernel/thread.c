#include "thread.h"
#include "process.h"
#include "sync.h"

extern void serial_write_public(const char *text);

#define ZEROOS_MAX_THREADS 32U
#define ZEROOS_THREAD_SLOT_BITS 16U
#define ZEROOS_THREAD_SLOT_MASK ((1ULL << ZEROOS_THREAD_SLOT_BITS) - 1ULL)

static struct thread threads[ZEROOS_MAX_THREADS];
static struct spinlock thread_lock;

static uint64_t thread_make_id(uint32_t slot, uint32_t generation) {
    return ((uint64_t)generation << ZEROOS_THREAD_SLOT_BITS) |
           ((uint64_t)slot + 1ULL);
}

static int thread_decode_id(thread_id_t tid,
                            uint32_t *slot_out,
                            uint32_t *generation_out) {
    uint64_t encoded_slot=tid & ZEROOS_THREAD_SLOT_MASK;
    uint64_t generation=tid >> ZEROOS_THREAD_SLOT_BITS;

    if (encoded_slot==0 || encoded_slot>ZEROOS_MAX_THREADS ||
        generation==0 || generation>0xffffffffULL)
        return -1;

    *slot_out=(uint32_t)(encoded_slot-1ULL);
    *generation_out=(uint32_t)generation;
    return 0;
}

static struct thread *thread_lookup_locked(thread_id_t tid) {
    uint32_t slot;
    uint32_t generation;

    if (thread_decode_id(tid,&slot,&generation)!=0)
        return 0;

    if (threads[slot].state==THREAD_UNUSED ||
        threads[slot].tid!=tid ||
        threads[slot].generation!=generation)
        return 0;

    return &threads[slot];
}

static void thread_reset_locked(struct thread *thread) {
    thread->tid=0;
    thread->state=THREAD_UNUSED;
    thread->process=0;
    thread->entry=0;
    thread->argument=0;
    thread->scheduler_task_id=0;
    thread->exit_status=0;
    thread->next_in_process=0;
}

static void thread_bootstrap(void *argument) {
    struct thread *thread=(struct thread *)argument;
    if (!thread || !thread->process || !thread->entry) {
        task_exit();
        return;
    }

    thread->state=THREAD_RUNNING;
    (void)process_thread_started(thread);
    thread->entry(thread->argument);
    (void)thread_exit(0);

    for (;;) __asm__ volatile ("cli; hlt");
}

int thread_system_init(void) {
    spinlock_init(&thread_lock);
    for (uint32_t i=0;i<ZEROOS_MAX_THREADS;++i) {
        threads[i].tid=0;
        threads[i].generation=0;
        threads[i].state=THREAD_UNUSED;
        threads[i].process=0;
        threads[i].entry=0;
        threads[i].argument=0;
        threads[i].scheduler_task_id=0;
        threads[i].exit_status=0;
        threads[i].next_in_process=0;
    }
    return 0;
}

struct thread *thread_current(void) {
    struct task *task=task_current();
    return task ? task->thread : 0;
}

struct thread *thread_lookup(thread_id_t tid) {
    uint64_t flags=spin_lock_irqsave(&thread_lock);
    struct thread *thread=thread_lookup_locked(tid);
    spin_unlock_irqrestore(&thread_lock,flags);
    return thread;
}

int thread_create_kernel(struct process *process,
                         task_entry_t entry,
                         void *argument,
                         thread_id_t *tid_out) {
    uint64_t flags;
    int slot=-1;
    struct thread *thread;
    uint64_t task_id;

    if (!process || !entry ||
        process_thread_reserve(process)!=0)
        return -1;

    flags=spin_lock_irqsave(&thread_lock);
    for (uint32_t i=0;i<ZEROOS_MAX_THREADS;++i) {
        if (threads[i].state==THREAD_UNUSED &&
            threads[i].generation!=0xffffffffU) {
            slot=(int)i;
            break;
        }
    }

    if (slot<0) {
        spin_unlock_irqrestore(&thread_lock,flags);
        (void)process_thread_unreserve(process);
        return -1;
    }

    thread=&threads[slot];
    uint32_t generation=thread->generation+1U;
    if (generation==0) {
        spin_unlock_irqrestore(&thread_lock,flags);
        (void)process_thread_reserve(process);
        return -1;
    }

    thread->generation=generation;
    thread->tid=thread_make_id((uint32_t)slot,generation);
    thread->state=THREAD_NEW;
    thread->process=process;
    thread->entry=entry;
    thread->argument=argument;
    thread->scheduler_task_id=0;
    thread->exit_status=0;
    thread->next_in_process=0;
    spin_unlock_irqrestore(&thread_lock,flags);

    /*
     * Prevent a timer IRQ from running the newly created task before its
     * process/thread ownership links are complete. This API is intentionally
     * for already-running kernel threads; the bootstrap user-thread path will
     * get an explicit creation primitive later.
     */
    if (task_preempt_disable()!=0) {
        flags=spin_lock_irqsave(&thread_lock);
        thread_reset_locked(thread);
        spin_unlock_irqrestore(&thread_lock,flags);
        return -1;
    }

    if (task_create_owned(thread_bootstrap,thread,thread,&task_id)!=0) {
        (void)task_preempt_enable();
        flags=spin_lock_irqsave(&thread_lock);
        thread_reset_locked(thread);
        spin_unlock_irqrestore(&thread_lock,flags);
        (void)process_thread_reserve(process);
        return -1;
    }

    thread->scheduler_task_id=task_id;
    thread->state=THREAD_RUNNABLE;

    if (process_thread_attach(process,thread)!=0) {
        /*
         * We cannot safely make task_exit() happen from this caller, so keep
         * the task out of the process model only on the impossible ownership
         * failure path and halt rather than creating an orphan runnable task.
         */
        serial_write_public("ZEROOS PANIC: failed to attach kernel thread.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    if (tid_out)
        *tid_out=thread->tid;

    (void)task_preempt_enable();
    return 0;
}

int thread_exit(uint64_t exit_status) {
    struct thread *thread=thread_current();
    if (!thread || thread->state==THREAD_ZOMBIE ||
        thread->state==THREAD_UNUSED)
        return -1;

    if (process_thread_exited(thread,exit_status)!=0)
        return -1;
    thread->state=THREAD_ZOMBIE;
    thread->scheduler_task_id=0;

    task_exit();
    return 0;
}

int thread_reap(struct thread *thread, uint64_t *exit_status_out) {
    uint64_t flags;

    if (!thread) return -1;

    flags=spin_lock_irqsave(&thread_lock);
    if (thread_lookup_locked(thread->tid)!=thread ||
        thread->state!=THREAD_ZOMBIE ||
        thread->scheduler_task_id!=0) {
        spin_unlock_irqrestore(&thread_lock,flags);
        return -1;
    }

    if (process_thread_detach(thread)!=0) {
        spin_unlock_irqrestore(&thread_lock,flags);
        return -1;
    }

    if (exit_status_out)
        *exit_status_out=thread->exit_status;

    thread_reset_locked(thread);
    spin_unlock_irqrestore(&thread_lock,flags);
    return 0;
}
