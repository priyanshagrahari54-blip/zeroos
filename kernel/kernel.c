#include "types.h"
#include "memory.h"
#include "timer.h"
#include "vmm.h"
#include "sync.h"
#include "task.h"
#include "scheduler.h"
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
    if (!memory_is_managed_range((uint64_t)a,ZEROOS_PAGE_SIZE) ||
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
    page_free(physical);
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
    if (vmm_space_map_page(&space,VMM_SPACE_TEST_VA,(uint64_t)physical,
                           VMM_USER|VMM_WRITABLE|VMM_NO_EXECUTE)!=0)
        kernel_panic("address-space user mapping failed");
    if (vmm_space_translate(&space,VMM_SPACE_TEST_VA)!=(uint64_t)physical)
        kernel_panic("address-space translation failed");
    if (vmm_space_map_page(&space,0x4000000000ULL,(uint64_t)physical,
                           VMM_USER|VMM_WRITABLE)!=-1)
        kernel_panic("address-space accepted unsafe PML4");
    if (vmm_space_unmap_page(&space,VMM_SPACE_TEST_VA)!=0)
        kernel_panic("address-space unmap failed");
    if (vmm_space_translate(&space,VMM_SPACE_TEST_VA)!=0)
        kernel_panic("address-space unmap translation failed");
    vmm_space_destroy(&space);
    page_free(physical);
    serial_write_public("ZEROOS: per-address-space VMM self-test passed.\n");
}

