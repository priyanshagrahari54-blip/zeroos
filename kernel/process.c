#include "process.h"
#include "task.h"
#include "memory.h"
#include "sync.h"
#include "timer.h"

extern void serial_write_public(const char *text);
extern char user_init_code[];
extern char user_init_end[];

static struct process processes[ZEROOS_MAX_PROCESSES];
static struct spinlock process_lock;
static uint64_t next_pid;
static int process_ready;

static void process_reset(struct process *p) {
    for (unsigned i = 0; i < sizeof(*p) / sizeof(uint8_t); ++i)
        ((uint8_t *)p)[i] = 0;
    p->state = PROCESS_UNUSED;
}

uint64_t process_phys_owner(uint64_t physical) {
    for (int i = 0; i < ZEROOS_MAX_PROCESSES; ++i) {
        struct process *p = &processes[i];
        if (p->state == PROCESS_UNUSED)
            continue;
        struct vmm_owned_page *cursor;
        for (cursor = p->space.owned_pages; cursor; cursor = cursor->next)
            if (cursor->physical == physical)
                return p->pid;
    }
    return 0;
}

int process_system_init(void) {
    if (process_ready)
        return 0;
    for (int i = 0; i < ZEROOS_MAX_PROCESSES; ++i)
        process_reset(&processes[i]);
    spinlock_init(&process_lock);
    next_pid = 1;
    process_ready = 1;
    return 0;
}

