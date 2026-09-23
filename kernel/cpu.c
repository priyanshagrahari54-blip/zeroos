#include "cpu.h"

#define CPUID_VENDOR 0x00000000U
#define CPUID_FEATURES 0x00000001U
#define CPUID_BASIC_EXT_FEATURES 0x00000007U
#define CPUID_TSC_INFO 0x00000015U
#define CPUID_BASE_FREQ 0x00000016U
#define CPUID_EXT_LIMIT 0x80000000U
#define CPUID_EXT_FEATURES 0x80000001U
#define CPUID_EXT_TSC 0x80000007U
#define CPUID_EXT_ADDRESS 0x80000008U

#define CR0_MP (1ULL << 1)
#define CR0_EM (1ULL << 2)
#define CR4_OSFXSR (1ULL << 9)
#define CR4_OSXMMEXCPT (1ULL << 10)
#define EFER_MSR 0xc0000080U
#define EFER_NXE (1ULL << 11)
#define IA32_GS_BASE_MSR 0xc0000101U

static struct cpu_info boot_cpu;
static struct cpu_local cpu_locals[ZEROOS_MAX_CPUS] __attribute__((aligned(64)));

static void cpuid(uint32_t leaf, uint32_t subleaf,
                  uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx) {
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid"
                     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(leaf), "c"(subleaf)
                     : "memory");
    *eax=a; *ebx=b; *ecx=c; *edx=d;
}

static uint64_t read_cr0(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr0,%0" : "=r"(value) : : "memory");
    return value;
}

static void write_cr0(uint64_t value) {
    __asm__ volatile("mov %0,%%cr0" : : "r"(value) : "memory");
}

static uint64_t read_cr4(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr4,%0" : "=r"(value) : : "memory");
    return value;
}

static void write_cr4(uint64_t value) {
    __asm__ volatile("mov %0,%%cr4" : : "r"(value) : "memory");
}

uint64_t cpu_read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr"
                     : "=a"(low), "=d"(high)
                     : "c"(msr)
                     : "memory");
    return ((uint64_t)high << 32) | low;
}

void cpu_write_msr(uint32_t msr, uint64_t value) {
    uint32_t low=(uint32_t)value;
    uint32_t high=(uint32_t)(value >> 32);
    __asm__ volatile("wrmsr"
                     :
                     : "a"(low), "d"(high), "c"(msr)
                     : "memory");
}

