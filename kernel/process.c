#include "process.h"
#include "sync.h"
#include "thread.h"

#define ZEROOS_MAX_PROCESSES 16U
#define ZEROOS_PROCESS_SLOT_BITS 16U
#define ZEROOS_PROCESS_SLOT_MASK ((1ULL << ZEROOS_PROCESS_SLOT_BITS) - 1ULL)

static struct process processes[ZEROOS_MAX_PROCESSES];
static struct spinlock process_lock;

static uint64_t process_make_id(uint32_t slot, uint32_t generation) {
    return ((uint64_t)generation << ZEROOS_PROCESS_SLOT_BITS) |
           ((uint64_t)slot + 1ULL);
}

static int process_decode_id(process_id_t pid,
                             uint32_t *slot_out,
                             uint32_t *generation_out) {
    uint64_t encoded_slot = pid & ZEROOS_PROCESS_SLOT_MASK;
    uint64_t generation = pid >> ZEROOS_PROCESS_SLOT_BITS;

    if (encoded_slot == 0 || encoded_slot > ZEROOS_MAX_PROCESSES ||
        generation == 0 || generation > 0xffffffffULL)
        return -1;

    *slot_out=(uint32_t)(encoded_slot - 1ULL);
    *generation_out=(uint32_t)generation;
    return 0;
}

static struct process *process_lookup_locked(process_id_t pid) {
    uint32_t slot;
    uint32_t generation;

    if (process_decode_id(pid,&slot,&generation)!=0)
        return 0;

    if (processes[slot].state==PROCESS_UNUSED ||
        processes[slot].pid!=pid ||
        processes[slot].generation!=generation)
        return 0;

    return &processes[slot];
}

static void process_reset_locked(struct process *process) {
    process->pid=0;
    process->state=PROCESS_UNUSED;
    process->parent=0;
    process->first_child=0;
    process->next_sibling=0;
    process->child_count=0;
    process->first_thread=0;
    process->thread_count=0;
    process->live_thread_count=0;
    process->creating_threads=0;
    process->exit_status=0;
    process->address_space.root=0;
    process->address_space.root_physical=0;
}

int process_system_init(void) {
    spinlock_init(&process_lock);
    for (uint32_t i=0;i<ZEROOS_MAX_PROCESSES;++i) {
        processes[i].pid=0;
        processes[i].generation=0;
        processes[i].state=PROCESS_UNUSED;
        processes[i].parent=0;
        processes[i].first_child=0;
        processes[i].next_sibling=0;
        processes[i].child_count=0;
        processes[i].first_thread=0;
        processes[i].thread_count=0;
        processes[i].live_thread_count=0;
        processes[i].creating_threads=0;
        processes[i].exit_status=0;
        processes[i].address_space.root=0;
        processes[i].address_space.root_physical=0;
    }
    return 0;
}

int process_create(struct process *parent, process_id_t *pid_out) {
    uint64_t flags=spin_lock_irqsave(&process_lock);
    int slot=-1;

    if (parent && (parent->state==PROCESS_UNUSED ||
                   process_lookup_locked(parent->pid)!=parent)) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }

    for (uint32_t i=0;i<ZEROOS_MAX_PROCESSES;++i) {
        if (processes[i].state!=PROCESS_UNUSED)
            continue;
        if (processes[i].generation==0xffffffffU)
            continue;
        slot=(int)i;
        break;
    }

    if (slot<0) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }

    struct process *process=&processes[slot];
    uint32_t generation=process->generation+1U;
    if (generation==0) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }

    if (vmm_space_create(&process->address_space)!=0) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }

    process->generation=generation;
    process->pid=process_make_id((uint32_t)slot,generation);
    process->state=PROCESS_NEW;
    process->parent=parent;
    process->first_child=0;
    process->next_sibling=0;
    process->child_count=0;
    process->first_thread=0;
    process->thread_count=0;
    process->live_thread_count=0;
    process->creating_threads=0;
    process->exit_status=0;

    if (parent) {
        process->next_sibling=parent->first_child;
        parent->first_child=process;
        ++parent->child_count;
    }

    if (pid_out)
        *pid_out=process->pid;

    spin_unlock_irqrestore(&process_lock,flags);
    return 0;
}

struct process *process_lookup(process_id_t pid) {
    uint64_t flags=spin_lock_irqsave(&process_lock);
    struct process *process=process_lookup_locked(pid);
    spin_unlock_irqrestore(&process_lock,flags);
    return process;
}