int process_spawn(uint64_t user_arg, uint64_t *pid) {
    uint64_t flags = spin_lock_irqsave(&process_lock);
    struct process *p = 0;
    uint64_t tid = 0;
    int result = -1;
    int code_mapped=0, data_mapped=0, stack_mapped=0;

    for (int i = 0; i < ZEROOS_MAX_PROCESSES; ++i) {
        if (processes[i].state == PROCESS_UNUSED) {
            p = &processes[i];
            break;
        }
    }
    if (!p)
        goto out;

    if (vmm_space_create(&p->space) != 0) goto out;

    p->code_phys = (uint64_t)page_alloc_zero();
    p->data_phys = (uint64_t)page_alloc_zero();
    p->stack_phys = (uint64_t)page_alloc_zero();
    if (!p->code_phys || !p->data_phys || !p->stack_phys)
        goto fail_space;

    /*
     * Freshly allocated pages must not be claimed by any other live space;
     * a hit here means the physical allocator handed out a page that was
     * never released from an address space.
     */
    if (process_phys_owner(p->code_phys) ||
        process_phys_owner(p->data_phys) ||
        process_phys_owner(p->stack_phys)) {
        serial_write_public("ZEROOS PANIC: process page already owned by another space.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    /*
     * Copy the user program into the code page through the kernel's
     * writable NX identity alias BEFORE publishing its RX user mapping.
     * vmm_space_map_page seals that alias read-only before enabling execute.
     */
    uint64_t blob_size = (uint64_t)user_init_end - (uint64_t)user_init_code;
    if (blob_size > ZEROOS_PAGE_SIZE)
        goto fail_space;
    for (uint64_t i = 0; i < blob_size; ++i)
        ((uint8_t *)p->code_phys)[i] = user_init_code[i];

    if (vmm_space_map_page(&p->space, ZEROOS_USER_CODE_VA, p->code_phys,
                           VMM_USER) != 0)
        goto fail_space;
    code_mapped=1;
    if (vmm_space_map_page(&p->space, ZEROOS_USER_DATA_VA, p->data_phys,
                           VMM_USER | VMM_WRITABLE | VMM_NO_EXECUTE) != 0)
        goto fail_space;
    data_mapped=1;
    if (vmm_space_map_page(&p->space, ZEROOS_USER_STACK_VA, p->stack_phys,
                           VMM_USER | VMM_WRITABLE | VMM_NO_EXECUTE) != 0)
        goto fail_space;

    stack_mapped=1;

    p->pid = next_pid++;
    struct task *parent=task_current();
    p->parent_pid = parent && parent->process ? parent->process->pid : 0;
    p->exit_code = 0;
    p->exited_by_fault = 0;

    /*
     * Entry RSP = top of the stack page minus 8, satisfying the syscall
     * ABI's RSP 8 mod 16 requirement before the user's first call.
     */
    if (task_create_user(ZEROOS_USER_CODE_VA,
                         ZEROOS_USER_STACK_VA + ZEROOS_PAGE_SIZE - 8ULL,
                         user_arg, &tid) != 0)
        goto fail_space;

    struct task *thread = task_find_by_id(tid);
    if (!thread) {
        serial_write_public("ZEROOS PANIC: newly created thread missing.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    thread->process = p;
    p->thread = thread;

    p->state = PROCESS_RUNNING;
    result = 0;

fail_space:
    if (result != 0) {
        if (!code_mapped && p->code_phys) page_free((void *)p->code_phys);
        if (!data_mapped && p->data_phys) page_free((void *)p->data_phys);
        if (!stack_mapped && p->stack_phys) page_free((void *)p->stack_phys);
        vmm_space_destroy(&p->space);
        process_reset(p);
    }
out:
    if (pid && result == 0) *pid = p->pid;
    spin_unlock_irqrestore(&process_lock, flags);
    return result;
}

void process_exit(uint64_t exit_code) {
    struct task *task = task_current();
    struct process *p = task ? task->process : 0;

    if (!p)
        return;

    uint64_t flags = spin_lock_irqsave(&process_lock);
    if (p->state == PROCESS_RUNNING) {
        p->state = PROCESS_ZOMBIE;
        p->exit_code = exit_code & 0xffULL;
        p->exited_by_fault = 0;
    }
    spin_unlock_irqrestore(&process_lock, flags);

    /*
     * Never returns: the task becomes a zombie and the scheduler switches
     * to another task (which also restores the kernel CR3). The process's
     * resources stay allocated until process_reap().
     */
    task_exit();
    for (;;) __asm__ volatile ("cli; hlt");
}

void process_user_fault(uint64_t rip, uint64_t vector) {
    struct task *task = task_current();
    struct process *p = task ? task->process : 0;

    if (!p)
        return;

    uint64_t flags = spin_lock_irqsave(&process_lock);
    if (p->state == PROCESS_RUNNING) {
        p->state = PROCESS_ZOMBIE;
        p->exit_code = (vector & 0xffULL) | 0x100ULL;
        p->exited_by_fault = 1;
    }
    spin_unlock_irqrestore(&process_lock, flags);

    /*
     * The task is marked zombie here (not via task_exit) because we are in
     * interrupt context: the IRQ-exit path in task.c performs the actual
     * switch away from the dying task after this returns.
     */
    task->state = TASK_ZOMBIE;
    task->need_resched = 1;
    (void)rip;
}

int process_reap(uint64_t pid) {
    uint64_t flags = spin_lock_irqsave(&process_lock);
    int result = -1;

    for (int i = 0; i < ZEROOS_MAX_PROCESSES; ++i) {
        struct process *p = &processes[i];
        if (p->state != PROCESS_ZOMBIE || p->pid != pid)
            continue;
        if (task_current() && task_current()->process == p) break;
        if (p->thread && p->thread->process == p) p->thread->process=0;

        /*
         * Release the address space: page-table pages, every owned data
         * page (code/data/stack), and the PCID. The shared kernel slot-0
         * mapping is untouched.
         */
        vmm_space_destroy(&p->space);
        process_reset(p);
        result = 0;
        break;
    }

    spin_unlock_irqrestore(&process_lock, flags);
    return result;
}

int process_wait_ticks(uint64_t ticks) {
    uint64_t deadline = timer_ticks() + ticks;

    for (;;) {
        if (process_zombie_count() != 0)
            return 0;
        if (timer_ticks() >= deadline)
            return -1;
        task_yield();
    }
}

struct process *process_find(uint64_t pid) {
    for (int i = 0; i < ZEROOS_MAX_PROCESSES; ++i) {
        if (processes[i].state != PROCESS_UNUSED &&
            processes[i].pid == pid)
            return &processes[i];
    }
    return 0;
}

uint64_t process_zombie_count(void) {
    uint64_t count = 0;
    for (int i = 0; i < ZEROOS_MAX_PROCESSES; ++i)
        if (processes[i].state == PROCESS_ZOMBIE)
            ++count;
    return count;
}

#ifdef ZEROOS_TEST_FAULTS
/* Only the test builder can discard a newly published, never-run process. */
int process_test_discard(uint64_t pid) {
    uint64_t flags=spin_lock_irqsave(&process_lock);
    struct process *p=process_find(pid);
    int result=-1;
    if (p && p->state==PROCESS_RUNNING && p->thread &&
        task_discard_new(p->thread->id)==0) {
        vmm_space_destroy(&p->space);
        process_reset(p);
        result=0;
    }
    spin_unlock_irqrestore(&process_lock,flags);
    return result;
}
#endif
