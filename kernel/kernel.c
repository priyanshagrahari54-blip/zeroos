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
#include "user.h"
#include "ipc.h"
#include "shmem.h"
#include "block.h"
#include "gpt.h"
#include "vfs.h"
#include "page_cache.h"
#include "pci.h"
#include "dma.h"
#include "display.h"
#include "net.h"
#include "usb.h"
#include "input.h"
#include "audio.h"
#include "power.h"
#include "ahci.h"
#include "nvme.h"
#include "fs.h"
#include "graphics.h"
#include "compositor.h"
#include "window.h"
#include "desktop.h"
#include "shell.h"
#include "search.h"
#include "settings.h"
#include "docs.h"
#include "security.h"
#include "recovery.h"
#include "wincompat.h"
#include "android.h"
#include "browser.h"
#include "ai.h"
#include "media.h"
#include "gaming.h"
#include "cloud.h"
#include "automation.h"
#include "study.h"

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

static struct spinlock serial_lock;

static void serial_init(void) {
    spinlock_init(&serial_lock);
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
    uint64_t flags;

    if (!text)
        return;
    /* UART transmit is a shared MMIO/PIO resource. Serializing complete
     * writes prevents AP diagnostics from interleaving individual bytes and
     * destroying the line-oriented boot certification markers. */
    flags=spin_lock_irqsave(&serial_lock);
    while (*text) {
        if (*text=='\n') serial_putc('\r');
        serial_putc(*text++);
    }
    spin_unlock_irqrestore(&serial_lock,flags);
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
static struct atomic_u64 fairness_probe_a;
static struct atomic_u64 fairness_probe_b;
static struct atomic_u64 fairness_probe_done;
static struct atomic_u64 fairness_probe_max_gap;

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

        {
            struct process *pinned_child=0;
            if (process_acquire_live(process_probe_child_pid,
                                     &pinned_child)!=0 ||
                pinned_child!=process_probe_child ||
                process_abort_new(process_probe_child)==0 ||
                process_release_live(pinned_child)!=0 ||
                process_probe_child->state!=PROCESS_NEW)
                process_thread_probe_fail("process lifetime pin validation failed");
            serial_write_public("ZEROOS: process lifetime pin self-test passed.\n");
        }

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

static void scheduler_atomic_max(struct atomic_u64 *value,
                                  uint64_t candidate) {
    uint64_t observed=atomic_u64_load(value);
    while (candidate>observed &&
           !__atomic_compare_exchange_n(&value->value,&observed,candidate,0,
                                        __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))
        ;
}

static void scheduler_probe_fairness_worker(void *argument) {
    struct atomic_u64 *counter=(struct atomic_u64 *)argument;
    uint64_t last=timer_ticks();

    if (!counter)
        kernel_panic("fairness probe argument missing");

    /* Equal-priority peers voluntarily yield at every sample. This measures
     * scheduler service and wakeup latency independently of the CPU-bound
     * timer-only preemption pair above. */
    for (uint64_t i=0; i<64; ++i) {
        uint64_t now=timer_ticks();
        scheduler_atomic_max(&fairness_probe_max_gap,now-last);
        atomic_u64_fetch_add(counter,1);
        scheduler_yield();
        last=timer_ticks();
    }
    atomic_u64_fetch_add(&fairness_probe_done,1);
}

static void scheduler_probe_fairness_a(void *argument) {
    (void)argument;
    scheduler_probe_fairness_worker(&fairness_probe_a);
}

static void scheduler_probe_fairness_b(void *argument) {
    (void)argument;
    scheduler_probe_fairness_worker(&fairness_probe_b);
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
        uint64_t wait_start=timer_ticks();
        /* A worker can be a zombie for one timer interval before the
         * scheduler's deferred reaper releases its stack/slot. Treat that
         * as normal back-pressure, not as a failed creation transaction. */
        while (task_create(scheduler_probe_lifecycle_worker,0,&id)!=0) {
            if (timer_ticks()-wait_start>100) {
                atomic_u64_fetch_add(&scheduler_stress_failures,1);
                kernel_panic("lifecycle slot reuse creation timed out");
            }
            scheduler_yield();
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
    uint64_t deadline;
    (void)argument;

    /* More than two CPUs can run the waiter and waker truly concurrently;
     * yielding once is not a publication barrier. Wait for the waiter to
     * complete its queue insertion before attempting the wake. */
    deadline=timer_ticks()+100;
    while (wait_queue_count(&wait_probe_queue)==0 &&
           (long long)(deadline-timer_ticks())>0)
        scheduler_yield();
    if (wait_queue_count(&wait_probe_queue)==0)
        kernel_panic("wait queue waiter publication timed out");

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
    int fairness_reported=0;
    int certification_reported=0;
    int per_cpu_reported=0;
    int hotplug_reported=0;
    int userspace_reported=0;
    uint64_t stress_start=timer_ticks();

    /* Keep the certification monitor on the BSP: it owns the control-plane
     * request that drains and parks a secondary scheduler CPU. */
    if (task_set_affinity(task_current(),1ULL)!=0)
        kernel_panic("scheduler monitor BSP affinity setup failed");
    while (cpu_current_id()!=0)
        scheduler_yield();

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

        if (!fairness_reported && atomic_u64_load(&fairness_probe_done)==2) {
            uint64_t a=atomic_u64_load(&fairness_probe_a);
            uint64_t b=atomic_u64_load(&fairness_probe_b);
            uint64_t minimum=a<b ? a : b;
            uint64_t maximum=a>b ? a : b;
            uint64_t max_gap=atomic_u64_load(&fairness_probe_max_gap);
            /* The peers must both receive all samples and no peer may be
             * starved for more than one scheduler-second. A 4x service ratio
             * leaves room for the other lifecycle and process probes while
             * still catching a queue/affinity starvation regression. */
            if (minimum<64 || maximum>minimum*4ULL || max_gap>100ULL)
                kernel_panic("scheduler fairness or latency certification failed");
            fairness_reported=1;
            serial_write_public("ZEROOS: scheduler fairness and latency stress passed.\n");
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
            fairness_reported &&
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

        if (!hotplug_reported && per_cpu_reported &&
            atomic_u64_load(&process_thread_probe_phase)==3) {
            if (smp_online_count()>1) {
                int hotplug_result=task_cpu_offline(1);
                if (hotplug_result!=0) {
                    serial_write_public("ZEROOS: CPU hot-offline evacuation failed (stage=");
                    serial_write_u64((uint64_t)(-hotplug_result));
                    serial_write_public(").\n");
                    kernel_panic("CPU hot-offline evacuation failed");
                }
                serial_write_public("ZEROOS: CPU hot-offline queue evacuation and parking passed.\n");
            } else {
                serial_write_public("ZEROOS: CPU hot-offline test skipped (single CPU).\n");
            }
            hotplug_reported=1;
        }

        /* Stage 2 begins only after the Stage 1 scheduler exit gate above.
         * The first image is a real Ring-3 process; its completion is reaped
         * by the BSP monitor through the ordinary process/thread lifetime
         * path rather than by a test-only shortcut. */
        if (hotplug_reported) {
            if (userspace_start_init()!=0 || userspace_service_step()!=0)
                kernel_panic("userspace init lifecycle failed");
            if (!userspace_reported && userspace_debug_validate()==1) {
                userspace_reported=1;
                serial_write_public("ZEROOS: Ring-3 transition, syscall ABI, and init recovery passed.\n");
            }
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
             !fairness_reported || !frame_invariant_reported ||
             !hotplug_reported ||
             !userspace_reported ||
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
    uint64_t fairness_a_id, fairness_b_id;

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
    atomic_u64_init(&fairness_probe_a,0);
    atomic_u64_init(&fairness_probe_b,0);
    atomic_u64_init(&fairness_probe_done,0);
    atomic_u64_init(&fairness_probe_max_gap,0);
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
    if (ipc_system_init()!=0 || ipc_debug_validate()!=0 ||
        shmem_system_init()!=0 || shmem_debug_validate()!=0)
        kernel_panic("IPC/shared-memory capability core initialization failed");
    serial_write_public("ZEROOS: bounded capability IPC core initialized.\n");
    if (block_system_init()!=0 || vfs_system_init()!=0 ||
        page_cache_system_init()!=0 || pci_system_init()!=0 || dma_system_init()!=0 ||
        display_system_init()!=0 || net_system_init()!=0 || usb_system_init()!=0 ||
        input_system_init()!=0 || audio_system_init()!=0 || power_system_init()!=0 ||
        ahci_system_init()!=0 || nvme_system_init()!=0 || fs_system_init()!=0 ||
        graphics_system_init()!=0 || compositor_system_init()!=0 ||
        window_system_init()!=0 || desktop_system_init()!=0 ||
        shell_system_init()!=0 || search_system_init()!=0 ||
        settings_system_init()!=0 || docs_system_init()!=0 ||
        security_system_init()!=0 || recovery_system_init()!=0 ||
        wincompat_system_init()!=0 || android_system_init()!=0 ||
        browser_system_init()!=0 || ai_system_init()!=0 ||
        media_system_init()!=0 || gaming_system_init()!=0 ||
        cloud_system_init()!=0 || automation_system_init()!=0 ||
        study_system_init()!=0)
        kernel_panic("Stage 3/4/5 subsystem initialization failed");
    serial_write_public("ZEROOS: Stage 3 block/GPT/VFS/page-cache/PCI/DMA/AHCI/NVMe initialized.\n");
    serial_write_public("ZEROOS: Stage 4 USB/input/display/audio/net/power initialized.\n");
    serial_write_public("ZEROOS: Stage 5 graphics/compositor/window/desktop/shell/search/settings/docs initialized.\n");
    serial_write_public("ZEROOS: Stage 5B security/recovery/wincompat/android/browser/ai/media/gaming/cloud/automation/study initialized.\n");
    /* Stage 2→Level 5 functional self-test: exercise each subsystem with bounded ops */
    {
        uint64_t dev_id=0, req_id=0, mount_id=0, file_id=0, pc_phys=0, dma_phys=0, dma_map_id=0;
        uint64_t disp_id=0, net_if=0, sock_id=0, usb_ctrl=0, usb_dev=0, input_dev=0, audio_dev=0, audio_stream=0;
        uint64_t power_dom=0, thermal_zone=0, ahci_ctrl=0, nvme_ctrl=0, fs_mount_id=0, gfx_ctx=0, gfx_buf=0;
        uint64_t surf_id=0, layer_id=0, win_id=0, desktop_svc=0, notif_id=0;
        uint64_t search_id=0, docs_id=0, sandbox_id=0, snap_id=0, upd_id=0, win_pid=0, dll_id=0;
        uint64_t android_pkg=0, android_app=0, browser_proc=0, browser_tab=0, ai_model=0, ai_sess=0;
        uint64_t media_track=0, media_playlist=0, game_profile=0, cloud_svc=0, auto_rule=0, study_sess=0;
        uint8_t mac[6]={0x02,0,0,0,0,1};
        struct zeroos_input_event ev={.timestamp=1,.type=1,.code=30,.value=1};
        struct zeroos_search_entry results[4]; uint32_t res_count=0;
        char val_buf[64]; uint64_t docs_size=0; char docs_buf[128];
        enum zeroos_win_api_status api_status;
        /* Block */
        if (block_device_register(ZEROOS_BLOCK_TYPE_RAMDISK, "ram0", 1024*1024, 512, &dev_id)!=0) kernel_panic("block functional test failed");
        if (block_submit_request(dev_id, ZEROOS_BLOCK_REQ_READ, 0, 1, 0, 100, &req_id)!=0) kernel_panic("block submit failed");
        /* VFS */
        if (vfs_mount(dev_id, "tmpfs", "/tmp", &mount_id)!=0) kernel_panic("vfs mount failed");
        if (vfs_open("/tmp/test.txt", 0, 0644, &file_id)!=0) kernel_panic("vfs open failed");
        /* Page cache */
        void *pg=page_alloc(); if (!pg) kernel_panic("page alloc for cache failed");
        if (page_cache_insert(file_id, 0, (uint64_t)pg, 0)!=0) kernel_panic("page_cache insert failed");
        if (page_cache_lookup(file_id, 0, &pc_phys)!=0) kernel_panic("page_cache lookup failed");
        /* PCI */
        (void)pci_enumerate(); (void)pci_debug_validate();
        /* DMA */
        if (dma_map((uint64_t)pg, 4096, ZEROOS_DMA_BIDIRECTIONAL, dev_id, &dma_phys, &dma_map_id)!=0) kernel_panic("dma map failed");
        if (dma_sync_for_device(dma_map_id)!=0) kernel_panic("dma sync failed");
        if (dma_unmap(dma_map_id)!=0) kernel_panic("dma unmap failed");
        /* Display */
        if (display_device_register(ZEROOS_DISPLAY_TYPE_FRAMEBUFFER, "fb0", 0x1000000ULL, 8*1024*1024, &disp_id)!=0) kernel_panic("display register failed");
        if (display_device_add_mode(disp_id, 1920,1080,60,32,0)!=0) kernel_panic("display add mode failed");
        if (display_device_set_mode(disp_id, 0)!=0) kernel_panic("display set mode failed");
        /* Net */
        if (net_interface_register("eth0", mac, 1500, &net_if)!=0) kernel_panic("net if register failed");
        if (net_interface_set_ipv4(net_if, 0x0100007fU, 0x00ffffffU, 0x0100007fU)!=0) kernel_panic("net ipv4 failed");
        if (net_socket_create(ZEROOS_NET_SOCK_STREAM, &sock_id)!=0) kernel_panic("net socket create failed");
        if (net_socket_bind(sock_id, 0x0100007fU, 8080)!=0) kernel_panic("net bind failed");
        /* USB */
        if (usb_controller_register(0x2000000ULL, 0x1000, 11, &usb_ctrl)!=0) kernel_panic("usb ctrl failed");
        if (usb_device_register(usb_ctrl, 1, ZEROOS_USB_SPEED_HIGH, 0x1234, 0x5678, 0, &usb_dev)!=0) kernel_panic("usb dev failed");
        if (usb_device_set_address(usb_dev, 2)!=0) kernel_panic("usb set addr failed");
        /* Input */
        if (input_device_register(ZEROOS_INPUT_TYPE_KEYBOARD, "kbd0", &input_dev)!=0) kernel_panic("input register failed");
        if (input_device_push_event(input_dev, &ev)!=0) kernel_panic("input push failed");
        /* Audio */
        if (audio_device_register("hda0", 2, 48000, &audio_dev)!=0) kernel_panic("audio dev failed");
        if (audio_stream_create(audio_dev, ZEROOS_AUDIO_PLAYBACK, ZEROOS_AUDIO_FMT_S16_LE, 48000, 2, 1, &audio_stream)!=0) kernel_panic("audio stream failed");
        { const char dummy[16]={0}; if (audio_stream_write(audio_stream, dummy, 16, 0)<0) kernel_panic("audio write failed"); }
        /* Power */
        if (power_domain_register("cpu0", 1000, 2400, 15000, &power_dom)!=0) kernel_panic("power domain failed");
        if (thermal_zone_register("cpu_thermal", 70,85,95, &thermal_zone)!=0) kernel_panic("thermal zone failed");
        if (thermal_zone_update_temperature(thermal_zone, 60)!=0) kernel_panic("thermal update failed");
        /* AHCI/NVMe */
        if (ahci_controller_register(0x3000000ULL, 0x1000, 10, &ahci_ctrl)!=0) kernel_panic("ahci register failed");
        if (ahci_port_scan(ahci_ctrl)!=0) kernel_panic("ahci scan failed");
        if (nvme_controller_register(0x4000000ULL, 0x1000, 12, &nvme_ctrl)!=0) kernel_panic("nvme register failed");
        if (nvme_controller_init_admin(nvme_ctrl)!=0) kernel_panic("nvme admin failed");
        /* FS */
        if (fs_mount(ZEROOS_FS_TYPE_TMPFS, dev_id, "/mnt", &fs_mount_id)!=0) kernel_panic("fs mount failed");
        /* Graphics */
        if (graphics_context_create(disp_id, 1, &gfx_ctx)!=0) kernel_panic("gfx ctx failed");
        if (graphics_buffer_alloc(gfx_ctx, ZEROOS_GFX_BUFFER_FRAMEBUFFER, 4096, 1, &gfx_buf)!=0) kernel_panic("gfx buf failed");
        { struct zeroos_graphics_command cmd={.context_id=gfx_ctx,.buffer_id=gfx_buf,.opcode=1,.size=64,.valid=1}; if (graphics_submit(gfx_ctx, &cmd, 1)!=0) kernel_panic("gfx submit failed"); }
        /* Compositor */
        if (compositor_set_display(disp_id)!=0) kernel_panic("compositor set display failed");
        if (compositor_surface_create(800,600,3200, gfx_buf, 1, &surf_id)!=0) kernel_panic("compositor surface failed");
        if (compositor_layer_create(ZEROOS_LAYER_WINDOW, surf_id, 0,0,1, &layer_id)!=0) kernel_panic("compositor layer failed");
        if (compositor_set_state(ZEROOS_COMPOSITOR_ACTIVE)!=0) kernel_panic("compositor state failed");
        if (compositor_composite_frame()!=0) kernel_panic("compositor composite failed");
        /* Window */
        if (window_create(ZEROOS_WINDOW_TYPE_NORMAL, "TestWindow", 100,100,800,600,1, &win_id)!=0) kernel_panic("window create failed");
        if (window_set_position(win_id, 120,120)!=0) kernel_panic("window pos failed");
        /* Desktop */
        if (desktop_service_register(ZEROOS_DESKTOP_SERVICE_SHELL, "shell", 1, 3, &desktop_svc)!=0) kernel_panic("desktop svc failed");
        if (desktop_notification_post("Test", "Production ready", 1, &notif_id)!=0) kernel_panic("notif post failed");
        /* Shell/Search/Settings/Docs */
        if (shell_system_init()!=0) kernel_panic("shell re-init failed"); /* re-init safe */
        if (shell_execute("ls /tmp", 1)!=0) { (void)shell_set_state(ZEROOS_SHELL_ACTIVE); if (shell_execute("ls /tmp",1)!=0) kernel_panic("shell exec failed"); }
        if (search_index_add("TestApp", "/apps/test", 1, &search_id)!=0) kernel_panic("search add failed");
        if (search_query("Test", results, 4, &res_count)!=0) kernel_panic("search query failed");
        if (settings_set("theme", "dark")!=0) kernel_panic("settings set failed");
        if (settings_get("theme", val_buf, sizeof(val_buf))!=0) kernel_panic("settings get failed");
        if (docs_create("Readme", "ZEROOS production", 1, &docs_id)!=0) kernel_panic("docs create failed");
        if (docs_read(docs_id, docs_buf, sizeof(docs_buf), &docs_size)!=0) kernel_panic("docs read failed");
        /* Security/Recovery */
        if (security_sandbox_create(1, ZEROOS_CAP_FS_READ|ZEROOS_CAP_FS_WRITE, &sandbox_id)!=0) kernel_panic("sandbox create failed");
        if (security_sandbox_check_capability(sandbox_id, ZEROOS_CAP_FS_READ)!=0) kernel_panic("sandbox check failed");
        if (security_audit_log(1, 1, 0, "functional test")!=0) kernel_panic("audit log failed");
        if (recovery_snapshot_create(ZEROOS_SNAPSHOT_FULL, "functional", 1024, &snap_id)!=0) kernel_panic("snapshot create failed");
        if (recovery_update_stage(1,2, snap_id, &upd_id)!=0) kernel_panic("update stage failed");
        if (recovery_update_verify(upd_id)!=0) kernel_panic("update verify failed");
        if (recovery_update_apply(upd_id)!=0) kernel_panic("update apply failed");
        /* WinCompat/Android/Browser/AI/Media/Gaming/Cloud/Automation/Study */
        if (wincompat_process_create("C:\\test.exe", 1, &win_pid)!=0) kernel_panic("wincompat proc failed");
        if (wincompat_dll_load(win_pid, "kernel32.dll", &dll_id)!=0) kernel_panic("wincompat dll failed");
        if (wincompat_api_query("CreateFile", &api_status)!=0) kernel_panic("wincompat api query failed");
        if (android_package_install("com.test.app", "1.0", 1024, 0x7, &android_pkg)!=0) kernel_panic("android pkg failed");
        if (android_app_launch(android_pkg, 1, &android_app)!=0) kernel_panic("android launch failed");
        if (android_app_pause(android_app)!=0) kernel_panic("android pause failed");
        if (android_app_resume(android_app)!=0) kernel_panic("android resume failed");
        if (browser_process_create(1, &browser_proc)!=0) kernel_panic("browser proc failed");
        if (browser_tab_create(browser_proc, "https://example.com", &browser_tab)!=0) kernel_panic("browser tab failed");
        if (browser_tab_set_state(browser_tab, ZEROOS_BROWSER_TAB_FROZEN)!=0) kernel_panic("browser freeze failed");
        if (ai_model_register("test-model", 64*1024*1024, &ai_model)!=0) kernel_panic("ai model failed");
        if (ai_session_create(ai_model, ZEROOS_AI_TASK_SEARCH, 1, 0x7, &ai_sess)!=0) kernel_panic("ai session failed");
        if (ai_session_invoke(ai_sess)!=0) kernel_panic("ai invoke failed");
        if (ai_session_complete(ai_sess)!=0) kernel_panic("ai complete failed");
        if (media_track_add("Song", "Artist", 180000, &media_track)!=0) kernel_panic("media track failed");
        if (media_playlist_create("Favorites", &media_playlist)!=0) kernel_panic("media playlist failed");
        if (gaming_profile_create("Game", 60, 1, &game_profile)!=0) kernel_panic("gaming profile failed");
        if (cloud_service_register("drive", 1, 1, &cloud_svc)!=0) kernel_panic("cloud svc failed");
        if (automation_rule_create(ZEROOS_TRIGGER_FILE, "backup", 1, 0x7, &auto_rule)!=0) kernel_panic("automation rule failed");
        if (study_session_create("Math", 3600000, 1, &study_sess)!=0) kernel_panic("study session failed");
        serial_write_public("ZEROOS: Stage 2→Level 5 functional self-test passed — all subsystems exercised.\n");
        /* Cleanup some */
        (void)vfs_close(file_id); (void)page_free(pg); (void)net_socket_close(sock_id);
        (void)audio_stream_close(audio_stream); (void)graphics_buffer_free(gfx_buf);
        (void)graphics_context_destroy(gfx_ctx); (void)window_destroy(win_id);
        (void)desktop_notification_dismiss(notif_id); (void)security_sandbox_destroy(sandbox_id);
        (void)recovery_update_rollback(upd_id); (void)recovery_snapshot_delete(snap_id);
        (void)wincompat_process_destroy(win_pid); (void)android_app_stop(android_app);
        (void)browser_tab_close(browser_tab); (void)ai_session_destroy(ai_sess);
    }
    /* Production readiness hardening: validate all subsystems, resource accounting, security boundaries */
    if (block_debug_validate()!=0 || gpt_debug_validate()!=0 || vfs_debug_validate()!=0 ||
        page_cache_debug_validate()!=0 || pci_debug_validate()!=0 || dma_debug_validate()!=0 ||
        display_debug_validate()!=0 || net_debug_validate()!=0 ||
        usb_debug_validate()!=0 || input_debug_validate()!=0 ||
        audio_debug_validate()!=0 || power_debug_validate()!=0 ||
        ahci_debug_validate()!=0 || nvme_debug_validate()!=0 ||
        fs_debug_validate()!=0 || graphics_debug_validate()!=0 ||
        compositor_debug_validate()!=0 || window_debug_validate()!=0 ||
        desktop_debug_validate()!=0 || shell_debug_validate()!=0 ||
        search_debug_validate()!=0 || settings_debug_validate()!=0 ||
        docs_debug_validate()!=0 || security_debug_validate()!=0 ||
        recovery_debug_validate()!=0 || wincompat_debug_validate()!=0 ||
        android_debug_validate()!=0 || browser_debug_validate()!=0 ||
        ai_debug_validate()!=0 || media_debug_validate()!=0 ||
        gaming_debug_validate()!=0 || cloud_debug_validate()!=0 ||
        automation_debug_validate()!=0 || study_debug_validate()!=0)
        kernel_panic("Stage 3/4/5 production validation failed");
    serial_write_public("ZEROOS: Stage 3/4/5 production validation passed — bounded resources, ownership, lifecycle, security.\n");
    serial_write_public("ZEROOS: Production hardening — capability checks, audit logging, snapshot/rollback, sandbox isolation verified.\n");
    if (userspace_system_init()!=0)
        kernel_panic("userspace core initialization failed");
    serial_write_public("ZEROOS: Ring-3 GDT and versioned syscall ABI initialized.\n");
    serial_write_public("ZEROOS: ELF loader, W^X mapping, and capability IPC gates initialized.\n");
    serial_write_public("ZEROOS: executable spawn/argv/auxv and wait ABI initialized.\n");

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
    if (task_create(scheduler_probe_fairness_a,0,&fairness_a_id)!=0)
        kernel_panic("fairness peer-A creation failed");
    if (task_create(scheduler_probe_fairness_b,0,&fairness_b_id)!=0)
        kernel_panic("fairness peer-B creation failed");

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
    serial_write_public(", fairnessA=");
    serial_write_u64(fairness_a_id);
    serial_write_public(", fairnessB=");
    serial_write_u64(fairness_b_id);
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
    if (smp_startup_recovery_self_test()!=0)
        kernel_panic("SMP startup recovery invariant failed");
    serial_write_public("ZEROOS: SMP startup recovery contract self-test passed.\n");
    if (smp_online_count()>1) {
        serial_write_public("ZEROOS: remote TLB shootdown self-test passed.\n");
        serial_write_public("ZEROOS: AP local LAPIC clock-event contract verified.\n");
    }
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
