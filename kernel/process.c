#include "process.h"
#include "sync.h"
#include "thread.h"
#include "ipc.h"
#include "shmem.h"

#define ZEROOS_MAX_PROCESSES 16U
#define ZEROOS_PROCESS_SLOT_BITS 16U
#define ZEROOS_PROCESS_SLOT_MASK ((1ULL << ZEROOS_PROCESS_SLOT_BITS) - 1ULL)

static struct process processes[ZEROOS_MAX_PROCESSES];
static struct spinlock process_lock;

static int process_pointer_valid(const struct process *process) {
    uint64_t address;
    uint64_t base;
    uint64_t end;

    if (!process) return 0;
    address=(uint64_t)process;
    base=(uint64_t)&processes[0];
    end=(uint64_t)&processes[ZEROOS_MAX_PROCESSES];
    return address>=base && address<end &&
           ((address-base) % sizeof(processes[0]))==0;
}

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
    process->reaping_threads=0;
    process->max_threads=0;
    process->max_children=0;
    process->max_address_space_pages=0;
    process->resident_pages=0;
    process->exit_status=0;
    process->address_space.root=0;
    process->address_space.root_physical=0;
    process->address_space.mapped_pages=0;
    process->address_space.max_pages=0;
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
        processes[i].reaping_threads=0;
        processes[i].max_threads=0;
        processes[i].max_children=0;
        processes[i].max_address_space_pages=0;
        processes[i].resident_pages=0;
        processes[i].exit_status=0;
        processes[i].address_space.root=0;
        processes[i].address_space.root_physical=0;
        processes[i].address_space.mapped_pages=0;
        processes[i].address_space.max_pages=0;
    }
    return 0;
}

int process_create(struct process *parent, process_id_t *pid_out) {
    uint64_t flags=spin_lock_irqsave(&process_lock);
    int slot=-1;

    if (parent && (parent->state==PROCESS_UNUSED ||
                   parent->state==PROCESS_ZOMBIE ||
                   process_lookup_locked(parent->pid)!=parent ||
                   parent->child_count>=parent->max_children)) {
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
    process->reaping_threads=0;
    process->max_threads=ZEROOS_PROCESS_DEFAULT_MAX_THREADS;
    process->max_children=ZEROOS_PROCESS_DEFAULT_MAX_CHILDREN;
    process->max_address_space_pages=ZEROOS_PROCESS_DEFAULT_MAX_ADDRESS_SPACE_PAGES;
    process->resident_pages=0;
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

struct process *process_find_child(struct process *parent, process_id_t pid) {
    uint64_t flags;
    struct process *child;
    struct process *match=0;

    if (!parent)
        return 0;
    flags=spin_lock_irqsave(&process_lock);
    if (process_lookup_locked(parent->pid)!=parent ||
        parent->state==PROCESS_UNUSED || parent->state==PROCESS_ZOMBIE) {
        spin_unlock_irqrestore(&process_lock,flags);
        return 0;
    }
    for (child=parent->first_child; child; child=child->next_sibling) {
        if (pid!=0 && child->pid!=pid)
            continue;
        if (pid==0 && child->state!=PROCESS_ZOMBIE)
            continue;
        match=child;
        break;
    }
    spin_unlock_irqrestore(&process_lock,flags);
    return match;
}

int process_thread_reserve(struct process *process) {
    uint64_t flags=spin_lock_irqsave(&process_lock);

    if (!process || process_lookup_locked(process->pid)!=process ||
        process->state==PROCESS_ZOMBIE) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }

    if (process->creating_threads==0xffffffffffffffffULL ||
        process->thread_count>process->max_threads ||
        process->creating_threads>process->max_threads-process->thread_count) {
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

int process_thread_unreserve(struct process *process) {
    uint64_t flags=spin_lock_irqsave(&process_lock);
    if (!process || process_lookup_locked(process->pid)!=process ||
        process->creating_threads==0) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }
    process_thread_unreserve_locked(process);
    spin_unlock_irqrestore(&process_lock,flags);
    return 0;
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

int process_thread_reap_begin(struct thread *thread,
                              struct process **owner_out) {
    struct process *process;
    struct thread **cursor;
    uint64_t flags;

    if (!thread || !thread->process)
        return -1;
    process=thread->process;
    flags=spin_lock_irqsave(&process_lock);
    if (process_lookup_locked(process->pid)!=process ||
        thread->state!=THREAD_ZOMBIE ||
        process->reaping_threads==~0ULL) {
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
    ++process->reaping_threads;
    if (owner_out)
        *owner_out=process;
    spin_unlock_irqrestore(&process_lock,flags);
    return 0;
}

int process_thread_reap_finish(struct process *process) {
    uint64_t flags;
    if (!process) return -1;
    flags=spin_lock_irqsave(&process_lock);
    if (process_lookup_locked(process->pid)!=process ||
        process->reaping_threads==0) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }
    --process->reaping_threads;
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
        process->reaping_threads!=0 ||
        process->first_child!=0 ||
        process->resident_pages!=
            vmm_space_mapped_pages(&process->address_space)) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }

    if (exit_status_out)
        *exit_status_out=process->exit_status;

    /* IPC and shared-memory capability references are revoked before the
     * process object is reset. Both revoke paths only take their subsystem
     * lock and clean tracked mappings before the private root is destroyed,
     * so no stale owner pointer or shared physical-page reference survives
     * process reuse. */
    if (ipc_process_revoke(process)<0 ||
        shmem_process_revoke(process)<0) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }

    parent=process->parent;

    /*
     * The process lock intentionally excludes only process-table mutations.
     * Refuse to publish the reap until the private root has been destroyed;
     * an active root must first be switched away with vmm_activate_kernel().
     * This prevents a failed address-space teardown from silently losing the
     * root and leaking all of its page-table pages.
     */
    if (vmm_space_destroy(&process->address_space)!=0) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }
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
    process_reset_locked(process);
    spin_unlock_irqrestore(&process_lock,flags);
    return 0;
}

