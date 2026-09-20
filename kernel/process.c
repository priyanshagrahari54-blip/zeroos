#include "process.h"
#include "task.h"
#include "memory.h"
#include "sync.h"
#include "timer.h"
#include "elf.h"

extern void serial_write_public(const char *text);
extern char user_init_code[];
extern char user_init_end[];

static struct process processes[ZEROOS_MAX_PROCESSES];
static struct spinlock process_lock;
static uint64_t next_pid;
static int process_ready;

static struct process *process_find_locked(uint64_t pid) {
    for (int i = 0; i < ZEROOS_MAX_PROCESSES; ++i) {
        if (processes[i].state != PROCESS_UNUSED && processes[i].pid == pid)
            return &processes[i];
    }
    return 0;
}

static uint64_t process_zombie_count_locked(void) {
    uint64_t count = 0;
    for (int i = 0; i < ZEROOS_MAX_PROCESSES; ++i)
        if (processes[i].state == PROCESS_ZOMBIE)
            ++count;
    return count;
}

static void process_reset(struct process *p) {
    for (unsigned i = 0; i < sizeof(*p) / sizeof(uint8_t); ++i)
        ((uint8_t *)p)[i] = 0;
    p->state = PROCESS_UNUSED;
}

uint64_t process_phys_owner(uint64_t physical) {
    uint64_t flags = spin_lock_irqsave(&process_lock);
    uint64_t owner = 0;
    for (int i = 0; i < ZEROOS_MAX_PROCESSES; ++i) {
        struct process *p = &processes[i];
        if (p->state == PROCESS_UNUSED)
            continue;
        struct vmm_owned_page *cursor;
        for (cursor = p->space.owned_pages; cursor; cursor = cursor->next)
            if (cursor->physical == physical)
                { owner = p->pid; goto out; }
    }
out:
    spin_unlock_irqrestore(&process_lock, flags);
    return owner;
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

    uint64_t blob_size = (uint64_t)user_init_end - (uint64_t)user_init_code;
    if (blob_size > ZEROOS_PAGE_SIZE)
        goto fail_space;

    /*
     * The built-in test program is now fed through the same ELF validation
     * and transactional segment loader that future filesystem executables
     * will use. It is wrapped as one RX PT_LOAD at the fixed Stage-1 code VA;
     * no executable page is published until validation has succeeded.
     */
    uint8_t builtin_elf[2 * ZEROOS_PAGE_SIZE];
    for (uint64_t i=0;i<sizeof(builtin_elf);++i) builtin_elf[i]=0;
    struct elf64_ehdr *eh=(struct elf64_ehdr *)builtin_elf;
    struct elf64_phdr *ph=(struct elf64_phdr *)(builtin_elf+sizeof(*eh));
    eh->ident[0]=0x7f; eh->ident[1]='E'; eh->ident[2]='L'; eh->ident[3]='F';
    eh->ident[4]=ZEROOS_ELF64_CLASS; eh->ident[5]=ZEROOS_ELF64_DATA_LSB;
    eh->ident[6]=ZEROOS_ELF_VERSION_CURRENT;
    eh->type=ZEROOS_ELF_TYPE_EXEC; eh->machine=ZEROOS_ELF_MACHINE_X86_64;
    eh->version=ZEROOS_ELF_VERSION_CURRENT; eh->entry=ZEROOS_USER_CODE_VA;
    eh->phoff=sizeof(*eh); eh->ehsize=sizeof(*eh);
    eh->phentsize=sizeof(*ph); eh->phnum=1;
    ph->type=ZEROOS_PT_LOAD; ph->flags=ZEROOS_PF_R|ZEROOS_PF_X;
    ph->offset=ZEROOS_PAGE_SIZE; ph->vaddr=ZEROOS_USER_CODE_VA;
    ph->filesz=blob_size; ph->memsz=ZEROOS_PAGE_SIZE; ph->align=ZEROOS_PAGE_SIZE;
    for (uint64_t i=0;i<blob_size;++i)
        builtin_elf[ZEROOS_PAGE_SIZE+i]=user_init_code[i];

    struct elf_image elf_image;
    if (elf64_load_image(builtin_elf,ZEROOS_PAGE_SIZE+blob_size,
                         &p->space,&elf_image)!=0)
        goto fail_space;
    /*
     * Mapping existence is authoritative; physical address zero is a valid
     * managed page and must never be used as an "unmapped" sentinel.
     * Mark ownership before translation so a valid physical-zero mapping can
     * never be double-freed by the rollback path.
     */
    if (!vmm_space_is_mapped(&p->space, ZEROOS_USER_CODE_VA))
        goto fail_space;
    code_mapped=1;
    p->code_phys=vmm_space_translate(&p->space,ZEROOS_USER_CODE_VA);

    void *data_page=page_alloc_zero();
    void *stack_page=page_alloc_zero();
    if (!data_page || !stack_page) {
        if (data_page) page_free(data_page);
        if (stack_page) page_free(stack_page);
        goto fail_space;
    }
    p->data_phys=(uint64_t)data_page;
    p->stack_phys=(uint64_t)stack_page;

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
        /* A newly created runnable thread is not owned by the process layer
         * until publication succeeds; discard it before destroying the space. */
        if (tid) {
            (void)task_discard_new(tid);
            tid=0;
        }
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
        if (p->thread && p->thread->process == p &&
            task_reap_finished(p->thread->id)!=0) break;

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
    uint64_t flags = spin_lock_irqsave(&process_lock);
    struct process *result = process_find_locked(pid);
    spin_unlock_irqrestore(&process_lock, flags);
    return result;
}

uint64_t process_zombie_count(void) {
    uint64_t flags = spin_lock_irqsave(&process_lock);
    uint64_t count = process_zombie_count_locked();
    spin_unlock_irqrestore(&process_lock, flags);
    return count;
}

#ifdef ZEROOS_TEST_FAULTS
/* Only the test builder can discard a newly published, never-run process. */
int process_test_discard(uint64_t pid) {
    uint64_t flags=spin_lock_irqsave(&process_lock);
    struct process *p=process_find_locked(pid);
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