int cpu_init(void) {
    uint32_t eax, ebx, ecx, edx;
    uint32_t max_basic;
    uint32_t max_extended;

    boot_cpu=(struct cpu_info){0};
    for (uint32_t i=0; i<ZEROOS_MAX_CPUS; ++i)
        cpu_locals[i]=(struct cpu_local){0};

    cpuid(CPUID_VENDOR,0,&max_basic,&ebx,&ecx,&edx);
    boot_cpu.max_basic_leaf=max_basic;
    if (max_basic < CPUID_FEATURES)
        return -1;

    cpuid(CPUID_FEATURES,0,&eax,&ebx,&ecx,&edx);
    boot_cpu.stepping=eax & 0xfU;
    boot_cpu.model=(eax >> 4) & 0xfU;
    boot_cpu.family=(eax >> 8) & 0xfU;
    if (boot_cpu.family==0xfU)
        boot_cpu.family += (eax >> 20) & 0xffU;
    if (((eax >> 8) & 0xfU)==0x6U || ((eax >> 8) & 0xfU)==0xfU)
        boot_cpu.model |= ((eax >> 16) & 0xfU) << 4;

    boot_cpu.logical_processors=(uint8_t)((ebx >> 16) & 0xffU);
    boot_cpu.bootstrap_apic_id=(ebx >> 24) & 0xffU;

    if (edx & (1U << 26)) boot_cpu.features |= ZEROOS_CPU_FEATURE_SSE2;
    if (edx & (1U << 9)) boot_cpu.features |= ZEROOS_CPU_FEATURE_APIC;
    if (ecx & (1U << 30)) boot_cpu.features |= ZEROOS_CPU_FEATURE_RDRAND;
    if (ecx & (1U << 17)) boot_cpu.features |= ZEROOS_CPU_FEATURE_PCID;
    if (ecx & (1U << 27)) boot_cpu.features |= ZEROOS_CPU_FEATURE_OSXSAVE;
    if (ecx & (1U << 24)) boot_cpu.features |= ZEROOS_CPU_FEATURE_TSC_DEADLINE;

    if (max_basic >= CPUID_BASIC_EXT_FEATURES) {
        cpuid(CPUID_BASIC_EXT_FEATURES,0,&eax,&ebx,&ecx,&edx);
        if (ebx & (1U << 7)) boot_cpu.features |= ZEROOS_CPU_FEATURE_SMEP;
        if (ebx & (1U << 20)) boot_cpu.features |= ZEROOS_CPU_FEATURE_SMAP;
        if (ecx & (1U << 21)) boot_cpu.features |= ZEROOS_CPU_FEATURE_1G_PAGES;
        if (ecx & (1U << 24)) boot_cpu.features |= ZEROOS_CPU_FEATURE_OSXSAVE;
    }

    if (max_basic>=CPUID_TSC_INFO) {
        uint32_t denominator, numerator, crystal;
        cpuid(CPUID_TSC_INFO,0,&denominator,&numerator,&ecx,&edx);
        crystal=ecx;
        if (denominator && numerator && crystal)
            boot_cpu.tsc_frequency_hz=((uint64_t)crystal*numerator)/denominator;
    }
    if (boot_cpu.tsc_frequency_hz==0 && max_basic>=CPUID_BASE_FREQ) {
        cpuid(CPUID_BASE_FREQ,0,&eax,&ebx,&ecx,&edx);
        if (eax)
            boot_cpu.tsc_frequency_hz=(uint64_t)eax*1000000ULL;
    }

    cpuid(CPUID_EXT_LIMIT,0,&max_extended,&ebx,&ecx,&edx);
    boot_cpu.max_extended_leaf=max_extended;
    boot_cpu.physical_address_bits=36;
    boot_cpu.virtual_address_bits=48;
    if (max_extended>=CPUID_EXT_FEATURES) {
        cpuid(CPUID_EXT_FEATURES,0,&eax,&ebx,&ecx,&edx);
        if (edx & (1U << 20))
            boot_cpu.features |= ZEROOS_CPU_FEATURE_NX;
    }
    if (max_extended>=CPUID_EXT_TSC) {
        cpuid(CPUID_EXT_TSC,0,&eax,&ebx,&ecx,&edx);
        if (edx & (1U << 8))
            boot_cpu.features |= ZEROOS_CPU_FEATURE_INVARIANT_TSC;
    }
    if (max_extended >= CPUID_EXT_ADDRESS) {
        cpuid(CPUID_EXT_ADDRESS,0,&eax,&ebx,&ecx,&edx);
        boot_cpu.physical_address_bits=(uint8_t)(eax & 0xffU);
        boot_cpu.virtual_address_bits=(uint8_t)((eax >> 8) & 0xffU);
    }

    /* A long-mode kernel requires a usable SSE2/FPU baseline. */
    if (!(boot_cpu.features & ZEROOS_CPU_FEATURE_SSE2))
        return -1;

    uint64_t cr0=read_cr0();
    cr0 &= ~CR0_EM;
    cr0 |= CR0_MP;
    write_cr0(cr0);

    uint64_t cr4=read_cr4();
    cr4 |= CR4_OSFXSR | CR4_OSXMMEXCPT;
    write_cr4(cr4);

    /* Enable execute-disable only after CPUID has proved NX support. */
    if (boot_cpu.features & ZEROOS_CPU_FEATURE_NX) {
        uint64_t efer=cpu_read_msr(EFER_MSR);
        cpu_write_msr(EFER_MSR,efer | EFER_NXE);
    }

    boot_cpu.initialized=1;
    cpu_locals[0].cpu_id=0;
    cpu_locals[0].apic_id=boot_cpu.bootstrap_apic_id;
    cpu_locals[0].irq_depth=0;
    cpu_locals[0].nmi_depth=0;
    cpu_locals[0].scheduler_epoch=0;
    cpu_locals[0].interrupt_count=0;
    cpu_locals[0].prepared=1;
    cpu_locals[0].online=1;
    cpu_write_msr(IA32_GS_BASE_MSR,(uint64_t)&cpu_locals[0]);
    return 0;
}

