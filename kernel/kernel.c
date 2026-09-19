#include "types.h"
#include "memory.h"
#include "timer.h"
#include "vmm.h"
#include "sync.h"
#include "task.h"
#include "scheduler.h"
#include "wait.h"

#define COM1 0x3F8
#define VMM_SELF_TEST_VA 0x4000000000ULL

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
    serial_write_public("ZEROOS: physical allocator self-test passed.\n");
}

static void vmm_self_test(void) {
    void *physical=page_alloc();
    if (!physical) kernel_panic("VMM self-test could not allocate a page");
    if (vmm_map_page(VMM_SELF_TEST_VA,(uint64_t)physical,VMM_WRITABLE|VMM_NO_EXECUTE)!=0)
        kernel_panic("VMM map failed");
    if (vmm_translate(VMM_SELF_TEST_VA)!=(uint64_t)physical)
        kernel_panic("VMM translation mismatch");
    if (vmm_unmap_page(VMM_SELF_TEST_VA)!=0) kernel_panic("VMM unmap failed");
    page_free(physical);
    serial_write_public("ZEROOS: virtual memory self-test passed.\n");
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

        if (now>=last_report+100) {
            last_report=now;
            serial_write_public("ZEROOS: timer tick 100.\n");
        }

        __asm__ volatile ("hlt");
        scheduler_yield();
    }
}

static void scheduler_self_test(void) {
    uint64_t worker_id, monitor_id, waiter_id, waker_id, sleeper_id;

    atomic_u64_init(&task_probe_counter,0);
    atomic_u64_init(&wait_probe_state,0);
    atomic_u64_init(&sleep_probe_state,0);
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
