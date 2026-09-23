#include "smp.h"
#include "acpi.h"
#include "apic.h"
#include "cpu.h"
#include "gdt.h"
#include "interrupts.h"
#include "memory.h"
#include "timer.h"
#include "tlb.h"
#include "vmm.h"
#include "task.h"

extern void serial_write_public(const char *text);

extern char ap_trampoline_start[];
extern char ap_trampoline_end[];
extern char ap_trampoline_cr3[];
extern char ap_trampoline_entry[];
extern char ap_trampoline_stack[];
extern char ap_trampoline_cpu_id[];
extern char ap_trampoline_protected[];
extern char ap_trampoline_protected_far[];

static struct smp_cpu_record records[ZEROOS_MAX_CPUS];
static uint32_t discovered;
static uint32_t online;
static void *trampoline_page;
static uint8_t trampoline_vector;
static uint8_t initialized;
static uint8_t degraded;
#ifdef ZEROOS_TEST_SMP_FAILED_DISPATCH
static uint8_t startup_fault_injected;
static uint8_t startup_fault_retried;
#endif

#define SMP_STARTUP_ATTEMPTS 2U
#define SMP_TOKEN_CPU_MASK 0xffffU
#define CR0_PE (1ULL << 0)
#define CR0_EM (1ULL << 2)
#define CR0_MP (1ULL << 1)
#define CR0_NE (1ULL << 5)
#define CR0_WP (1ULL << 16)
#define CR0_NW (1ULL << 29)
#define CR0_CD (1ULL << 30)
#define CR0_PG (1ULL << 31)
#define EFER_LME (1ULL << 8)
#define EFER_LMA (1ULL << 10)
#define EFER_NXE (1ULL << 11)

static void atomic_store_u32(uint32_t *value, uint32_t new_value) {
    __atomic_store_n(value,new_value,__ATOMIC_RELEASE);
}

static uint32_t atomic_load_u32(const uint32_t *value) {
    return __atomic_load_n(value,__ATOMIC_ACQUIRE);
}

static void smp_write_u64(uint64_t value) {
    char buffer[21];
    int position=20;
    buffer[position]='\0';
    if (value==0) {
        serial_write_public("0");
        return;
    }
    while (value && position>0) {
        buffer[--position]=(char)('0'+(value%10ULL));
        value/=10ULL;
    }
    serial_write_public(&buffer[position]);
}

static uint64_t trampoline_offset(const char *symbol) {
    return (uint64_t)symbol-(uint64_t)ap_trampoline_start;
}

static int trampoline_prepare(void) {
    uint64_t size=(uint64_t)ap_trampoline_end-
                  (uint64_t)ap_trampoline_start;
    if (size==0 || size>ZEROOS_PAGE_SIZE)
        return -1;

    trampoline_page=page_alloc_below(0x100000ULL);
    if (!trampoline_page)
        return -1;
    uint8_t *copy=(uint8_t *)trampoline_page;
    const uint8_t *source=(const uint8_t *)(uint64_t)ap_trampoline_start;
    for (uint64_t i=0; i<size; ++i)
        copy[i]=source[i];

    uint64_t physical=(uint64_t)trampoline_page;
    if ((physical&0xfffULL)!=0 || physical>=0x100000ULL)
        return -1;

    uint64_t offset=trampoline_offset(ap_trampoline_cr3);
    *(uint64_t *)(copy+offset)=vmm_root();
    offset=trampoline_offset(ap_trampoline_entry);
    *(uint64_t *)(copy+offset)=(uint64_t)smp_ap_entry;
    /* Enter protected mode through an absolute far pointer. Flat GDT
     * descriptors then leave the transition independent of descriptor-base
     * patching. */
    offset=trampoline_offset(ap_trampoline_protected_far);
    *(uint32_t *)(copy+offset)=(uint32_t)physical+
                               (uint32_t)trampoline_offset(ap_trampoline_protected);
    *(uint16_t *)(copy+offset+4ULL)=0x08U;

    trampoline_vector=(uint8_t)(physical>>12);
    return 0;
}