int process_abort_new(struct process *process) {
    uint64_t flags;
    struct process *parent;

    if (!process)
        return -1;
    flags=spin_lock_irqsave(&process_lock);
    if (process_lookup_locked(process->pid)!=process ||
        process->state!=PROCESS_NEW || process->thread_count!=0 ||
        process->live_thread_count!=0 || process->creating_threads!=0 ||
        process->reaping_threads!=0 || process->first_child!=0) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }
    if (ipc_process_revoke(process)<0 ||
        shmem_process_revoke(process)<0 ||
        vmm_space_destroy(&process->address_space)!=0) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }
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
    process_reset_locked(process);
    spin_unlock_irqrestore(&process_lock,flags);
    return 0;
}

int process_set_limits(struct process *process, uint64_t max_threads,
                       uint64_t max_children,
                       uint64_t max_address_space_pages) {
    if (!process || max_threads==0 || max_children==0 ||
        max_address_space_pages==0)
        return -1;
    uint64_t flags=spin_lock_irqsave(&process_lock);
    if (process_lookup_locked(process->pid)!=process ||
        process->state==PROCESS_UNUSED ||
        process->thread_count>max_threads ||
        process->creating_threads>max_threads-process->thread_count ||
        process->child_count>max_children ||
        process->resident_pages>max_address_space_pages ||
        vmm_space_set_page_limit(&process->address_space,
                                  max_address_space_pages)!=0) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }
    process->max_threads=max_threads;
    process->max_children=max_children;
    process->max_address_space_pages=max_address_space_pages;
    spin_unlock_irqrestore(&process_lock,flags);
    return 0;
}

int process_get_limits(const struct process *process, uint64_t *max_threads,
                       uint64_t *max_children,
                       uint64_t *max_address_space_pages) {
    if (!process) return -1;
    uint64_t flags=spin_lock_irqsave(&process_lock);
    if (process_lookup_locked(process->pid)!=process ||
        process->state==PROCESS_UNUSED) {
        spin_unlock_irqrestore(&process_lock,flags);
        return -1;
    }
    if (max_threads) *max_threads=process->max_threads;
    if (max_children) *max_children=process->max_children;
    if (max_address_space_pages)
        *max_address_space_pages=process->max_address_space_pages;
    spin_unlock_irqrestore(&process_lock,flags);
    return 0;
}

int process_address_space_map_page(struct process *process,
                                   uint64_t virtual_address,
                                   uint64_t physical_address,
                                   uint64_t flags) {
    uint64_t irq_flags=spin_lock_irqsave(&process_lock);
    if (!process || process_lookup_locked(process->pid)!=process ||
        process->state==PROCESS_UNUSED || process->state==PROCESS_ZOMBIE ||
        process->resident_pages!=
            vmm_space_mapped_pages(&process->address_space) ||
        process->resident_pages==~0ULL) {
        spin_unlock_irqrestore(&process_lock,irq_flags);
        return -1;
    }
    if (vmm_space_map_page(&process->address_space,virtual_address,
                           physical_address,flags)!=0) {
        spin_unlock_irqrestore(&process_lock,irq_flags);
        return -1;
    }
    ++process->resident_pages;
    spin_unlock_irqrestore(&process_lock,irq_flags);
    return 0;
}

