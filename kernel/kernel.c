#include "types.h"
#include "memory.h"
#include "heap.h"
#include "timer.h"
#include "vmm.h"
#include "sync.h"
#include "gdt.h"
#include "task.h"
#include "process.h"
#include "scheduler.h"
#include "syscall.h"
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
    /*
     * One string is one atomic unit of console output. On this single CPU
     * an IRQ can otherwise preempt a C-context writer between bytes and
     * interleave its own diagnostics into the middle of a line, corrupting
     * the console log. Strings are short (microseconds), so disabling
     * interrupts for the write is the minimal correct serialization; in
     * interrupt context (IF already clear) and in user syscalls this is a
     * no-op.
     */
    /*
     * The write itself is atomic, and the previous interrupt state is
     * restored (not forced on): callers may run with IF deliberately
     * clear (locked regions, exception handlers), and an unconditional
     * sti there would reopen the window the caller closed.
     */
    uint64_t flags;
    __asm__ volatile ("pushf; popq %0; cli" : "=r"(flags) : : "memory");
    while (*text) {
        if (*text=='\n') serial_putc('\r');
        serial_putc(*text++);
    }
    if (flags & 0x200ULL)
        __asm__ volatile ("sti" ::: "memory");
}

void serial_write_u64_public(uint64_t value) {
    /*
     * Print as 16 zero-padded hex digits. Same atomicity and interrupt
     * state rules as serial_write_public: the whole number is one
     * console unit, and the previous IF state is restored.
     */
    char buf[17];
    const char *digits="0123456789abcdef";
    buf[16]=0;
    for (int i=15;i>=0;--i) {
        buf[i]=digits[value&0xf];
        value>>=4;
    }
    uint64_t flags;
    __asm__ volatile ("pushf; popq %0; cli" : "=r"(flags) : : "memory");
    for (int i=0;i<16;++i) serial_putc(buf[i]);
    if (flags & 0x200ULL)
        __asm__ volatile ("sti" ::: "memory");
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

/*
 * Early fatal-exception IDT.
 *
 * From vmm_init onward the kernel runs under a full paging root, but the
 * real IDT is only installed by interrupts_init, much later. Any CPU
 * exception in that window (a page fault, an illegal instruction, ...)
 * would otherwise triple-fault with no information. Install a minimal
 * 32-vector IDT first: each stub reports the vector, error code,
 * faulting RIP and CR2 on the serial console and halts. interrupts_init
 * replaces this table when it runs.
 */
struct early_idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  reserved;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

_Static_assert(sizeof(struct early_idt_gate) == 16, "x86-64 IDT gate size");
_Static_assert(__builtin_offsetof(struct early_idt_gate, zero) == 12,
               "x86-64 IDT reserved word offset");

struct early_idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern void early_stub_0(void);
extern void early_stub_1(void);
extern void early_stub_2(void);
extern void early_stub_3(void);
extern void early_stub_4(void);
extern void early_stub_5(void);
extern void early_stub_6(void);
extern void early_stub_7(void);
extern void early_stub_8(void);
extern void early_stub_9(void);
extern void early_stub_10(void);
extern void early_stub_11(void);
extern void early_stub_12(void);
extern void early_stub_13(void);
extern void early_stub_14(void);
extern void early_stub_15(void);
extern void early_stub_16(void);
extern void early_stub_17(void);
extern void early_stub_18(void);
extern void early_stub_19(void);
extern void early_stub_20(void);
extern void early_stub_21(void);
extern void early_stub_22(void);
extern void early_stub_23(void);
extern void early_stub_24(void);
extern void early_stub_25(void);
extern void early_stub_26(void);
extern void early_stub_27(void);
extern void early_stub_28(void);
extern void early_stub_29(void);
extern void early_stub_30(void);
extern void early_stub_31(void);

static struct early_idt_gate early_gate[32];
static struct early_idtr early_descriptor;

extern void serial_write_u64_public(uint64_t value);

void early_fatal_dispatch(uint64_t vector, uint64_t error_code,
                          uint64_t rip, uint64_t cr2) {
    serial_write_public("ZEROOS EARLY FATAL: vector=");
    serial_write_u64_public(vector);
    serial_write_public(" err=");
    serial_write_u64_public(error_code);
    serial_write_public(" rip=");
    serial_write_u64_public(rip);
    serial_write_public(" cr2=");
    serial_write_u64_public(cr2);
    serial_write_public("\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

static void early_idt_install(void) {
    static void *stub[32] = {
        (void *)early_stub_0,  (void *)early_stub_1,  (void *)early_stub_2,
        (void *)early_stub_3,  (void *)early_stub_4,  (void *)early_stub_5,
        (void *)early_stub_6,  (void *)early_stub_7,  (void *)early_stub_8,
        (void *)early_stub_9,  (void *)early_stub_10, (void *)early_stub_11,
        (void *)early_stub_12, (void *)early_stub_13, (void *)early_stub_14,
        (void *)early_stub_15, (void *)early_stub_16, (void *)early_stub_17,
        (void *)early_stub_18, (void *)early_stub_19, (void *)early_stub_20,
        (void *)early_stub_21, (void *)early_stub_22, (void *)early_stub_23,
        (void *)early_stub_24, (void *)early_stub_25, (void *)early_stub_26,
        (void *)early_stub_27, (void *)early_stub_28, (void *)early_stub_29,
        (void *)early_stub_30, (void *)early_stub_31
    };
    struct early_idt_gate *gate = early_gate;

    for (uint64_t i = 0; i < 32; ++i) {
        uint64_t base = (uint64_t)stub[i];
        gate[i].offset_low = (uint16_t)(base & 0xffff);
        gate[i].selector = 0x08;
        gate[i].reserved = 0;
        gate[i].zero = 0;
        gate[i].type_attr = 0x8E; /* present, DPL0, 64-bit interrupt gate */
        gate[i].offset_mid = (uint16_t)((base >> 16) & 0xffff);
        gate[i].offset_high = (uint32_t)((base >> 32) & 0xffffffff);
    }
    early_descriptor.limit = (uint16_t)(sizeof(early_gate) - 1);
    early_descriptor.base = (uint64_t)early_gate;
    __asm__ volatile ("lidt %0" : : "m"(early_descriptor));

    /*
     * Verify the descriptor the CPU actually holds: a corrupted load
     * would make every exception dispatch #GP into a silent triple
     * fault, so this must be checked, not assumed.
     */
    struct early_idtr readback;
    __asm__ volatile ("sidt %0" : "=m"(readback));
    if (readback.limit != early_descriptor.limit ||
        readback.base != early_descriptor.base) {
        serial_write_public("ZEROOS PANIC: early IDT descriptor mismatch.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    serial_write_public("ZEROOS: early fatal IDT installed.\n");
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

static void heap_self_test(void) {
    uint64_t capacity=heap_capacity_bytes();
    void *p1,*p2,*p3,*big;

    if (capacity<ZEROOS_HEAP_MIN_ALLOC)
        kernel_panic("heap capacity self-test failed");


    /* Tiny and odd-sized allocations, 16-byte alignment, full-payload writes. */
    p1=kmalloc(1); p2=kmalloc(31); p3=kmalloc(1000);
    if (!p1 || !p2 || !p3 || p1==p2 || p2==p3 || p1==p3)
        kernel_panic("heap basic allocation failed");
    if (((uint64_t)p1| (uint64_t)p2 | (uint64_t)p3) & 15ULL)
        kernel_panic("heap alignment self-test failed");
    for (uint64_t i=0;i<1;++i) ((uint8_t *)p1)[i]=0xA5;
    for (uint64_t i=0;i<31;++i) ((uint8_t *)p2)[i]=(uint8_t)i;
    for (uint64_t i=0;i<1000;++i) ((uint8_t *)p3)[i]=(uint8_t)(i*7);
    for (uint64_t i=0;i<31;++i) if (((uint8_t *)p2)[i]!=(uint8_t)i)
        kernel_panic("heap payload content self-test failed");
    if (kfree(p1)!=0 || kfree(p2)!=0 || kfree(p3)!=0)
        kernel_panic("heap basic free failed");

    /* Negative tests: double free, NULL, unaligned, out-of-region. */
    if (kfree(p1)!=-1)
        kernel_panic("heap double-free detection failed");
    if (kfree((void *)0)!=-1)
        kernel_panic("heap NULL free rejection failed");
    if (kfree((void *)0x1)!=-1)
        kernel_panic("heap unaligned free rejection failed");
    if (kfree((void *)0xdeadbeef00ULL)!=-1)
        kernel_panic("heap out-of-region free rejection failed");
    p2=kmalloc(32);
    if (!p2) kernel_panic("heap re-allocation after free failed");
    if (kfree((uint8_t *)p2+3)!=-1)
        kernel_panic("heap misaligned pointer free rejection failed");
    if (kfree(p2)!=0)
        kernel_panic("heap aligned free after partial reject failed");

    /*
     * Boundary: an allocation whose header plus payload would exceed the
     * whole region must be rejected. capacity itself fits exactly
     * (header + capacity == region size) and must succeed, consuming the
     * region as one block.
     */
    void *exact=kmalloc(capacity);
    if (!exact)
        kernel_panic("heap exact-capacity allocation failed");
    /* kmalloc does not zero memory: verify the payload is writable. */
    ((uint8_t *)exact)[0]=0xC3;
    if (((uint8_t *)exact)[0]!=0xC3)
        kernel_panic("heap exact-capacity payload failed");
    if (kfree(exact)!=0)
        kernel_panic("heap exact-capacity free failed");
    if (kmalloc(capacity+1))
        kernel_panic("heap oversized allocation rejection failed");
    if (kmalloc(~0ULL))
        kernel_panic("heap huge allocation rejection failed");

    /* Near-maximum allocation covering essentially the whole region. */
    big=kmalloc(capacity-ZEROOS_HEAP_MIN_ALLOC);
    if (!big)
        kernel_panic("heap near-maximum allocation failed");
    for (uint64_t i=0;i<capacity-ZEROOS_HEAP_MIN_ALLOC;++i) {
        ((uint8_t *)big)[i]=0x5A;
    }
    for (uint64_t i=0;i<capacity-ZEROOS_HEAP_MIN_ALLOC;++i)
        if (((uint8_t *)big)[i]!=0x5A)
            kernel_panic("heap near-maximum payload failed");
    if (kfree(big)!=0)
        kernel_panic("heap near-maximum free failed");

    /* kcalloc zero-fill. */
    void *zeroed=kcalloc(128,8);
    if (!zeroed) kernel_panic("kcalloc failed");
    for (uint64_t i=0;i<1024;++i)
        if (((uint8_t *)zeroed)[i]!=0)
            kernel_panic("kcalloc zero-fill failed");
    if (kfree(zeroed)!=0)
        kernel_panic("kcalloc free failed");

    /* Overflow-safe count: count*size must not wrap. */
    if (kcalloc(~0ULL,8))
        kernel_panic("kcalloc overflow rejection failed");

    /*
     * Exhaustion: fill the whole region with 64-byte blocks (96-byte blocks
     * including the header), then drain. The hold array is sized for the
     * maximum 4 MiB region.
     */
    static void *exhaust[45000];
    uint64_t allocated=0;
    while (allocated<45000) {
        void *block=kmalloc(64);
        if (!block) break;
        ((uint8_t *)block)[0]=(uint8_t)allocated;
        exhaust[allocated++]=block;
    }
    if (allocated<100)
        kernel_panic("heap exhaustion self-test could not fill region");
    while (kmalloc(64))
        kernel_panic("heap exhaustion did not return NULL");
    for (uint64_t i=0;i<allocated;++i)
        if (kfree(exhaust[i])!=0)
            kernel_panic("heap exhaustion drain failed");
    if (heap_used_bytes()!=0 || heap_validate()!=0)
        kernel_panic("heap exhaustion accounting failed");

    /* Bounded deterministic stress: interleaved alloc/free (LCG sequence). */
    static void *live[2048];
    uint64_t live_count=0, lcg=0x2545F4914F6CDD1DULL;
    for (uint64_t iter=0;iter<20000;++iter) {
        lcg=lcg*6364136223846793005ULL+1ULL;
        uint64_t op=lcg>>58;
        if (op<52 || live_count==0) {
            uint64_t size=(lcg>>30)&0x1FF;
            if (size==0) size=1;
            void *block=kmalloc(size);
            if (block) {
                ((uint8_t *)block)[0]=(uint8_t)iter;
                if (live_count<2048) live[live_count++]=block;
                else if (kfree(block)!=0)
                    kernel_panic("heap stress free failed");
            }
        } else {
            uint64_t slot=(lcg>>30)%live_count;
            if (kfree(live[slot])!=0)
                kernel_panic("heap stress free failed");
            live[slot]=live[--live_count];
        }
    }
    for (uint64_t i=0;i<live_count;++i)
        if (kfree(live[i])!=0)
            kernel_panic("heap stress drain failed");
    if (heap_used_bytes()!=0 || heap_validate()!=0)
        kernel_panic("heap stress accounting failed");

    serial_write_public("ZEROOS: heap capacity: ");
    serial_write_u64(capacity);
    serial_write_public(" bytes.\n");
    serial_write_public("ZEROOS: heap self-test passed.\n");
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

/*
 * Stage-1 ring-3 certification. Runs as a kernel task after the scheduler
 * is live. Each step spawns a deterministic user process, waits for its
 * termination with a bounded tick budget, and verifies the expected
 * outcome. Any deviation panics, which fails the QEMU boot test.
 */
static uint64_t ring3_failures;

static void ring3_run_case(uint64_t param, uint64_t budget_ticks,
                           int expect_fault, uint64_t *out_pid) {
    uint64_t pid=0;
    if (process_spawn(param,&pid)!=0) {
        ring3_failures++;
        kernel_panic("ring-3 case spawn failed");
    }
    if (process_wait_ticks(budget_ticks)!=0) {
        ring3_failures++;
        kernel_panic("ring-3 case timed out");
    }
    struct process *p=process_find(pid);
    if (!p || p->state!=PROCESS_ZOMBIE) {
        ring3_failures++;
        kernel_panic("ring-3 case not zombie after wait");
    }
    if (p->exited_by_fault!=((uint8_t)expect_fault)) {
        ring3_failures++;
        kernel_panic("ring-3 case fault expectation mismatch");
    }
    *out_pid=pid;
}

static void ring3_orchestrator(void *argument) {
    (void)argument;
    uint64_t deadline=timer_ticks()+1200;
    uint64_t pid=0;

    /* Case 0: hello — prove ring-3 execution and the write/getpid/gettid
     * syscall path end to end. */
    ring3_run_case(0,300,0,&pid);
    {
        struct process *p=process_find(pid);
        if (!p || p->exit_code!=0)
            kernel_panic("ring-3 hello exit code mismatch");
    }
    if (process_reap(pid)!=0)
        kernel_panic("ring-3 hello reap failed");
    serial_write_public("ZEROOS: ring-3 hello process verified.\n");

    /* Case 1 (A) and case 2 (B): address-space isolation. Both processes
     * map ZEROOS_USER_DATA_VA to their own physical page. A writes a
     * marker; B must read its own (zero) page, not A's. */
    ring3_run_case(1,300,0,&pid);
    uint64_t pid_a=pid;
    {
        struct process *a=process_find(pid_a);
        uint32_t marker=0;
        if (!a) kernel_panic("ring-3 process A vanished");
        marker=*(volatile uint32_t *)(uint64_t)a->data_phys;
        if (marker!=0xdeadbeef) {
            ring3_failures++;
            kernel_panic("ring-3 user store to data page not observed");
        }
    }

    ring3_run_case(2,300,0,&pid);
    uint64_t pid_b=pid;
    {
        struct process *a=process_find(pid_a);
        struct process *b=process_find(pid_b);
        if (!a || !b) kernel_panic("ring-3 isolation processes vanished");
        if (a->data_phys==b->data_phys) {
            ring3_failures++;
            kernel_panic("ring-3 isolation: two spaces share a physical page");
        }
        if (vmm_space_translate(&a->space,ZEROOS_USER_DATA_VA)!=a->data_phys) {
            ring3_failures++;
            kernel_panic("ring-3 isolation: A VA->PA mismatch");
        }
        if (vmm_space_translate(&b->space,ZEROOS_USER_DATA_VA)!=b->data_phys) {
            ring3_failures++;
            kernel_panic("ring-3 isolation: B VA->PA mismatch");
        }
        if (b->exited_by_fault || b->exit_code!=0) {
            ring3_failures++;
            kernel_panic("ring-3 reader process failed");
        }
    }
    if (process_reap(pid_a)!=0 || process_reap(pid_b)!=0)
        kernel_panic("ring-3 isolation reap failed");
    serial_write_public("ZEROOS: per-process address-space isolation verified.\n");

    /* Case 3: contained page fault from ring 3. */
    ring3_run_case(3,300,1,&pid);
    if (process_reap(pid)!=0)
        kernel_panic("ring-3 fault case reap failed");
    serial_write_public("ZEROOS: ring-3 page-fault containment verified.\n");

    /* Case 4: contained general protection from ring 3. */
    ring3_run_case(4,300,1,&pid);
    if (process_reap(pid)!=0)
        kernel_panic("ring-3 GP case reap failed");
    serial_write_public("ZEROOS: ring-3 general-protection containment verified.\n");

    /* Case 5: negative syscall validation — kernel pointer and length
     * overflow must be rejected with -1 while the process survives. */
    ring3_run_case(5,300,0,&pid);
    {
        struct process *p=process_find(pid);
        if (!p || p->exit_code!=0)
            kernel_panic("ring-3 negative syscall case failed");
    }
    if (process_reap(pid)!=0)
        kernel_panic("ring-3 negative case reap failed");
    serial_write_public("ZEROOS: user pointer validation verified.\n");

    if (timer_ticks()>deadline) {
        ring3_failures++;
        kernel_panic("ring-3 certification exceeded tick budget");
    }
    serial_write_public("ZEROOS: stage-1 ring-3 foundation certified.\n");
}

static void scheduler_self_test(void) {
    uint64_t worker_id, monitor_id, waiter_id, waker_id, sleeper_id;
    uint64_t preempt_a_id, preempt_b_id, lifecycle_id, ring3_id;

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
    if (task_create(ring3_orchestrator,0,&ring3_id)!=0)
        kernel_panic("ring-3 orchestrator creation failed");

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

    /* Cover allocator and VMM initialization as well as the heap. */
    early_idt_install();
#if ZEROOS_EARLY_FAULT_TEST == 6
    __asm__ volatile (".global early_fault_test_site\n"
                      "early_fault_test_site: ud2");
#elif ZEROOS_EARLY_FAULT_TEST == 14
    __asm__ volatile ("movabs $0x4000000000, %%rax\n"
                      ".global early_fault_test_site\n"
                      "early_fault_test_site: mov (%%rax), %%rax"
                      : : : "rax", "memory");
#endif
#if ZEROOS_LATE_FAULT_TEST
    gdt_init();
    interrupts_init();
    kernel_panic("late fault test returned");
#endif
    memory_init(multiboot_info);
    serial_write_public("ZEROOS: physical page allocator initialized.\n");
    serial_write_public("ZEROOS: managed pages: ");
    serial_write_u64(memory_total_pages());
    serial_write_public("\nZEROOS: free pages: ");
    serial_write_u64(memory_free_pages());
    serial_write_public("\n");

    memory_self_test();

    if (vmm_init()!=0) kernel_panic("virtual memory initialization failed (NX required)");
    serial_write_public("ZEROOS: virtual memory manager initialized.\n");
    vmm_self_test();

    serial_write_public("ZEROOS: heap init starting.\n");

    /*
     * The heap must be live before the per-address-space self-test:
     * space page-ownership tracking allocates its descriptors from the
     * heap.
     */
    if (heap_init()!=0) kernel_panic("kernel heap initialization failed");
    serial_write_public("ZEROOS: kernel heap initialized.\n");
    serial_write_public("ZEROOS: heap self-test starting.\n");
    heap_self_test();

    vmm_space_self_test();

    sync_self_test();

    gdt_init();
    serial_write_public("ZEROOS: GDT extended (user segments) and TSS loaded.\n");

    interrupts_init();
    serial_write_public("ZEROOS: IDT installed and interrupts enabled.\n");
    serial_write_public("ZEROOS: PIT timer configured at ");
    serial_write_u64(timer_frequency_hz());
    serial_write_public(" Hz.\n");
    serial_write_public("ZEROOS: IRQ ownership layer initialized.\n");

    if (vmm_pcid_enabled())
        serial_write_public("ZEROOS: PCID TLB isolation enabled.\n");
    else
        serial_write_public("ZEROOS: PCID unavailable; full TLB flush mode.\n");

    syscall_init();
    serial_write_public("ZEROOS: SYSCALL/SYSRET syscall entry initialized.\n");

    if (process_system_init()!=0)
        kernel_panic("process system initialization failed");
    serial_write_public("ZEROOS: process system initialized.\n");

    serial_write_public("ZEROOS: foundation milestone reached.\n");

    scheduler_self_test();

    for (;;) __asm__ volatile ("sti; hlt");
}