const struct cpu_info *cpu_info(void) {
    return &boot_cpu;
}

const struct cpu_local *cpu_local(void) {
    uint64_t base=cpu_read_msr(IA32_GS_BASE_MSR);
    uint64_t first=(uint64_t)&cpu_locals[0];
    uint64_t last=(uint64_t)&cpu_locals[ZEROOS_MAX_CPUS];
    if (base>=first && base<last &&
        ((base-first)%sizeof(cpu_locals[0]))==0)
        return (const struct cpu_local *)(uint64_t)base;
    return &cpu_locals[0];
}

int cpu_has(uint64_t feature) {
    return boot_cpu.initialized && (boot_cpu.features & feature)!=0;
}

uint64_t cpu_tsc_frequency_hz(void) {
    return boot_cpu.tsc_frequency_hz;
}

uint64_t cpu_read_tsc(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high) : : "memory");
    return ((uint64_t)high << 32) | low;
}

uint64_t cpu_read_tscp(uint32_t *aux) {
    uint32_t low, high, value;
    __asm__ volatile("rdtscp" : "=a"(low), "=d"(high), "=c"(value) : : "memory");
    if (aux) *aux=value;
    return ((uint64_t)high << 32) | low;
}

void cpu_relax(void) {
    __asm__ volatile("pause" : : : "memory");
}

void cpu_irq_enter(void) {
    struct cpu_local *local=(struct cpu_local *)(uint64_t)cpu_local();
    ++local->irq_depth;
    ++local->interrupt_count;
}

void cpu_irq_exit(void) {
    struct cpu_local *local=(struct cpu_local *)(uint64_t)cpu_local();
    if (local->irq_depth)
        --local->irq_depth;
}

uint32_t cpu_irq_depth(void) {
    return cpu_local()->irq_depth;
}

int cpu_prepare_local(uint32_t cpu_id, uint32_t apic_id) {
    if (cpu_id>=ZEROOS_MAX_CPUS)
        return -1;
    struct cpu_local *local=&cpu_locals[cpu_id];
    if (local->online)
        return local->apic_id==apic_id ? 0 : -1;
    *local=(struct cpu_local){
        .cpu_id=cpu_id,
        .apic_id=apic_id,
        .prepared=1,
        .online=0
    };
    return 0;
}

int cpu_mark_online(uint32_t cpu_id) {
    if (cpu_id>=ZEROOS_MAX_CPUS || !cpu_locals[cpu_id].prepared)
        return -1;
    __atomic_store_n(&cpu_locals[cpu_id].online,1,__ATOMIC_RELEASE);
    cpu_write_msr(IA32_GS_BASE_MSR,(uint64_t)&cpu_locals[cpu_id]);
    return 0;
}

int cpu_mark_offline(uint32_t cpu_id) {
    if (cpu_id==0 || cpu_id>=ZEROOS_MAX_CPUS ||
        !cpu_locals[cpu_id].online)
        return -1;
    __atomic_store_n(&cpu_locals[cpu_id].online,0,__ATOMIC_RELEASE);
    return 0;
}

uint32_t cpu_current_id(void) {
    return cpu_local()->cpu_id;
}

uint32_t cpu_online_count(void) {
    uint32_t count=0;
    for (uint32_t i=0; i<ZEROOS_MAX_CPUS; ++i)
        if (__atomic_load_n(&cpu_locals[i].online,__ATOMIC_ACQUIRE))
            ++count;
    return count;
}

struct cpu_local *cpu_local_for_id(uint32_t cpu_id) {
    return cpu_id<ZEROOS_MAX_CPUS ? &cpu_locals[cpu_id] : (struct cpu_local *)0;
}
