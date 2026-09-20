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

    for (int i = 0; i < ZEROOS_MAX_PROCESSES; ++i) {
        if (processes[i].state == PROCESS_UNUSED) {
            p = &processes[i];
            break;
        }
    }
    if (!p)
        goto out;

    uint16_t pcid = vmm_pcid_alloc();
    if (!pcid)
        goto out;
    p->space.pcid = pcid;
    p->space.has_pcid = 1;

    if (vmm_space_create(&p->space) != 0) {
        vmm_pcid_free(pcid);
        goto out;
    }

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

    if (vmm_space_map_page(&p->space, ZEROOS_USER_CODE_VA, p->code_phys,
                           VMM_USER) != 0)
        goto fail_space;
    if (vmm_space_map_page(&p->space, ZEROOS_USER_DATA_VA, p->data_phys,
                           VMM_USER | VMM_WRITABLE | VMM_NO_EXECUTE) != 0)
        goto fail_space;
    if (vmm_space_map_page(&p->space, ZEROOS_USER_STACK_VA, p->stack_phys,
                           VMM_USER | VMM_WRITABLE | VMM_NO_EXECUTE) != 0)
        goto fail_space;

    /*
     * Copy the user program into the code page through the kernel's
     * identity mapping. The page is executable only through the process
     * address space (the identity mapping is non-executable).
     */
    uint64_t blob_size = (uint64_t)user_init_end - (uint64_t)user_init_code;
    if (blob_size > ZEROOS_PAGE_SIZE)
        goto fail_space;
    for (uint64_t i = 0; i < blob_size; ++i)
        ((uint8_t *)p->code_phys)[i] = user_init_code[i];

    p->pid = next_pid++;
    p->parent_pid = 0;
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
    if (!thread)
        goto fail_space;
    thread->process = p;
    p->thread = thread;

    p->state = PROCESS_RUNNING;
    result = 0;

fail_space:
    if (result != 0) {
        vmm_space_destroy(&p->space);
        if (p->space.has_pcid)
            vmm_pcid_free(p->space.pcid);
        process_reset(p);
    }
out:
    spin_unlock_irqrestore(&process_lock, flags);
    if (pid && result == 0)
        *pid = p->pid;
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

        /*
         * Release the address space: page-table pages, every owned data
         * page (code/data/stack), and the PCID. The shared kernel slot-0
         * mapping is untouched.
         */
        vmm_space_destroy(&p->space);
        if (p->space.has_pcid)
            vmm_pcid_free(p->space.pcid);
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
