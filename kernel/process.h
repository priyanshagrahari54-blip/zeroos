#ifndef ZEROOS_PROCESS_H
#define ZEROOS_PROCESS_H

#include "types.h"
#include "vmm.h"

#define ZEROOS_MAX_PROCESSES 16

/*
 * Fixed user virtual-address layout inside PML4 slot 254.
 *
 *   0x7f0000000000  user code page   (user, executable, not writable)
 *   0x7f0000002000  user data page   (user, writable, not executable)
 *   0x7f0000004000  user stack page  (user, writable, not executable)
 *
 * The same virtual addresses exist in every process address space but map
 * to that process's own physical pages (enforced by exclusive page
 * ownership), which is the isolation property Stage 1 certifies.
 */
#define ZEROOS_USER_CODE_VA   0x00007f0000000000ULL
#define ZEROOS_USER_DATA_VA   0x00007f0000002000ULL
#define ZEROOS_USER_STACK_VA  0x00007f0000004000ULL

enum process_state {
    PROCESS_UNUSED = 0,
    PROCESS_RUNNING,
    PROCESS_ZOMBIE
};

struct task;

struct process {
    uint64_t pid;
    uint64_t parent_pid;
    enum process_state state;
    uint64_t exit_code;
    uint8_t  exited_by_fault;

    /* The process owns its address space and the physical pages in it. */
    struct vmm_space space;
    uint64_t code_phys;
    uint64_t data_phys;
    uint64_t stack_phys;

    /* Stage 1: one process has exactly one (main) thread. */
    struct task *thread;
};

int process_system_init(void);
/*
 * Spawn a user process from the built-in user_init blob. user_arg is passed
 * in RDI to the user entry. Returns the new pid or -1.
 */
int process_spawn(uint64_t user_arg, uint64_t *pid);
/* Terminate the current process (called from the exit syscall). */
void process_exit(uint64_t exit_code);
/*
 * Kill the current process after a contained user-mode fault. rip and
 * vector are diagnostic only.
 */
void process_user_fault(uint64_t rip, uint64_t vector);
/*
 * Fully release a zombie process: address space (page tables + owned
 * physical pages), PCID, and the process slot. Returns -1 if the process
 * is not a zombie.
 */
int process_reap(uint64_t pid);
/*
 * Block the calling kernel task until any process becomes a zombie or the
 * tick budget expires. Returns 0 when a zombie exists, -1 on timeout.
 * Stage 1 policy: cooperative yield polling; a wait queue with deadlines
 * is a later refinement.
 */
int process_wait_ticks(uint64_t ticks);
struct process *process_find(uint64_t pid);
uint64_t process_zombie_count(void);
/* pid owning a physical page (via its address space), or 0. */
uint64_t process_phys_owner(uint64_t physical);

#ifdef ZEROOS_TEST_FAULTS
int process_test_discard(uint64_t pid);
#endif
#endif
