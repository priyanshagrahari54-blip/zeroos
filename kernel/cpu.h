#ifndef ZEROOS_CPU_H
#define ZEROOS_CPU_H

#include "types.h"

struct task;

/* Feature bits are stable kernel-internal capability identifiers. */
#define ZEROOS_CPU_FEATURE_SSE2       (1ULL << 0)
#define ZEROOS_CPU_FEATURE_NX         (1ULL << 1)
#define ZEROOS_CPU_FEATURE_APIC       (1ULL << 2)
#define ZEROOS_CPU_FEATURE_X2APIC     (1ULL << 3)
#define ZEROOS_CPU_FEATURE_TSC_DEADLINE (1ULL << 4)
#define ZEROOS_CPU_FEATURE_INVARIANT_TSC (1ULL << 5)
#define ZEROOS_CPU_FEATURE_PCID       (1ULL << 6)
#define ZEROOS_CPU_FEATURE_SMEP       (1ULL << 7)
#define ZEROOS_CPU_FEATURE_SMAP       (1ULL << 8)
#define ZEROOS_CPU_FEATURE_OSXSAVE    (1ULL << 9)
#define ZEROOS_CPU_FEATURE_1G_PAGES   (1ULL << 10)
#define ZEROOS_CPU_FEATURE_RDRAND     (1ULL << 11)
#define ZEROOS_MAX_CPUS               64U

struct cpu_info {
    uint32_t bootstrap_apic_id;
    uint32_t max_basic_leaf;
    uint32_t max_extended_leaf;
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint8_t physical_address_bits;
    uint8_t virtual_address_bits;
    uint8_t logical_processors;
    uint8_t initialized;
    uint64_t tsc_frequency_hz;
    uint64_t features;
};

/* One statically allocated per-CPU record is the UP-safe SMP boundary. */
struct cpu_local {
    uint32_t cpu_id;
    uint32_t apic_id;
    uint32_t irq_depth;
    uint32_t nmi_depth;
    uint64_t scheduler_epoch;
    uint64_t interrupt_count;
    struct task *scheduler_current;
    uint8_t scheduler_started;
    uint8_t scheduler_timer_ready;
    uint8_t scheduler_timer_fallback;
    uint8_t prepared;
    uint8_t online;
};

int cpu_init(void);
const struct cpu_info *cpu_info(void);
const struct cpu_local *cpu_local(void);
int cpu_has(uint64_t feature);
uint64_t cpu_tsc_frequency_hz(void);
uint64_t cpu_read_tsc(void);
uint64_t cpu_read_tscp(uint32_t *aux);
uint64_t cpu_read_msr(uint32_t msr);
void cpu_write_msr(uint32_t msr, uint64_t value);
void cpu_relax(void);
void cpu_irq_enter(void);
void cpu_irq_exit(void);
uint32_t cpu_irq_depth(void);

/* SMP-safe per-CPU registration and GS-base ownership. */
int cpu_prepare_local(uint32_t cpu_id, uint32_t apic_id);
int cpu_mark_online(uint32_t cpu_id);
int cpu_mark_offline(uint32_t cpu_id);
uint32_t cpu_current_id(void);
uint32_t cpu_online_count(void);
struct cpu_local *cpu_local_for_id(uint32_t cpu_id);

#endif
