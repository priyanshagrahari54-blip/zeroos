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

extern void serial_write_public(const char *text);

extern char ap_trampoline_start[];
extern char ap_trampoline_end[];
extern char ap_trampoline_cr3[];
extern char ap_trampoline_entry[];
extern char ap_trampoline_stack[];
extern char ap_trampoline_cpu_id[];
extern char ap_trampoline_gdt[];

static struct smp_cpu_record records[ZEROOS_MAX_CPUS];
static uint32_t discovered;
static uint32_t online;
static void *trampoline_page;
static uint8_t trampoline_vector;
static uint8_t initialized;

static void atomic_store_u32(uint32_t *value, uint32_t new_value) {
    __atomic_store_n(value,new_value,__ATOMIC_RELEASE);
}

static uint32_t atomic_load_u32(const uint32_t *value) {
    return __atomic_load_n(value,__ATOMIC_ACQUIRE);
}

static uint64_t trampoline_offset(const char *symbol) {
    return (uint64_t)symbol-(uint64_t)ap_trampoline_start;
}

static void trampoline_patch_segment_base(uint8_t *copy,
                                          uint32_t index,
                                          uint32_t base) {
    uint64_t offset=trampoline_offset(ap_trampoline_gdt)+index*8ULL;
    uint64_t descriptor=*(uint64_t *)(copy+offset);
    descriptor&=~((0xffffffULL<<16)|(0xffULL<<56));
    descriptor|=((uint64_t)(base&0xffffffU)<<16)|
                ((uint64_t)((base>>24)&0xffU)<<56);
    *(uint64_t *)(copy+offset)=descriptor;
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
    /* Protected-mode segment bases keep the copied code/data page addressable
     * before long mode ignores the code-segment base. */
    trampoline_patch_segment_base(copy,1,(uint32_t)physical);
    trampoline_patch_segment_base(copy,2,(uint32_t)physical);

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

static void smp_ap_fail(uint32_t cpu_id) {
    if (cpu_id<ZEROOS_MAX_CPUS) {
        atomic_store_u32(&records[cpu_id].state,ZEROOS_SMP_CPU_FAILED);
        if (cpu_online_count()>1)
            (void)cpu_mark_offline(cpu_id);
    }
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void smp_ap_entry(uint32_t cpu_id) {
    if (cpu_id==0 || cpu_id>=discovered ||
        records[cpu_id].cpu_id!=cpu_id)
        smp_ap_fail(cpu_id);

    if (cpu_mark_online(cpu_id)!=0)
        smp_ap_fail(cpu_id);
    if (gdt_init_cpu(cpu_id)!=0)
        smp_ap_fail(cpu_id);
    if (apic_cpu_init()!=0)
        smp_ap_fail(cpu_id);
    interrupts_load_current_cpu();
    if (tlb_register_cpu(cpu_id)!=0 || tlb_set_current_cpu(cpu_id)!=0)
        smp_ap_fail(cpu_id);

    atomic_store_u32(&records[cpu_id].state,ZEROOS_SMP_CPU_ONLINE);
    __atomic_fetch_add(&online,1,__ATOMIC_ACQ_REL);

    for (;;) {
        __asm__ volatile ("sti; hlt" : : : "memory");
    }
}

int smp_init(void) {
    if (initialized)
        return 0;
    initialized=1;
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

    const struct acpi_info *firmware=acpi_info();
    if (!firmware->valid || firmware->processor_count<=1) {
        serial_write_public("ZEROOS: SMP topology has no startable secondary CPUs.\n");
        return 0;
    }
    if (!apic_available()) {
        serial_write_public("ZEROOS: SMP topology requires a usable Local APIC.\n");
        return -1;
    }

    for (uint32_t i=0; i<firmware->processor_count; ++i) {
        uint32_t apic_id=firmware->processors[i].apic_id;
        if (apic_id==records[0].apic_id)
            continue;
        if (discovered>=ZEROOS_MAX_CPUS) {
            serial_write_public("ZEROOS: ACPI processor topology exceeds SMP capacity.\n");
            return -1;
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

    if (discovered==1) {
        serial_write_public("ZEROOS: SMP topology has no startable secondary CPUs.\n");
        return 0;
    }
    if (trampoline_prepare()!=0) {
        for (uint32_t i=1; i<discovered; ++i)
            atomic_store_u32(&records[i].state,ZEROOS_SMP_CPU_FAILED);
        serial_write_public("ZEROOS: SMP AP trampoline unavailable.\n");
        return -1;
    }
    if (tlb_install_ipi_sender(smp_send_tlb_ipi)!=0) {
        serial_write_public("ZEROOS: SMP TLB IPI sender unavailable.\n");
        return -1;
    }

    uint8_t failed=0;
    for (uint32_t i=1; i<discovered; ++i) {
        if (atomic_load_u32(&records[i].state)!=ZEROOS_SMP_CPU_PREPARED) {
            failed=1;
            continue;
        }
        uint64_t field=trampoline_offset(ap_trampoline_stack);
        uint8_t *copy=(uint8_t *)trampoline_page;
        *(uint64_t *)(copy+field)=records[i].bootstrap_stack+
                                   ZEROOS_PAGE_SIZE;
        field=trampoline_offset(ap_trampoline_cpu_id);
        *(uint32_t *)(copy+field)=i;
        atomic_store_u32(&records[i].state,ZEROOS_SMP_CPU_STARTING);

        if (apic_send_init_sipi(records[i].apic_id,trampoline_vector)!=0) {
            atomic_store_u32(&records[i].state,ZEROOS_SMP_CPU_FAILED);
            failed=1;
            continue;
        }

        uint8_t started=0;
        uint64_t wait_ticks=timer_ticks();
        for (uint64_t spins=0; spins<5000000ULL; ++spins) {
            uint32_t state=atomic_load_u32(&records[i].state);
            if (state==ZEROOS_SMP_CPU_ONLINE) {
                started=1;
                break;
            }
            if (state==ZEROOS_SMP_CPU_FAILED ||
                timer_ticks()-wait_ticks>=100ULL)
                break;
            cpu_relax();
        }
        if (!started) {
            atomic_store_u32(&records[i].state,ZEROOS_SMP_CPU_FAILED);
            failed=1;
        }
    }

    if (failed)
        return -1;

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

const struct smp_cpu_record *smp_cpu_record(uint32_t cpu_id) {
    return cpu_id<discovered ? &records[cpu_id] : (const struct smp_cpu_record *)0;
}