static int smp_send_tlb_ipi(uint64_t target_mask, uint8_t vector) {
    int sent=0;
    for (uint32_t i=0; i<discovered; ++i) {
        if (!(target_mask&(1ULL<<i)))
            continue;
        if (atomic_load_u32(&records[i].state)!=ZEROOS_SMP_CPU_ONLINE &&
            atomic_load_u32(&records[i].state)!=ZEROOS_SMP_CPU_STARTING)
            return -1;
        if (apic_send_ipi(records[i].apic_id,vector)!=0)
            return -1;
        sent=1;
    }
    return sent || target_mask==0 ? 0 : -1;
}

static void smp_ap_park(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static int smp_ap_token_matches(uint32_t cpu_id, uint32_t generation) {
    return cpu_id>0 && cpu_id<discovered && generation!=0 &&
           atomic_load_u32(&records[cpu_id].startup_generation)==generation;
}

static int smp_ap_generation_matches(uint32_t cpu_id, uint32_t generation) {
    return smp_ap_token_matches(cpu_id,generation) &&
           atomic_load_u32(&records[cpu_id].state)==ZEROOS_SMP_CPU_STARTING;
}

static void smp_ap_fail(uint32_t cpu_id, uint32_t generation) {
    if (smp_ap_token_matches(cpu_id,generation)) {
        if (atomic_load_u32(&records[cpu_id].state)==ZEROOS_SMP_CPU_STARTING)
            atomic_store_u32(&records[cpu_id].state,ZEROOS_SMP_CPU_FAILED);
        /* The TLB mask is independent of the SMP record state. Remove this
         * CPU before parking so a later BSP shootdown cannot wait forever,
         * including when the BSP timed out between AP initialization steps. */
        (void)tlb_unregister_current_cpu(cpu_id);
        (void)cpu_mark_offline(cpu_id);
    }
    smp_ap_park();
}

void smp_ap_entry(uint32_t startup_token) {
    uint32_t cpu_id=startup_token&SMP_TOKEN_CPU_MASK;
    uint32_t generation=startup_token>>16;

    if (!smp_ap_generation_matches(cpu_id,generation))
        smp_ap_park();

    if (cpu_mark_online(cpu_id)!=0)
        smp_ap_fail(cpu_id,generation);
    if (cpu_current_id()!=cpu_id)
        smp_ap_fail(cpu_id,generation);
    if (!smp_ap_generation_matches(cpu_id,generation)) {
        (void)cpu_mark_offline(cpu_id);
        smp_ap_park();
    }
    if (gdt_init_cpu(cpu_id)!=0)
        smp_ap_fail(cpu_id,generation);
    /* Install the per-CPU IDT before touching device state so an AP startup
     * fault is contained and diagnosable rather than triple-faulting. */
    interrupts_load_current_cpu();
    {
        struct __attribute__((packed)) {
            uint16_t limit;
            uint64_t base;
        } idtr;
        uint64_t cr0, cr3;
        __asm__ volatile ("mov %%cr0,%0" : "=r"(cr0) : : "memory");
        __asm__ volatile ("mov %%cr3,%0" : "=r"(cr3) : : "memory");
        __asm__ volatile ("sidt %0" : "=m"(idtr) : : "memory");
        records[cpu_id].startup_cr0=cr0;
        records[cpu_id].startup_cr3=cr3;
        records[cpu_id].startup_efer=cpu_read_msr(0xc0000080U);
        records[cpu_id].startup_idt_base=idtr.base;
        records[cpu_id].startup_idt_limit=idtr.limit;
    }
    if (apic_cpu_init()!=0)
        smp_ap_fail(cpu_id,generation);
    if (apic_local_id()!=records[cpu_id].apic_id)
        smp_ap_fail(cpu_id,generation);
    if (tlb_register_cpu(cpu_id)!=0 || tlb_set_current_cpu(cpu_id)!=0)
        smp_ap_fail(cpu_id,generation);

    if (!smp_ap_generation_matches(cpu_id,generation))
        smp_ap_fail(cpu_id,generation);
    atomic_store_u32(&records[cpu_id].state,ZEROOS_SMP_CPU_ONLINE);
    __atomic_fetch_add(&online,1,__ATOMIC_ACQ_REL);

    /* The AP remains in its private bootstrap context until the BSP has
     * initialized task queues and published the scheduler start gate. */
    task_start_secondary_cpu();
    for (;;) {
        __asm__ volatile ("cli; hlt" : : : "memory");
    }
}

static void smp_cleanup_failed_ap(uint32_t cpu_id) {
    /* A failed AP is not a scheduler participant. The BSP is allowed to
     * remove stale registration state after the bounded attempt, and the next
     * INIT in a retry resets a late trampoline execution. */
    (void)tlb_unregister_cpu(cpu_id);
    (void)cpu_mark_offline(cpu_id);
}

static int smp_start_ap(uint32_t cpu_id) {
    uint8_t *copy=(uint8_t *)trampoline_page;

    for (uint32_t attempt=1; attempt<=SMP_STARTUP_ATTEMPTS; ++attempt) {
        uint32_t generation=records[cpu_id].startup_generation+1U;
        if (generation==0 || generation>0xffffU)
            generation=1U;

        if (attempt>1U) {
            /* The INIT at the start of this dispatch is the recovery reset.
             * Publish PREPARED before it so a late first-attempt AP cannot
             * acknowledge the new attempt with an old token. */
            smp_cleanup_failed_ap(cpu_id);
            atomic_store_u32(&records[cpu_id].state,
                             ZEROOS_SMP_CPU_PREPARED);
        }

        atomic_store_u32(&records[cpu_id].startup_generation,generation);
        records[cpu_id].startup_attempts=attempt;
        uint64_t field=trampoline_offset(ap_trampoline_stack);
        *(uint64_t *)(copy+field)=records[cpu_id].bootstrap_stack+
                                   ZEROOS_PAGE_SIZE;
        field=trampoline_offset(ap_trampoline_cpu_id);
        *(uint32_t *)(copy+field)=(generation<<16)|cpu_id;
        atomic_store_u32(&records[cpu_id].state,ZEROOS_SMP_CPU_STARTING);

        serial_write_public("ZEROOS: SMP INIT/SIPI dispatch started (cpu=");
        smp_write_u64(cpu_id);
        serial_write_public(", attempt=");
        smp_write_u64(attempt);
        serial_write_public(").\n");
#ifdef ZEROOS_TEST_SMP_FAILED_DISPATCH
        /* Fault validation deliberately exercises the same bounded recovery
         * path as a failed IPI delivery, without starting a first-attempt AP.
         * The retry must publish a new generation before the real dispatch. */
        if (!startup_fault_injected && attempt==1U) {
            startup_fault_injected=1;
            atomic_store_u32(&records[cpu_id].state,ZEROOS_SMP_CPU_FAILED);
            smp_cleanup_failed_ap(cpu_id);
            serial_write_public("ZEROOS: injected SMP dispatch failure for recovery validation.\n");
            continue;
        }
        if (attempt>1U)
            startup_fault_retried=1;
#endif
        if (apic_send_init_sipi(records[cpu_id].apic_id,trampoline_vector)!=0) {
            atomic_store_u32(&records[cpu_id].state,ZEROOS_SMP_CPU_FAILED);
            smp_cleanup_failed_ap(cpu_id);
            serial_write_public("ZEROOS: SMP INIT/SIPI dispatch failed (cpu=");
            smp_write_u64(cpu_id);
            serial_write_public(").\n");
            continue;
        }
        serial_write_public("ZEROOS: SMP INIT/SIPI dispatch completed (cpu=");
        smp_write_u64(cpu_id);
        serial_write_public(").\n");

        uint8_t timed_out=0;
        uint64_t wait_ticks=timer_ticks();
        for (uint64_t spins=0; spins<100000ULL; ++spins) {
            uint32_t state=atomic_load_u32(&records[cpu_id].state);
            if (state==ZEROOS_SMP_CPU_ONLINE)
                return 0;
            if (state==ZEROOS_SMP_CPU_FAILED)
                break;
            if (timer_ticks()-wait_ticks>=100ULL) {
                timed_out=1;
                break;
            }
            cpu_relax();
        }

        atomic_store_u32(&records[cpu_id].state,ZEROOS_SMP_CPU_FAILED);
        smp_cleanup_failed_ap(cpu_id);
        serial_write_public(timed_out ?
                            "ZEROOS: SMP AP acknowledgement timed out (cpu=" :
                            "ZEROOS: SMP AP reported startup failure (cpu=");
        smp_write_u64(cpu_id);
        serial_write_public(", attempt=");
        smp_write_u64(attempt);
        serial_write_public(").\n");
    }
    return -1;
}

int smp_init(void) {
    serial_write_public("ZEROOS: SMP initialization entered.\n");
    if (initialized)
        return 0;
    initialized=1;
    degraded=0;
    discovered=0;
    online=0;
    records[0]=(struct smp_cpu_record){
        .cpu_id=0,
        .apic_id=apic_local_id(),
        .state=ZEROOS_SMP_CPU_ONLINE,
        .bootstrap_stack=0
    };
    discovered=1;
    online=1;
    serial_write_public("ZEROOS: SMP BSP record initialized.\n");

    const struct acpi_info *firmware=acpi_info();
    if (!firmware->valid || firmware->processor_count<=1) {
        serial_write_public("ZEROOS: SMP topology has no startable secondary CPUs.\n");
        return 0;
    }
    if (!apic_available()) {
        degraded=1;
        serial_write_public("ZEROOS: SMP topology requires a usable Local APIC; BSP-only recovery selected.\n");
        return 0;
    }
    serial_write_public("ZEROOS: SMP AP preparation started.\n");

    for (uint32_t i=0; i<firmware->processor_count; ++i) {
        uint32_t apic_id=firmware->processors[i].apic_id;
        if (apic_id==records[0].apic_id)
            continue;
        if (discovered>=ZEROOS_MAX_CPUS) {
            degraded=1;
            serial_write_public("ZEROOS: ACPI processor topology exceeds SMP capacity; remaining CPUs left offline.\n");
            break;
        }
        uint32_t cpu_id=discovered;
        records[cpu_id]=(struct smp_cpu_record){
            .cpu_id=cpu_id,
            .apic_id=apic_id,
            .state=ZEROOS_SMP_CPU_PREPARED,
            .bootstrap_stack=0
        };
        if (cpu_prepare_local(cpu_id,apic_id)!=0)
            atomic_store_u32(&records[cpu_id].state,ZEROOS_SMP_CPU_FAILED);
        else {
            records[cpu_id].bootstrap_stack=(uint64_t)page_alloc();
            if (!records[cpu_id].bootstrap_stack)
                atomic_store_u32(&records[cpu_id].state,ZEROOS_SMP_CPU_FAILED);
        }
        ++discovered;
    }
    serial_write_public("ZEROOS: SMP AP records prepared.\n");

    if (discovered==1) {
        serial_write_public("ZEROOS: SMP topology has no startable secondary CPUs.\n");
        return 0;
    }
    serial_write_public("ZEROOS: SMP trampoline preparation started.\n");
    if (trampoline_prepare()!=0) {
        for (uint32_t i=1; i<discovered; ++i)
            atomic_store_u32(&records[i].state,ZEROOS_SMP_CPU_FAILED);
        degraded=1;
        serial_write_public("ZEROOS: SMP AP trampoline unavailable; BSP-only recovery selected.\n");
        return 0;
    }
    serial_write_public("ZEROOS: SMP trampoline prepared.\n");
    if (tlb_install_ipi_sender(smp_send_tlb_ipi)!=0) {
        for (uint32_t i=1; i<discovered; ++i)
            atomic_store_u32(&records[i].state,ZEROOS_SMP_CPU_FAILED);
        degraded=1;
        serial_write_public("ZEROOS: SMP TLB IPI sender unavailable; BSP-only recovery selected.\n");
        return 0;
    }
    serial_write_public("ZEROOS: SMP TLB IPI sender installed.\n");

    uint8_t failed=0;
    for (uint32_t i=1; i<discovered; ++i) {
        if (atomic_load_u32(&records[i].state)!=ZEROOS_SMP_CPU_PREPARED) {
            failed=1;
            continue;
        }
        if (smp_start_ap(i)!=0)
            failed=1;
    }

    if (failed) {
        degraded=1;
        serial_write_public("ZEROOS: SMP AP startup degraded; failed CPUs remain offline.\n");
    }

    serial_write_public("ZEROOS: SMP AP startup boundary completed (online=");
    /* Avoid a formatter dependency in the boot boundary; kernel diagnostics
     * report the detailed count through the public accessor below. */
    serial_write_public("see CPU topology self-test).\n");
    return 0;
}

uint32_t smp_discovered_count(void) {
    return discovered;
}

uint32_t smp_online_count(void) {
    return online;
}

int smp_is_degraded(void) {
    return degraded!=0;
}

int smp_startup_self_test(void) {
    if (!initialized || online==0 || cpu_online_count()!=online ||
        tlb_online_count()!=online)
        return -1;
    for (uint32_t i=0; i<discovered; ++i) {
        uint32_t state=atomic_load_u32(&records[i].state);
        if (records[i].cpu_id!=i)
            return -1;
        if (state==ZEROOS_SMP_CPU_ONLINE) {
            if (i!=0 && (records[i].startup_generation==0 ||
                         records[i].startup_attempts==0 ||
                         records[i].startup_attempts>SMP_STARTUP_ATTEMPTS))
                return -1;
            if (i!=0 &&
                (records[i].startup_cr3!=vmm_root() ||
                 (records[i].startup_cr0 &
                  (CR0_PE|CR0_MP|CR0_NE|CR0_WP|CR0_PG)) !=
                  (CR0_PE|CR0_MP|CR0_NE|CR0_WP|CR0_PG) ||
                 (records[i].startup_cr0 & (CR0_EM|CR0_CD|CR0_NW)) != 0 ||
                 (records[i].startup_efer & (EFER_LME|EFER_LMA)) !=
                  (EFER_LME|EFER_LMA) ||
                 ((records[i].startup_efer & EFER_NXE)!=0) !=
                  (cpu_has(ZEROOS_CPU_FEATURE_NX)!=0) ||
                 records[i].startup_idt_base==0 ||
                 records[i].startup_idt_limit < (256U*16U-1U) ||
                 !gdt_cpu_is_initialized(i)))
                return -1;
            if (!cpu_local_for_id(i) ||
                !__atomic_load_n(&cpu_local_for_id(i)->online,
                                 __ATOMIC_ACQUIRE))
                return -1;
        } else if (state==ZEROOS_SMP_CPU_FAILED) {
            if (records[i].startup_attempts>SMP_STARTUP_ATTEMPTS)
                return -1;
        } else if (i!=0) {
            return -1;
        }
    }
    /* Exercise the actual remote IPI/ack path after APs are published. The
     * pre-SMP VMM self-test can only certify the BSP-local fast path. */
    if (online>1 && tlb_flush_all()!=0)
        return -1;
    return 0;
}

int smp_startup_recovery_self_test(void) {
    if (!initialized || online==0 || cpu_online_count()!=online ||
        tlb_online_count()!=online)
        return -1;

    for (uint32_t i=1; i<discovered; ++i) {
        uint32_t state=atomic_load_u32(&records[i].state);
        uint32_t generation=atomic_load_u32(&records[i].startup_generation);
        struct cpu_local *local=cpu_local_for_id(i);

        /* A failed AP is quarantined from both CPU-local and TLB ownership;
         * this is the recovery invariant that makes later shootdowns safe. */
        if (state==ZEROOS_SMP_CPU_FAILED &&
            ((!local) || __atomic_load_n(&local->online,__ATOMIC_ACQUIRE) ||
             tlb_cpu_is_online(i)))
            return -1;

        if (generation==0)
            continue;

        /* A late trampoline from the preceding generation must never match
         * the published attempt. Generation zero is intentionally invalid,
         * so it is also a useful first-generation stale-token probe. */
        uint32_t stale=generation>1U ? generation-1U : 0U;
        if (smp_ap_token_matches(i,stale))
            return -1;

        /* Token identity alone is not enough: final states must reject entry
         * unless the record is explicitly STARTING. */
        if (!smp_ap_token_matches(i,generation) ||
            smp_ap_generation_matches(i,generation))
            return -1;
    }

#ifdef ZEROOS_TEST_SMP_FAILED_DISPATCH
    if (!startup_fault_injected || !startup_fault_retried)
        return -1;
#endif
    return 0;
}

const struct smp_cpu_record *smp_cpu_record(uint32_t cpu_id) {
    return cpu_id<discovered ? &records[cpu_id] : (const struct smp_cpu_record *)0;
}

uint64_t smp_bootstrap_stack(uint32_t cpu_id) {
    const struct smp_cpu_record *record=smp_cpu_record(cpu_id);
    return record ? record->bootstrap_stack : 0;
}
