#ifndef ZEROOS_PROCESS_H
#define ZEROOS_PROCESS_H

#include "types.h"
#include "vmm.h"

#define ZEROOS_PROCESS_DEFAULT_MAX_THREADS 32ULL
#define ZEROOS_PROCESS_DEFAULT_MAX_CHILDREN 16ULL
#define ZEROOS_PROCESS_DEFAULT_MAX_ADDRESS_SPACE_PAGES (~0ULL)

typedef uint64_t process_id_t;

enum process_state {
    PROCESS_UNUSED=0,
    PROCESS_NEW,
    PROCESS_RUNNING,
    PROCESS_ZOMBIE
};

struct thread;

struct process {
    process_id_t pid;
    uint32_t generation;
    enum process_state state;

    struct process *parent;
    struct process *first_child;
    struct process *next_sibling;
    uint64_t child_count;

    struct thread *first_thread;
    uint64_t thread_count;
    uint64_t live_thread_count;
    uint64_t creating_threads;
    /* A detached zombie remains process-owned until its thread object is
     * reset; this prevents process reap from racing thread-object reuse. */
    uint64_t reaping_threads;
    /* Temporary kernel pins prevent a target process from being reaped or
     * transitioning to zombie while a cross-process capability grant is
     * being published. */
    uint64_t lifetime_refs;

    /* Explicit resource ceilings; zero is never interpreted as unlimited. */
    uint64_t max_threads;
    uint64_t max_children;
    uint64_t max_address_space_pages;
    uint64_t resident_pages;

    uint64_t exit_status;

    /* The process owns its private user address-space root. */
    struct vmm_space address_space;
};

int process_system_init(void);

int process_create(struct process *parent, process_id_t *pid_out);
struct process *process_lookup(process_id_t pid);
/* Acquire/release a generation-checked live-process pin. A pinned process
 * cannot be published as zombie or reaped until the release. */
int process_acquire_live(process_id_t pid, struct process **process_out);
int process_release_live(struct process *process);
/* Returns a live child owned by parent, preferring a zombie child when pid is
 * zero. The pointer is stable until the caller reaps that child. */
struct process *process_find_child(struct process *parent, process_id_t pid);

int process_thread_reserve(struct process *process);
int process_thread_unreserve(struct process *process);
int process_thread_attach(struct process *process, struct thread *thread);
int process_thread_started(struct thread *thread);
int process_thread_exited(struct thread *thread, uint64_t exit_status);
int process_thread_detach(struct thread *thread);
int process_thread_reap_begin(struct thread *thread,
                              struct process **owner_out);
int process_thread_reap_finish(struct process *process);

int process_reap(struct process *process, uint64_t *exit_status_out);
/* Destroy a process that has never published a thread. This is the
 * transactional rollback path for exec/spawn setup failures. */
int process_abort_new(struct process *process);
int process_set_limits(struct process *process, uint64_t max_threads,
                       uint64_t max_children,
                       uint64_t max_address_space_pages);
int process_get_limits(const struct process *process, uint64_t *max_threads,
                       uint64_t *max_children,
                       uint64_t *max_address_space_pages);
int process_address_space_map_page(struct process *process,
                                   uint64_t virtual_address,
                                   uint64_t physical_address,
                                   uint64_t flags);
int process_address_space_unmap_page(struct process *process,
                                     uint64_t virtual_address);
uint64_t process_address_space_mapped_pages(const struct process *process);
int process_address_space_is_user_range(const struct process *process,
                                        uint64_t virtual_address,
                                        uint64_t length, uint64_t write);

uint64_t process_child_count(const struct process *process);
uint64_t process_thread_count(const struct process *process);
uint64_t process_live_thread_count(const struct process *process);
int process_debug_validate(void);

#endif