int process_address_space_unmap_page(struct process *process,
                                     uint64_t virtual_address) {
    uint64_t irq_flags=spin_lock_irqsave(&process_lock);
    if (!process || process_lookup_locked(process->pid)!=process ||
        process->state==PROCESS_UNUSED || process->state==PROCESS_ZOMBIE ||
        process->resident_pages==0 ||
        process->resident_pages!=
            vmm_space_mapped_pages(&process->address_space)) {
        spin_unlock_irqrestore(&process_lock,irq_flags);
        return -1;
    }
    if (vmm_space_unmap_page(&process->address_space,virtual_address)!=0) {
        spin_unlock_irqrestore(&process_lock,irq_flags);
        return -1;
    }
    --process->resident_pages;
    spin_unlock_irqrestore(&process_lock,irq_flags);
    return 0;
}

uint64_t process_address_space_mapped_pages(const struct process *process) {
    if (!process) return 0;
    uint64_t irq_flags=spin_lock_irqsave(&process_lock);
    uint64_t count=0;
    if (process_lookup_locked(process->pid)==process &&
        process->state!=PROCESS_UNUSED)
        count=vmm_space_mapped_pages(&process->address_space);
    spin_unlock_irqrestore(&process_lock,irq_flags);
    return count;
}

int process_address_space_is_user_range(const struct process *process,
                                        uint64_t virtual_address,
                                        uint64_t length, uint64_t write) {
    if (!process) return 0;
    uint64_t irq_flags=spin_lock_irqsave(&process_lock);
    int valid=process_lookup_locked(process->pid)==process &&
              process->state!=PROCESS_UNUSED &&
              vmm_space_is_user_range(&process->address_space,
                                      virtual_address,length,write);
    spin_unlock_irqrestore(&process_lock,irq_flags);
    return valid;
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

int process_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&process_lock);

    for (uint32_t i=0;i<ZEROOS_MAX_PROCESSES;++i) {
        struct process *process=&processes[i];
        uint32_t slot;
        uint32_t generation;

        if (process->state==PROCESS_UNUSED) {
            if (process->pid || process->parent || process->first_child ||
                process->next_sibling || process->child_count ||
                process->first_thread || process->thread_count ||
                process->live_thread_count || process->creating_threads ||
                process->reaping_threads || process->max_threads || process->max_children ||
                process->max_address_space_pages || process->resident_pages ||
                process->address_space.root ||
                process->address_space.root_physical ||
                process->address_space.mapped_pages ||
                process->address_space.max_pages) {
                spin_unlock_irqrestore(&process_lock,flags);
                return -1;
            }
            continue;
        }

        if (process_decode_id(process->pid,&slot,&generation)!=0 ||
            slot!=i || generation!=process->generation ||
            process_lookup_locked(process->pid)!=process) {
            spin_unlock_irqrestore(&process_lock,flags);
            return -1;
        }

        if (process->max_threads==0 || process->max_children==0 ||
            process->max_address_space_pages==0 ||
            process->thread_count>process->max_threads ||
            process->reaping_threads>process->max_threads ||
            process->child_count>process->max_children ||
            process->resident_pages>process->max_address_space_pages ||
            !process->address_space.root ||
            process->address_space.max_pages!=
                process->max_address_space_pages ||
            process->address_space.mapped_pages!=process->resident_pages) {
            spin_unlock_irqrestore(&process_lock,flags);
            return -1;
        }

        /* A zombie process may still own unreaped children.  Child
         * ownership is deliberately retained until each child has been
         * reaped, so a validator must not mistake that normal intermediate
         * lifetime state for corruption. process_reap() still requires the
         * list to be empty before destruction. */
        if (process->state==PROCESS_ZOMBIE &&
            (process->live_thread_count || process->creating_threads)) {
            spin_unlock_irqrestore(&process_lock,flags);
            return -1;
        }

        if (process->live_thread_count>process->thread_count) {
            spin_unlock_irqrestore(&process_lock,flags);
            return -1;
        }

        if (process->child_count) {
            uint64_t count=0;
            struct process *child=process->first_child;
            while (child) {
                if (!process_pointer_valid(child) || child->parent!=process) {
                    spin_unlock_irqrestore(&process_lock,flags);
                    return -1;
                }
                if (++count>ZEROOS_MAX_PROCESSES) {
                    spin_unlock_irqrestore(&process_lock,flags);
                    return -1;
                }
                child=child->next_sibling;
            }
            if (count!=process->child_count) {
                spin_unlock_irqrestore(&process_lock,flags);
                return -1;
            }
        } else if (process->first_child) {
            spin_unlock_irqrestore(&process_lock,flags);
            return -1;
        }
    }

    spin_unlock_irqrestore(&process_lock,flags);
    return 0;
}