static void sync_self_test(void) {
    struct spinlock lock;
    struct atomic_u64 counter;
    uint64_t flags;
    spinlock_init(&lock);
    atomic_u64_init(&counter,41);
    flags=spin_lock_irqsave(&lock);
    atomic_u64_fetch_add(&counter,1);
    spin_unlock_irqrestore(&lock,flags);
    if (atomic_u64_load(&counter)!=42)
        kernel_panic("synchronization primitive self-test failed");
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

static void scheduler_probe_cpu_a(void *argument) {
    (void)argument;
    atomic_u64_store(&preempt_probe_ticks,timer_ticks());

    /*
     * Deliberately never yield.  CPU-B must still make progress here, proving
     * timer-only preemption rather than cooperative scheduling.
     */
    while (atomic_u64_load(&preempt_probe_b)==0) {
        atomic_u64_fetch_add(&preempt_probe_a,1);
        if (timer_ticks() - atomic_u64_load(&preempt_probe_ticks) > 250) {
            atomic_u64_fetch_add(&scheduler_stress_failures,1);
            kernel_panic("timer-only preemption failed: CPU-B made no progress");
        }
    }
    atomic_u64_fetch_add(&preempt_probe_done,1);

    /*
     * Keep callee-saved registers live across repeated preemptions as well as
     * voluntary switches. The values are checked after CPU-B has run.
     */
    register uint64_t rbx asm("rbx")=0x9e3779b97f4a7c15ULL;
    register uint64_t r12 asm("r12")=0x243f6a8885a308d3ULL;
    register uint64_t r13 asm("r13")=0x13198a2e03707344ULL;
    register uint64_t r14 asm("r14")=0xa4093822299f31d0ULL;
    register uint64_t r15 asm("r15")=0x082efa98ec4e6c89ULL;
    for (volatile uint64_t i=0;i<2000000ULL;++i) {
        __asm__ volatile ("" : "+r"(rbx), "+r"(r12), "+r"(r13), "+r"(r14), "+r"(r15));
    }
    if (rbx!=0x9e3779b97f4a7c15ULL ||
        r12!=0x243f6a8885a308d3ULL ||
        r13!=0x13198a2e03707344ULL ||
        r14!=0xa4093822299f31d0ULL ||
        r15!=0x082efa98ec4e6c89ULL)
        kernel_panic("callee-saved register corruption during preemption");
}

static void scheduler_probe_cpu_b(void *argument) {
    (void)argument;
    register uint64_t rbx asm("rbx")=0xdeadbeefcafebabeULL;
    register uint64_t r12 asm("r12")=0x0123456789abcdefULL;
    register uint64_t r13 asm("r13")=0xfedcba9876543210ULL;
    register uint64_t r14 asm("r14")=0x55aa55aa55aa55aaULL;
    register uint64_t r15 asm("r15")=0xaa55aa55aa55aa55ULL;

    atomic_u64_store(&preempt_probe_b,1);
    while (atomic_u64_load(&preempt_probe_done)==0) {
        atomic_u64_fetch_add(&preempt_probe_b,1);
        __asm__ volatile ("" : "+r"(rbx), "+r"(r12), "+r"(r13), "+r"(r14), "+r"(r15));
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

static void scheduler_probe_worker(void *argument) {
    uint64_t rbx_value=0x1122334455667788ULL;
    uint64_t r12_value=0x13579bdf2468ace0ULL;
    uint64_t r13_value=0x0eca8642fdb97531ULL;
    uint64_t r14_value=0x55aa55aa33cc33ccULL;
    uint64_t r15_value=0xcc33cc3355aa55aaULL;
    (void)argument;

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
        scheduler_yield();
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
    uint64_t stress_start=timer_ticks();

    for (;;) {
        uint64_t now=timer_ticks();

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
             atomic_u64_load(&sleep_probe_state)!=2 ||
             atomic_u64_load(&wait_probe_state)!=2)) {
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
    wait_queue_init(&wait_probe_queue);

    if (task_system_init()!=0)
        kernel_panic("task system initialization failed");
    if (scheduler_init()!=0)
        kernel_panic("scheduler initialization failed");

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

    if (timer_register_tick_hook(scheduler_tick)!=0)
        kernel_panic("scheduler timer hook registration failed");

    if (task_debug_validate()!=0)
        kernel_panic("scheduler pre-start task validation failed");

    serial_write_public("ZEROOS: entering kernel task scheduler.\n");
    scheduler_start();

    serial_write_public("ZEROOS: returned to bootstrap task.\n");
}

void kernel_main(uint64_t multiboot_info, uint64_t multiboot_magic) {
    serial_init();
    serial_write_public("\nZEROOS kernel starting...\n");
    serial_write_public("ZEROOS: entered x86-64 long mode.\n");
    serial_write_public("ZEROOS: serial console initialized.\n");

    if ((uint32_t)multiboot_magic==0x36d76289)
        serial_write_public("ZEROOS: Multiboot2 handoff verified.\n");
    else
        kernel_panic("unexpected Multiboot2 boot magic");

    serial_write_public("ZEROOS: Multiboot info address: ");
    serial_write_u64(multiboot_info);
    serial_write_public("\n");

    memory_init(multiboot_info);
    serial_write_public("ZEROOS: physical page allocator initialized.\n");
    serial_write_public("ZEROOS: managed pages: ");
    serial_write_u64(memory_total_pages());
    serial_write_public("\nZEROOS: free pages: ");
    serial_write_u64(memory_free_pages());
    serial_write_public("\n");

    memory_self_test();

    if (vmm_init()!=0) kernel_panic("virtual memory initialization failed");
    serial_write_public("ZEROOS: virtual memory manager initialized.\n");
    vmm_self_test();
    vmm_space_self_test();

    sync_self_test();

    interrupts_init();
    serial_write_public("ZEROOS: IDT installed and interrupts enabled.\n");
    serial_write_public("ZEROOS: PIT timer configured at ");
    serial_write_u64(timer_frequency_hz());
    serial_write_public(" Hz.\n");
    serial_write_public("ZEROOS: IRQ ownership layer initialized.\n");
    serial_write_public("ZEROOS: foundation milestone reached.\n");

    scheduler_self_test();

    for (;;) __asm__ volatile ("sti; hlt");
}
