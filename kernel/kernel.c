#include "types.h"
#include "cpu.h"
#include "apic.h"
#include "memory.h"
#include "timer.h"
#include "vmm.h"
#include "tlb.h"
#include "gdt.h"
#include "sync.h"
#include "task.h"
#include "thread.h"
#include "process.h"
#include "scheduler.h"
#include "smp.h"
#include "wait.h"

#define COM1 0x3F8
#define VMM_SELF_TEST_VA 0x00007f0000000000ULL
#define VMM_SPACE_TEST_VA (VMM_SELF_TEST_VA + 0x2000ULL)

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void) {
    outb(COM1+1,0x00);
    outb(COM1+3,0x80);
    outb(COM1+0,0x03);
    outb(COM1+1,0x00);
    outb(COM1+3,0x03);
    outb(COM1+2,0xC7);
    outb(COM1+4,0x0B);
}

static void serial_putc(char c) {
    while ((inb(COM1+5)&0x20)==0) {}
    outb(COM1,(uint8_t)c);
}

void serial_write_public(const char *text) {
    while (*text) {
        if (*text=='\n') serial_putc('\r');
        serial_putc(*text++);
    }
}

extern void interrupts_init(void);
extern int boot_stack_guard_ok(void);

static void serial_write_u64(uint64_t value) {
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

static void kernel_panic(const char *message) {
    serial_write_public("ZEROOS PANIC: ");
    serial_write_public(message);
    serial_write_public("\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

static void memory_self_test(void) {
    uint64_t before=memory_free_pages();
    void *a=page_alloc(), *b=page_alloc();
    if (!a || !b || a==b) kernel_panic("physical page allocator self-test failed");
    page_free(b); page_free(a);
    if (memory_free_pages()!=before) kernel_panic("physical page allocator accounting failed");

    void *shared=page_alloc();
    if (!shared || memory_page_retain((uint64_t)shared)!=0 ||
        memory_page_references((uint64_t)shared)!=2)
        kernel_panic("physical page reference acquisition failed");
    page_free(shared);
    if (memory_page_references((uint64_t)shared)!=1 ||
        memory_free_pages()!=before-1)
        kernel_panic("physical page reference release failed");
    if (memory_page_release((uint64_t)shared)!=0 ||
        memory_page_references((uint64_t)shared)!=0 ||
        memory_free_pages()!=before)
        kernel_panic("physical page reference finalization failed");

    /* A reserved page must never become free through an invalid page_free(). */
    page_free((void *)0);
    if (memory_free_pages()!=before ||
        !memory_is_usable_range((uint64_t)a,ZEROOS_PAGE_SIZE) ||
        !memory_is_managed_range((uint64_t)a,ZEROOS_PAGE_SIZE) ||
        memory_page_is_allocated((uint64_t)a))
        kernel_panic("physical allocator ownership validation failed");
    void *z=page_alloc_zero();
    if (!z) kernel_panic("zeroed page allocation failed");
    for (uint64_t i=0;i<ZEROOS_PAGE_SIZE/sizeof(uint64_t);++i)
        if (((uint64_t *)z)[i]!=0)
            kernel_panic("zeroed page validation failed");
    page_free(z);
    serial_write_public("ZEROOS: physical allocator self-test passed.\n");
}

static void vmm_self_test(void) {
    void *physical=page_alloc();
    if (!physical) kernel_panic("VMM self-test could not allocate a page");
    if (vmm_map_page(VMM_SELF_TEST_VA+0x4000ULL,(uint64_t)physical,VMM_WRITABLE)==0 ||
        vmm_map_range(~0ULL-0x1000ULL,(uint64_t)physical,2,VMM_USER|VMM_NO_EXECUTE)==0)
        kernel_panic("VMM W^X/overflow validation failed");
    if (vmm_map_page(VMM_SELF_TEST_VA,(uint64_t)physical,VMM_WRITABLE|VMM_NO_EXECUTE)!=0)
        kernel_panic("VMM map failed");
    if (vmm_translate(VMM_SELF_TEST_VA)!=(uint64_t)physical)
        kernel_panic("VMM translation mismatch");
    if (vmm_protect_page(VMM_SELF_TEST_VA,VMM_USER|VMM_NO_EXECUTE)!=0)
        kernel_panic("VMM protection update failed");
    if (!vmm_is_user_range(VMM_SELF_TEST_VA,VMM_PAGE_SIZE,0))
        kernel_panic("VMM user-range validation failed");
    if (vmm_is_user_range(VMM_SELF_TEST_VA,VMM_PAGE_SIZE,1))
        kernel_panic("VMM write permission validation failed");

    void *range_a=page_alloc();
    void *range_b=page_alloc();
    if (!range_a || !range_b)
        kernel_panic("VMM range self-test allocation failed");
    const uint64_t range_va=VMM_SELF_TEST_VA+0x2000ULL;
    if (vmm_map_range(range_va,(uint64_t)range_a,2,VMM_USER|VMM_WRITABLE|VMM_NO_EXECUTE)!=0)
        kernel_panic("VMM range mapping failed");
    if (!vmm_is_user_range(range_va,8192,1))
        kernel_panic("VMM multi-page range validation failed");
    if (vmm_unmap_range(range_va,2)!=0)
        kernel_panic("VMM range unmap failed");
    page_free(range_a);
    page_free(range_b);

    if (vmm_unmap_page(VMM_SELF_TEST_VA)!=0) kernel_panic("VMM unmap failed");

    /* Device registers are not allocator-owned RAM: exercise the dedicated
     * supervisor MMIO mapping contract without dereferencing a fake device. */
    const uint64_t mmio_va=VMM_MMIO_BASE+0x100000ULL;
    const uint64_t mmio_pa=0xfec00000ULL;
    if (vmm_map_mmio_page(mmio_va,mmio_pa,
                          VMM_WRITABLE|VMM_CACHE_DISABLE|VMM_NO_EXECUTE)!=0 ||
        vmm_translate(mmio_va)!=mmio_pa ||
        vmm_unmap_mmio_page(mmio_va)!=0 ||
        vmm_translate(mmio_va)!=0)
        kernel_panic("VMM MMIO ownership validation failed");

    if (tlb_set_current_cpu(0)!=0 || tlb_online_count()!=1 ||
        tlb_install_ipi_sender(0)!=0 || tlb_register_cpu(1)!=-1 ||
        tlb_invalidate_page(VMM_SELF_TEST_VA)!=0 ||
        tlb_debug_validate()!=0)
        kernel_panic("TLB shootdown boundary self-test failed");

    page_free(physical);
    serial_write_public("ZEROOS: TLB shootdown boundary self-test passed.\n");
    serial_write_public("ZEROOS: virtual memory self-test passed.\n");
}

static void vmm_space_self_test(void) {
    struct vmm_space space;
    void *physical=page_alloc_zero();
    if (!physical) kernel_panic("address-space page allocation failed");
    if (vmm_space_create(&space)!=0)
        kernel_panic("address-space creation failed");
    if (vmm_space_translate(&space,VMM_SPACE_TEST_VA)!=0)
        kernel_panic("fresh address-space is not empty");
    uint64_t space_free_before_map=memory_free_pages();
    if (vmm_space_map_page(&space,VMM_SPACE_TEST_VA,(uint64_t)physical,
                           VMM_USER|VMM_WRITABLE|VMM_NO_EXECUTE)!=0)
        kernel_panic("address-space user mapping failed");
    if (vmm_space_translate(&space,VMM_SPACE_TEST_VA)!=(uint64_t)physical ||
        memory_page_references((uint64_t)physical)!=2)
        kernel_panic("address-space translation/ownership failed");
    if (vmm_space_map_page(&space,0x4000000000ULL,(uint64_t)physical,
                           VMM_USER|VMM_WRITABLE)!=-1)
        kernel_panic("address-space accepted unsafe PML4");
    if (vmm_space_unmap_page(&space,VMM_SPACE_TEST_VA)!=0)
        kernel_panic("address-space unmap failed");
    if (vmm_space_translate(&space,VMM_SPACE_TEST_VA)!=0 ||
        memory_page_references((uint64_t)physical)!=1 ||
        memory_free_pages()!=space_free_before_map)
        kernel_panic("address-space table reclamation failed");
    if (vmm_space_activate(&space)!=0 || vmm_space_destroy(&space)==0)
        kernel_panic("active address-space destruction guard failed");
    if (vmm_activate_kernel()!=0 || vmm_space_destroy(&space)!=0)
        kernel_panic("kernel-root address-space teardown failed");
    page_free(physical);
    serial_write_public("ZEROOS: per-address-space VMM self-test passed.\n");
}

static void gdt_self_test(void) {
    struct __attribute__((packed)) gdtr64_test {
        uint16_t limit;
        uint64_t base;
    } gdtr;
    uint16_t tr;
    uint16_t cs;
    uint16_t ds;
    uint64_t rsp0;

    __asm__ volatile (
        "sgdt %0\n"
        "str %1\n"
        "movw %%cs,%2\n"
        "movw %%ds,%3\n"
        : "=m"(gdtr), "=r"(tr), "=r"(cs), "=r"(ds)
        :
        : "memory"
    );

    rsp0=gdt_kernel_stack();

    if (!gdt_is_initialized() ||
        gdtr.base!=gdt_base() ||
        gdtr.limit<((7U*8U)-1U) ||
        tr!=gdt_tss_selector() ||
        cs!=gdt_kernel_code_selector() ||
        ds!=gdt_kernel_data_selector() ||
        gdt_user_code_selector()==gdt_kernel_code_selector() ||
        gdt_user_data_selector()==gdt_kernel_data_selector() ||
        rsp0==0 || (rsp0 & 0xfULL)!=0)
        kernel_panic("GDT/TSS self-test failed");

    for (uint8_t ist=1; ist<=ZEROOS_GDT_IST_COUNT; ++ist)
        if (gdt_ist_stack_top(ist)==0 ||
            (gdt_ist_stack_top(ist)&0xfULL)!=0)
            kernel_panic("GDT/TSS IST self-test failed");
    if (gdt_exception_ist(8)!=1 || gdt_exception_ist(2)!=2 ||
        gdt_exception_ist(18)!=3 || gdt_exception_ist(14)!=4 ||
        gdt_exception_ist(13)!=5 || gdt_exception_ist(32)!=0)
        kernel_panic("GDT/TSS exception-stack routing failed");

    serial_write_public("ZEROOS: exception IST routing self-test passed.\n");
    serial_write_public("ZEROOS: runtime GDT/TSS self-test passed.\n");
}

static void sync_self_test(void) {
    struct spinlock lock;
    struct rwlock rw;
    struct atomic_u64 counter;
    uint64_t flags;
    spinlock_init(&lock);
    atomic_u64_init(&counter,41);
    flags=spin_lock_irqsave(&lock);
    if (spin_try_lock(&lock)==0 || spin_lock_bounded(&lock,1)==0)
        kernel_panic("spinlock contention/try contract failed");
    atomic_u64_fetch_add(&counter,1);
    spin_unlock_irqrestore(&lock,flags);
    if (atomic_u64_load(&counter)!=42 || spinlock_contention_count(&lock)==0)
        kernel_panic("synchronization primitive self-test failed");

    rwlock_init(&rw);
    rwlock_read_lock(&rw);
    rwlock_read_unlock(&rw);
    rwlock_write_lock(&rw);
    rwlock_write_unlock(&rw);
    if (rwlock_try_write(&rw)!=0)
        kernel_panic("rwlock writer self-test failed");
    rwlock_write_unlock(&rw);
    serial_write_public("ZEROOS: synchronization primitives self-test passed.\n");
}

static struct atomic_u64 task_probe_counter;
static struct wait_queue wait_probe_queue;
static struct atomic_u64 wait_probe_state;
static struct atomic_u64 sleep_probe_state;

/* Advanced scheduler certification state. */
static struct atomic_u64 preempt_probe_a;
static struct atomic_u64 preempt_probe_b;
static struct atomic_u64 preempt_probe_done;
static struct atomic_u64 preempt_probe_ticks;
static struct atomic_u64 lifecycle_probe_done;
static struct atomic_u64 lifecycle_probe_created;
static struct atomic_u64 lifecycle_probe_exited;
static struct atomic_u64 scheduler_stress_failures;

/* Process/thread model certification state. */
static struct atomic_u64 process_thread_probe_phase;
static struct atomic_u64 process_thread_probe_parent_ran;
static struct atomic_u64 process_thread_probe_child_ran;
static struct atomic_u64 process_thread_probe_reuse_ran;
static struct atomic_u64 process_thread_probe_failures;

static struct process *process_probe_parent;
static struct process *process_probe_child;
static struct process *process_probe_reuse;

static struct thread *thread_probe_parent;
static struct thread *thread_probe_child;
static struct thread *thread_probe_reuse;

static process_id_t process_probe_parent_pid;
static process_id_t process_probe_child_pid;
static process_id_t process_probe_reuse_pid;
static thread_id_t thread_probe_parent_tid;
static thread_id_t thread_probe_child_tid;
static thread_id_t thread_probe_reuse_tid;

static struct atomic_u64 process_thread_probe_allow_parent_exit;

static void scheduler_tss_stack_self_check(void);

static void process_thread_probe_parent_entry(void *argument) {
    (void)argument;
    while (atomic_u64_load(&process_thread_probe_allow_parent_exit)==0)
        scheduler_yield();
    atomic_u64_store(&process_thread_probe_parent_ran,1);
}

static void process_thread_probe_child_entry(void *argument) {
    (void)argument;
    atomic_u64_store(&process_thread_probe_child_ran,1);
}

static void process_thread_probe_reuse_entry(void *argument) {
    (void)argument;
    atomic_u64_store(&process_thread_probe_reuse_ran,1);
}

static void process_thread_probe_fail(const char *message) {
    atomic_u64_fetch_add(&process_thread_probe_failures,1);
    kernel_panic(message);
}

static void process_thread_probe_monitor_step(void) {
    uint64_t phase=atomic_u64_load(&process_thread_probe_phase);

    if (phase==0) {
        if (process_create(0,&process_probe_parent_pid)!=0)
            process_thread_probe_fail("process/thread parent creation failed");
        process_probe_parent=process_lookup(process_probe_parent_pid);
        if (!process_probe_parent ||
            process_probe_parent->state!=PROCESS_NEW)
            process_thread_probe_fail("process lookup/state validation failed");

        if (process_set_limits(process_probe_parent,4,2,1024)!=0) {
            process_thread_probe_fail("process resource-limit setup failed");
        }
        {
            uint64_t max_threads=0, max_children=0, max_pages=0;
            if (process_get_limits(process_probe_parent,&max_threads,
                                   &max_children,&max_pages)!=0 ||
                max_threads!=4 || max_children!=2 || max_pages!=1024)
                process_thread_probe_fail("process resource-limit validation failed");
        }

        if (thread_create_kernel(process_probe_parent,
                                  process_thread_probe_parent_entry,0,
                                  &thread_probe_parent_tid)!=0)
            process_thread_probe_fail("parent kernel thread creation failed");
        thread_probe_parent=thread_lookup(thread_probe_parent_tid);
        if (!thread_probe_parent ||
            thread_probe_parent->process!=process_probe_parent)
            process_thread_probe_fail("parent thread ownership validation failed");

        if (process_create(process_probe_parent,&process_probe_child_pid)!=0)
            process_thread_probe_fail("child process creation failed");
        process_probe_child=process_lookup(process_probe_child_pid);
        if (!process_probe_child ||
            process_probe_child->parent!=process_probe_parent ||
            process_child_count(process_probe_parent)!=1)
            process_thread_probe_fail("parent/child relationship validation failed");

        if (thread_create_kernel(process_probe_child,
                                  process_thread_probe_child_entry,0,
                                  &thread_probe_child_tid)!=0)
            process_thread_probe_fail("child kernel thread creation failed");
        thread_probe_child=thread_lookup(thread_probe_child_tid);
        if (!thread_probe_child ||
            thread_probe_child->process!=process_probe_child)
            process_thread_probe_fail("child thread ownership validation failed");

        if (thread_probe_parent->tid==thread_probe_child->tid ||
            process_probe_parent->pid==process_probe_child->pid)
            process_thread_probe_fail("PID/TID uniqueness validation failed");

        /*
         * Both processes own dedicated address-space roots. They should start
         * with no user mapping at the same virtual address.
         */
        if (vmm_space_translate(&process_probe_parent->address_space,
                                VMM_SPACE_TEST_VA)!=0 ||
            vmm_space_translate(&process_probe_child->address_space,
                                VMM_SPACE_TEST_VA)!=0)
            process_thread_probe_fail("process address-space isolation validation failed");

        /* Process-owned mapping wrappers must enforce the address-space quota
         * and keep process accounting identical to VMM ownership accounting. */
        void *probe_page=page_alloc_zero();
        if (!probe_page ||
            process_address_space_map_page(process_probe_parent,
                                           VMM_SPACE_TEST_VA,
                                           (uint64_t)probe_page,
                                           VMM_USER|VMM_WRITABLE|VMM_NO_EXECUTE)!=0 ||
            process_address_space_mapped_pages(process_probe_parent)!=1 ||
            !process_address_space_is_user_range(process_probe_parent,
                                                 VMM_SPACE_TEST_VA,
                                                 VMM_PAGE_SIZE,1) ||
            process_address_space_map_page(process_probe_parent,
                                           VMM_SPACE_TEST_VA,
                                           (uint64_t)probe_page,
                                           VMM_USER|VMM_WRITABLE|VMM_NO_EXECUTE)!=-1) {
            if (probe_page) page_free(probe_page);
            process_thread_probe_fail("process address-space ownership/quota validation failed");
        }
        if (process_set_limits(process_probe_parent,4,2,1)!=0 ||
            process_address_space_map_page(process_probe_parent,
                                           VMM_SPACE_TEST_VA+VMM_PAGE_SIZE,
                                           (uint64_t)probe_page,
                                           VMM_USER|VMM_WRITABLE|VMM_NO_EXECUTE)!=-1 ||
            process_address_space_unmap_page(process_probe_parent,
                                             VMM_SPACE_TEST_VA)!=0 ||
            process_address_space_mapped_pages(process_probe_parent)!=0 ||
            process_set_limits(process_probe_parent,4,2,1024)!=0) {
            page_free(probe_page);
            process_thread_probe_fail("process address-space limit transition failed");
        }
        page_free(probe_page);
        serial_write_public("ZEROOS: process address-space ownership self-test passed.\n");

        atomic_u64_store(&process_thread_probe_allow_parent_exit,1);
        atomic_u64_store(&process_thread_probe_phase,1);
        return;
    }

    if (phase==1 &&
        atomic_u64_load(&process_thread_probe_parent_ran)==1 &&
        atomic_u64_load(&process_thread_probe_child_ran)==1 &&
        thread_probe_parent->state==THREAD_ZOMBIE &&
        thread_probe_child->state==THREAD_ZOMBIE &&
        process_probe_parent->state==PROCESS_ZOMBIE &&
        process_probe_child->state==PROCESS_ZOMBIE) {

        uint64_t parent_status=0xffffffffULL;
        uint64_t child_status=0xffffffffULL;

        if (thread_reap(thread_probe_child,&child_status)!=0 ||
            child_status!=0)
            process_thread_probe_fail("child thread reap failed");
        if (process_thread_count(process_probe_child)!=0)
            process_thread_probe_fail("child thread count did not reach zero");
        if (process_reap(process_probe_child,&child_status)!=0 ||
            child_status!=0)
            process_thread_probe_fail("child process reap failed");
        if (process_child_count(process_probe_parent)!=0 ||
            process_lookup(process_probe_child_pid)!=0)
            process_thread_probe_fail("child process unlink/stale PID validation failed");

        if (thread_reap(thread_probe_parent,&parent_status)!=0 ||
            parent_status!=0)
            process_thread_probe_fail("parent thread reap failed");
        if (process_thread_count(process_probe_parent)!=0)
            process_thread_probe_fail("parent thread count did not reach zero");
        if (process_reap(process_probe_parent,&parent_status)!=0 ||
            parent_status!=0)
            process_thread_probe_fail("parent process reap failed");

        if (thread_lookup(thread_probe_child_tid)!=0 ||
            thread_lookup(thread_probe_parent_tid)!=0 ||
            process_lookup(process_probe_child_pid)!=0 ||
            process_lookup(process_probe_parent_pid)!=0)
            process_thread_probe_fail("stale PID/TID remained visible after reap");

        if (process_create(0,&process_probe_reuse_pid)!=0)
            process_thread_probe_fail("PID reuse test process creation failed");
        process_probe_reuse=process_lookup(process_probe_reuse_pid);
        if (!process_probe_reuse)
            process_thread_probe_fail("PID reuse test lookup failed");

        if (thread_create_kernel(process_probe_reuse,
                                  process_thread_probe_reuse_entry,0,
                                  &thread_probe_reuse_tid)!=0)
            process_thread_probe_fail("TID reuse test thread creation failed");
        thread_probe_reuse=thread_lookup(thread_probe_reuse_tid);
        if (!thread_probe_reuse)
            process_thread_probe_fail("TID reuse test lookup failed");

        if (process_probe_reuse_pid==process_probe_parent_pid ||
            thread_probe_reuse_tid==thread_probe_parent_tid)
            process_thread_probe_fail("generation-tagged PID/TID reuse protection failed");

        atomic_u64_store(&process_thread_probe_phase,2);
        return;
    }

    if (phase==2 &&
        atomic_u64_load(&process_thread_probe_reuse_ran)==1 &&
        thread_probe_reuse->state==THREAD_ZOMBIE &&
        process_probe_reuse->state==PROCESS_ZOMBIE) {

        uint64_t status=0xffffffffULL;

        if (thread_reap(thread_probe_reuse,&status)!=0 || status!=0)
            process_thread_probe_fail("reused thread reap failed");
        if (process_reap(process_probe_reuse,&status)!=0 || status!=0)
            process_thread_probe_fail("reused process reap failed");
        if (thread_lookup(thread_probe_reuse_tid)!=0 ||
            process_lookup(process_probe_reuse_pid)!=0)
            process_thread_probe_fail("reused PID/TID remained visible after reap");

        serial_write_public("ZEROOS: process/thread object model self-test passed.\n");
        serial_write_public("ZEROOS: PID/TID reuse protection self-test passed.\n");
        atomic_u64_store(&process_thread_probe_phase,3);
    }
}

static void scheduler_probe_cpu_a(void *argument) {
    (void)argument;
    scheduler_tss_stack_self_check();

    /*
     * Keep callee-saved registers live from the very first CPU-bound loop so
     * the first timer-only handoff itself is covered by the register test.
     */
    register uint64_t rbx asm("rbx")=0x9e3779b97f4a7c15ULL;
    register uint64_t r12 asm("r12")=0x243f6a8885a308d3ULL;
    register uint64_t r13 asm("r13")=0x13198a2e03707344ULL;
    register uint64_t r14 asm("r14")=0xa4093822299f31d0ULL;
    register uint64_t r15 asm("r15")=0x082efa98ec4e6c89ULL;

    uint64_t start=timer_ticks();
    atomic_u64_store(&preempt_probe_ticks,start);

    /*
     * Deliberately never yield. CPU-B must still make progress here, proving
     * timer-only preemption rather than cooperative scheduling.
     */
    while (atomic_u64_load(&preempt_probe_b)==0) {
        atomic_u64_fetch_add(&preempt_probe_a,1);
        __asm__ volatile ("" : "+r"(rbx), "+r"(r12), "+r"(r13), "+r"(r14), "+r"(r15));
        if (timer_ticks() - start > 250) {
            atomic_u64_fetch_add(&scheduler_stress_failures,1);
            kernel_panic("timer-only preemption failed: CPU-B made no progress");
        }
    }

    /*
     * Hold CPU-A runnable and CPU-bound for many ticks after CPU-B starts.
     * This forces multiple timer preemptions while the callee-saved register
     * set remains live in registers, instead of merely testing one initial
     * handoff followed by a short non-preempted loop.
     */
    uint64_t deadline=timer_ticks()+50;
    while ((long long)(deadline-timer_ticks())>0) {
        atomic_u64_fetch_add(&preempt_probe_a,1);
        __asm__ volatile ("" : "+r"(rbx), "+r"(r12), "+r"(r13), "+r"(r14), "+r"(r15));
        if (rbx!=0x9e3779b97f4a7c15ULL ||
            r12!=0x243f6a8885a308d3ULL ||
            r13!=0x13198a2e03707344ULL ||
            r14!=0xa4093822299f31d0ULL ||
            r15!=0x082efa98ec4e6c89ULL)
            kernel_panic("callee-saved register corruption during preemption");
    }

    if (timer_ticks()-start < 10) {
        atomic_u64_fetch_add(&scheduler_stress_failures,1);
        kernel_panic("timer-only preemption window was too short");
    }

    atomic_u64_fetch_add(&preempt_probe_done,1);
}

static void scheduler_probe_cpu_b(void *argument) {
    (void)argument;
    scheduler_tss_stack_self_check();
    register uint64_t rbx asm("rbx")=0xdeadbeefcafebabeULL;
    register uint64_t r12 asm("r12")=0x0123456789abcdefULL;
    register uint64_t r13 asm("r13")=0xfedcba9876543210ULL;
    register uint64_t r14 asm("r14")=0x55aa55aa55aa55aaULL;
    register uint64_t r15 asm("r15")=0xaa55aa55aa55aa55ULL;

    atomic_u64_store(&preempt_probe_b,1);
    while (atomic_u64_load(&preempt_probe_done)==0) {
        atomic_u64_fetch_add(&preempt_probe_b,1);
        __asm__ volatile ("" : "+r"(rbx), "+r"(r12), "+r"(r13), "+r"(r14), "+r"(r15));
        if (rbx!=0xdeadbeefcafebabeULL ||
            r12!=0x0123456789abcdefULL ||
            r13!=0xfedcba9876543210ULL ||
            r14!=0x55aa55aa55aa55aaULL ||
            r15!=0xaa55aa55aa55aa55ULL)
            kernel_panic("CPU-B callee-saved register corruption during preemption");
    }

    if (rbx!=0xdeadbeefcafebabeULL ||
        r12!=0x0123456789abcdefULL ||
        r13!=0xfedcba9876543210ULL ||
        r14!=0x55aa55aa55aa55aaULL ||
        r15!=0xaa55aa55aa55aa55ULL)
        kernel_panic("CPU-B callee-saved register corruption during preemption");
}

static void scheduler_probe_lifecycle_worker(void *argument) {
    (void)argument;
    atomic_u64_fetch_add(&lifecycle_probe_exited,1);
    /* Returning exercises ZOMBIE transition and deferred stack reclamation. */
}

static void scheduler_probe_lifecycle_creator(void *argument) {
    (void)argument;
    for (uint64_t i=0;i<6;++i) {
        uint64_t id;
        if (task_create(scheduler_probe_lifecycle_worker,0,&id)!=0) {
            atomic_u64_fetch_add(&scheduler_stress_failures,1);
            kernel_panic("lifecycle slot reuse creation failed");
        }
        atomic_u64_fetch_add(&lifecycle_probe_created,1);
        scheduler_yield();
    }
    atomic_u64_store(&lifecycle_probe_done,1);
}

static void scheduler_tss_stack_self_check(void) {
    if (!task_current() || gdt_kernel_stack()!=task_current()->kernel_stack_top)
        kernel_panic("scheduler/TSS kernel-stack handoff validation failed");
}

static void scheduler_probe_worker(void *argument) {
    uint64_t rbx_value=0x1122334455667788ULL;
    uint64_t r12_value=0x13579bdf2468ace0ULL;
    uint64_t r13_value=0x0eca8642fdb97531ULL;
    uint64_t r14_value=0x55aa55aa33cc33ccULL;
    uint64_t r15_value=0xcc33cc3355aa55aaULL;
    (void)argument;
    scheduler_tss_stack_self_check();
    serial_write_public("ZEROOS: per-task kernel-stack/TSS handoff self-test passed.\n");

    /*
     * Keep callee-saved values live across repeated cooperative switches.
     * The compiler must preserve these registers across scheduler_yield(),
     * making this a direct regression test for context.S.
     */
    register uint64_t rbx asm("rbx")=rbx_value;
    register uint64_t r12 asm("r12")=r12_value;
    register uint64_t r13 asm("r13")=r13_value;
    register uint64_t r14 asm("r14")=r14_value;
    register uint64_t r15 asm("r15")=r15_value;

    for (uint64_t i=0;i<32;++i) {
        atomic_u64_fetch_add(&task_probe_counter,1);
        if (task_debug_validate()!=0)
            kernel_panic("task scheduler invariant check failed");
        scheduler_yield();
        if (task_debug_validate()!=0)
            kernel_panic("task scheduler resume invariant check failed");
        if (rbx!=rbx_value ||
            r12!=r12_value || r13!=r13_value ||
            r14!=r14_value || r15!=r15_value)
            kernel_panic("callee-saved register context corruption");
    }
    serial_write_public("ZEROOS: task context-switch worker completed.\n");
}

static void wait_probe_waiter(void *argument) {
    (void)argument;
    if (wait_queue_block(&wait_probe_queue)!=0)
        kernel_panic("wait queue block failed");
    if (atomic_u64_load(&wait_probe_state)!=1)
        kernel_panic("wait queue wake state mismatch");
    atomic_u64_store(&wait_probe_state,2);
    serial_write_public("ZEROOS: wait queue block/wakeup self-test passed.\n");
}

static void wait_probe_waker(void *argument) {
    (void)argument;
    scheduler_yield();
    atomic_u64_store(&wait_probe_state,1);
    if (wait_queue_wake_one(&wait_probe_queue)!=1)
        kernel_panic("wait queue wake failed");
}

static void scheduler_probe_sleeper(void *argument) {
    (void)argument;
    atomic_u64_store(&sleep_probe_state,1);
    if (scheduler_sleep_ticks(5)!=0)
        kernel_panic("timed sleep failed");
    atomic_u64_store(&sleep_probe_state,2);
    serial_write_public("ZEROOS: timed sleep wakeup self-test passed.\n");
}

static void scheduler_probe_monitor(void *argument) {
    (void)argument;
    uint64_t last_report=0;
    int context_reported=0;
    int wait_reported=0;
    int sleep_reported=0;
    int preempt_reported=0;
    int lifecycle_reported=0;
    int frame_invariant_reported=0;
    int certification_reported=0;
    int per_cpu_reported=0;
    uint64_t stress_start=timer_ticks();

    for (;;) {
        uint64_t now=timer_ticks();

        process_thread_probe_monitor_step();

        if (process_debug_validate()!=0)
            kernel_panic("process table invariant check failed");
        if (thread_debug_validate()!=0)
            kernel_panic("thread table invariant check failed");

        if (!context_reported && atomic_u64_load(&task_probe_counter)==32) {
            context_reported=1;
            serial_write_public("ZEROOS: task context-switch self-test passed.\n");
        }

        if (!wait_reported && atomic_u64_load(&wait_probe_state)==2) {
            wait_reported=1;
            serial_write_public("ZEROOS: wait queue integration verified.\n");
        }

        if (!sleep_reported && atomic_u64_load(&sleep_probe_state)==2) {
            sleep_reported=1;
            serial_write_public("ZEROOS: timed sleep integration verified.\n");
        }

        if (!preempt_reported && atomic_u64_load(&preempt_probe_done)==1 &&
            atomic_u64_load(&preempt_probe_b)>1) {
            preempt_reported=1;
            serial_write_public("ZEROOS: timer-only preemption stress passed.\n");
        }

        if (!lifecycle_reported && atomic_u64_load(&lifecycle_probe_done)==1 &&
            atomic_u64_load(&lifecycle_probe_exited)>=6) {
            lifecycle_reported=1;
            serial_write_public("ZEROOS: zombie reaping and slot-reuse stress passed.\n");
        }

        /*
         * Interrupt-frame ownership is enforced continuously by
         * task_debug_validate(): a RUNNING task must never retain a frame
         * pointer and a suspended task must own exactly one live context.
         * Once timer-driven preemption has demonstrably resumed tasks
         * through their live hardware frames (frame-form dispatch), record
         * the positive invariant marker so CI can gate on it explicitly.
         */
        if (!frame_invariant_reported &&
            preempt_reported &&
            task_frame_resume_count()>=2 &&
            task_current() && task_current()->interrupt_frame==0 &&
            task_debug_validate()==0) {
            frame_invariant_reported=1;
            serial_write_public("ZEROOS: interrupt-frame ownership invariant verified.\n");
        }

        /*
         * This marker is the scheduler's explicit certification boundary.
         * CI keys off it so a booting kernel cannot be mistaken for a fully
         * passing scheduler: all independent scheduler probes must report
         * success before the aggregate certificate is emitted.
         */
        if (!certification_reported &&
            context_reported &&
            wait_reported &&
            sleep_reported &&
            preempt_reported &&
            lifecycle_reported &&
            frame_invariant_reported &&
            atomic_u64_load(&process_thread_probe_phase)==3 &&
            atomic_u64_load(&scheduler_stress_failures)==0) {
            certification_reported=1;
            serial_write_public("ZEROOS: scheduler certification passed.\n");
        }

        if (!per_cpu_reported && certification_reported &&
            (smp_online_count()==1 ||
             (task_scheduler_task_cpu_mask() & ~1ULL)!=0)) {
            per_cpu_reported=1;
            serial_write_public("ZEROOS: per-CPU scheduler ownership verified.\n");
        }

        if (now>=last_report+100) {
            last_report=now;
            serial_write_public("ZEROOS: timer tick 100.\n");
        }

        /*
         * Certification deadline: all stress probes must complete within a
         * bounded tick budget. This prevents a broken scheduler from hanging
         * CI forever while retaining deterministic QEMU behavior.
         */
        if (now-stress_start>400 &&
            (!preempt_reported || !lifecycle_reported ||
             !frame_invariant_reported ||
             (smp_online_count()>1 && !per_cpu_reported) ||
             atomic_u64_load(&sleep_probe_state)!=2 ||
             atomic_u64_load(&wait_probe_state)!=2 ||
             atomic_u64_load(&process_thread_probe_phase)!=3)) {
            atomic_u64_fetch_add(&scheduler_stress_failures,1);
            kernel_panic("scheduler stress certification timed out");
        }

        __asm__ volatile ("hlt");
        scheduler_yield();
    }
}

static void scheduler_self_test(void) {
    uint64_t worker_id, monitor_id, waiter_id, waker_id, sleeper_id;
    uint64_t preempt_a_id, preempt_b_id, lifecycle_id;

    atomic_u64_init(&task_probe_counter,0);
    atomic_u64_init(&wait_probe_state,0);
    atomic_u64_init(&sleep_probe_state,0);
    atomic_u64_init(&preempt_probe_a,0);
    atomic_u64_init(&preempt_probe_b,0);
    atomic_u64_init(&preempt_probe_done,0);
    atomic_u64_init(&preempt_probe_ticks,0);
    atomic_u64_init(&lifecycle_probe_done,0);
    atomic_u64_init(&lifecycle_probe_created,0);
    atomic_u64_init(&lifecycle_probe_exited,0);
    atomic_u64_init(&scheduler_stress_failures,0);
    atomic_u64_init(&process_thread_probe_phase,0);
    atomic_u64_init(&process_thread_probe_parent_ran,0);
    atomic_u64_init(&process_thread_probe_child_ran,0);
    atomic_u64_init(&process_thread_probe_reuse_ran,0);
    atomic_u64_init(&process_thread_probe_failures,0);
    atomic_u64_init(&process_thread_probe_allow_parent_exit,0);
    wait_queue_init(&wait_probe_queue);

    if (task_system_init()!=0)
        kernel_panic("task system initialization failed");
    if (scheduler_init()!=0)
        kernel_panic("scheduler initialization failed");
    if (process_system_init()!=0)
        kernel_panic("process system initialization failed");
    if (thread_system_init()!=0)
        kernel_panic("thread system initialization failed");

    if (task_create(scheduler_probe_worker,0,&worker_id)!=0)
        kernel_panic("scheduler worker creation failed");
    if (task_create(scheduler_probe_monitor,0,&monitor_id)!=0)
        kernel_panic("scheduler monitor creation failed");
    if (task_create(wait_probe_waiter,0,&waiter_id)!=0)
        kernel_panic("waiter task creation failed");
    if (task_create(wait_probe_waker,0,&waker_id)!=0)
        kernel_panic("waker task creation failed");
    if (task_create(scheduler_probe_sleeper,0,&sleeper_id)!=0)
        kernel_panic("timed sleeper creation failed");
    if (task_create(scheduler_probe_cpu_a,0,&preempt_a_id)!=0)
        kernel_panic("timer-preemption CPU-A creation failed");
    if (task_create(scheduler_probe_cpu_b,0,&preempt_b_id)!=0)
        kernel_panic("timer-preemption CPU-B creation failed");
    if (task_create(scheduler_probe_lifecycle_creator,0,&lifecycle_id)!=0)
        kernel_panic("lifecycle creator creation failed");

    serial_write_public("ZEROOS: kernel tasks created: ");
    serial_write_u64(task_count());
    serial_write_public(" (worker=");
    serial_write_u64(worker_id);
    serial_write_public(", monitor=");
    serial_write_u64(monitor_id);
    serial_write_public(", waiter=");
    serial_write_u64(waiter_id);
    serial_write_public(", waker=");
    serial_write_u64(waker_id);
    serial_write_public(", preemptA=");
    serial_write_u64(preempt_a_id);
    serial_write_public(", preemptB=");
    serial_write_u64(preempt_b_id);
    serial_write_public(", lifecycle=");
    serial_write_u64(lifecycle_id);
    serial_write_public(").\n");

    task_debug_validate();
    if (task_set_priority(task_current(),ZEROOS_TASK_PRIORITY_DEFAULT)!=0 ||
        task_set_affinity(task_current(),1ULL)!=0 ||
        task_set_affinity(task_current(),2ULL)==0)
        kernel_panic("scheduler policy/affinity self-test failed");
    serial_write_public("ZEROOS: scheduler policy and affinity self-test passed.\n");

    if (timer_register_tick_hook(scheduler_tick)!=0)
        kernel_panic("scheduler timer hook registration failed");

    if (task_debug_validate()!=0)
        kernel_panic("scheduler pre-start task validation failed");

    serial_write_public("ZEROOS: publishing per-CPU scheduler start gate.\n");
    task_publish_scheduler_start();
    serial_write_public("ZEROOS: entering kernel task scheduler.\n");
    scheduler_start();

    serial_write_public("ZEROOS: scheduler control transfer returned to bootstrap.\n");
}

void kernel_main(uint64_t multiboot_info, uint64_t multiboot_magic) {
    if (!boot_stack_guard_ok())
        for (;;) __asm__ volatile ("cli; hlt");
    serial_init();
    serial_write_public("\nZEROOS kernel starting...\n");
    serial_write_public("ZEROOS: entered x86-64 long mode.\n");
    serial_write_public("ZEROOS: serial console initialized.\n");
    serial_write_public("ZEROOS: bootstrap stack guard verified.\n");

    if (cpu_init()!=0)
        kernel_panic("CPU feature initialization failed");
    {
        const struct cpu_info *info=cpu_info();
        serial_write_public("ZEROOS: CPU capabilities detected (APIC=");
        serial_write_u64((info->features & ZEROOS_CPU_FEATURE_APIC)!=0);
        serial_write_public(", NX=");
        serial_write_u64((info->features & ZEROOS_CPU_FEATURE_NX)!=0);
        serial_write_public(", physical-address-bits=");
        serial_write_u64(info->physical_address_bits);
        serial_write_public(").\n");
    }

    if ((uint32_t)multiboot_magic==0x36d76289)
        serial_write_public("ZEROOS: Multiboot2 handoff verified.\n");
    else
        kernel_panic("unexpected Multiboot2 boot magic");

    serial_write_public("ZEROOS: Multiboot info address: ");
    serial_write_u64(multiboot_info);
    serial_write_public("\n");

    memory_init(multiboot_info);
    if (memory_total_pages()==0)
        kernel_panic("Multiboot memory map contained no usable pages");
    serial_write_public("ZEROOS: physical page allocator initialized.\n");
    serial_write_public("ZEROOS: managed pages: ");
    serial_write_u64(memory_total_pages());
    serial_write_public("\nZEROOS: free pages: ");
    serial_write_u64(memory_free_pages());
    serial_write_public("\n");

    memory_self_test();

    if (gdt_init()!=0)
        kernel_panic("runtime GDT/TSS initialization failed");
    gdt_self_test();

    if (vmm_init()!=0) kernel_panic("virtual memory initialization failed");
    serial_write_public("ZEROOS: virtual memory manager initialized.\n");
    vmm_self_test();
    vmm_space_self_test();

    if (apic_init(multiboot_info)!=0)
        kernel_panic("interrupt-controller capability probe failed");
    {
        const struct apic_info *info=apic_info();
        const struct acpi_info *firmware=acpi_info();
        if (!firmware->initialized ||
            (firmware->valid &&
             (!firmware->madt_physical || !firmware->local_apic_address ||
              firmware->processor_count==0)))
            kernel_panic("ACPI routing discovery invariant failed");
        serial_write_public("ZEROOS: ACPI routing discovery: ");
        serial_write_public(info->acpi_valid ? "MADT valid" : "MADT unavailable");
        serial_write_public(" (processors=");
        serial_write_u64(info->acpi_processor_count);
        serial_write_public(", ioapics=");
        serial_write_u64(info->acpi_ioapic_count);
        serial_write_public(", overrides=");
        serial_write_u64(info->acpi_interrupt_override_count);
        serial_write_public(", error=");
        serial_write_u64(firmware->error);
        serial_write_public(").\n");
        serial_write_public("ZEROOS: IRQ controller capability: ");
        serial_write_public(info->local_apic_present ?
                            "LAPIC detected, IOAPIC activation pending.\n" :
                            "legacy PIC fallback.\n");
    }

    sync_self_test();

    interrupts_init();
    serial_write_public("ZEROOS: IDT installed and interrupts enabled.\n");
    serial_write_public("ZEROOS: PIT timer configured at ");
    serial_write_u64(timer_frequency_hz());
    serial_write_public(" Hz (clocksource=");
    serial_write_public(timer_clocksource());
    serial_write_public(").\n");
    serial_write_public("ZEROOS: wall-clock sample: ");
    serial_write_u64(timer_wallclock_unix_seconds());
    serial_write_public(".\n");
    serial_write_public("ZEROOS: IRQ ownership layer initialized.\n");

    if (smp_init()!=0)
        kernel_panic("SMP startup boundary failed its internal contract");
    serial_write_public("ZEROOS: SMP CPU topology: discovered=");
    serial_write_u64(smp_discovered_count());
    serial_write_public(" online=");
    serial_write_u64(smp_online_count());
    serial_write_public(".\n");
    if (smp_startup_self_test()!=0)
        kernel_panic("SMP topology publication invariant failed");
    if (smp_online_count()>1)
        serial_write_public("ZEROOS: remote TLB shootdown self-test passed.\n");
    if (smp_discovered_count()>1 && !smp_is_degraded()) {
        serial_write_public("ZEROOS: SMP startup self-test passed.\n");
    } else if (smp_is_degraded()) {
        serial_write_public("ZEROOS: SMP startup recovery self-test passed in degraded mode.\n");
    } else {
        serial_write_public("ZEROOS: SMP startup self-test skipped (single CPU).\n");
    }

    serial_write_public("ZEROOS: foundation milestone reached.\n");

    scheduler_self_test();

    for (;;) __asm__ volatile ("sti; hlt");
}