int process_thread_reserve(struct process *process) {
    uint64_t flags=spin_lock_irqsave(&process_lock);

    if (!process || process_lookup_locked(process->pid)!=process ||
        process->state==PROCESS_ZOMBIE) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }

    if (process->creating_threads==0xffffffffffffffffULL) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }

    ++process->creating_threads;
    spin_unlock_irqrestore(&process_lock,flags);
    return 0;
}

static void process_thread_unreserve_locked(struct process *process) {
    if (process->creating_threads)
        --process->creating_threads;
}

int process_thread_attach(struct process *process, struct thread *thread) {
    uint64_t flags=spin_lock_irqsave(&process_lock);

    if (!process || !thread ||
        process_lookup_locked(process->pid)!=process ||
        process->state==PROCESS_ZOMBIE ||
        process->creating_threads==0) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }

    process_thread_unreserve_locked(process);
    thread->next_in_process=process->first_thread;
    process->first_thread=thread;
    ++process->thread_count;
    ++process->live_thread_count;
    if (process->state==PROCESS_NEW)
        process->state=PROCESS_RUNNING;

    spin_unlock_irqrestore(&process_lock,flags);
    return 0;
}

int process_thread_started(struct thread *thread) {
    struct process *process;
    uint64_t flags;

    if (!thread || !thread->process)
        return -1;
    process=thread->process;
    flags=spin_lock_irqsave(&process_lock);

    if (process_lookup_locked(process->pid)!=process ||
        thread->state==THREAD_ZOMBIE || thread->state==THREAD_UNUSED) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }

    if (process->state==PROCESS_NEW)
        process->state=PROCESS_RUNNING;

    spin_unlock_irqrestore(&process_lock,flags);
    return 0;
}

int process_thread_exited(struct thread *thread, uint64_t exit_status) {
    struct process *process;
    uint64_t flags;

    if (!thread || !thread->process)
        return -1;
    process=thread->process;
    flags=spin_lock_irqsave(&process_lock);

    if (process_lookup_locked(process->pid)!=process ||
        thread->state==THREAD_UNUSED ||
        thread->state==THREAD_ZOMBIE) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }

    thread->exit_status=exit_status;
    if (process->live_thread_count)
        --process->live_thread_count;
    if (process->live_thread_count==0 &&
        process->creating_threads==0) {
        process->exit_status=exit_status;
        process->state=PROCESS_ZOMBIE;
    }

    spin_unlock_irqrestore(&process_lock,flags);
    return 0;
}

int process_thread_detach(struct thread *thread) {
    struct process *process;
    struct thread **cursor;
    uint64_t flags;

    if (!thread || !thread->process)
        return -1;
    process=thread->process;
    flags=spin_lock_irqsave(&process_lock);

    if (process_lookup_locked(process->pid)!=process) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }

    cursor=&process->first_thread;
    while (*cursor && *cursor!=thread)
        cursor=&(*cursor)->next_in_process;
    if (*cursor!=thread) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }

    *cursor=thread->next_in_process;
    thread->next_in_process=0;
    if (process->thread_count)
        --process->thread_count;

    spin_unlock_irqrestore(&process_lock,flags);
    return 0;
}

int process_reap(struct process *process, uint64_t *exit_status_out) {
    uint64_t flags;
    struct process *parent;

    if (!process) return -1;
    flags=spin_lock_irqsave(&process_lock);

    if (process_lookup_locked(process->pid)!=process ||
        process->state!=PROCESS_ZOMBIE ||
        process->thread_count!=0 ||
        process->live_thread_count!=0 ||
        process->creating_threads!=0 ||
        process->first_child!=0) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }

    if (exit_status_out)
        *exit_status_out=process->exit_status;

    parent=process->parent;
    if (parent) {
        struct process **cursor=&parent->first_child;
        while (*cursor && *cursor!=process)
            cursor=&(*cursor)->next_sibling;
        if (*cursor==process) {
            *cursor=process->next_sibling;
            if (parent->child_count)
                --parent->child_count;
        }
    }

    /*
     * The process lock intentionally excludes only process-table mutations.
     * vmm_space_destroy() does not acquire process_lock, so destroying the
     * private address-space root here cannot deadlock the process manager.
     */
    vmm_space_destroy(&process->address_space);
    process_reset_locked(process);
    spin_unlock_irqrestore(&process_lock,flags);
    return 0;
}

uint64_t process_child_count(const struct process *process) {
    if (!process) return 0;
    return process->child_count;
}

uint64_t process_thread_count(const struct process *process) {
    if (!process) return 0;
    return process->thread_count;
}

uint64_t process_live_thread_count(const struct process *process) {
    if (!process) return 0;
    return process->live_thread_count;
}
