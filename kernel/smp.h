#ifndef ZEROOS_SMP_H
#define ZEROOS_SMP_H

#include "types.h"

#define ZEROOS_SMP_CPU_OFFLINE 0U
#define ZEROOS_SMP_CPU_PREPARED 1U
#define ZEROOS_SMP_CPU_STARTING 2U
#define ZEROOS_SMP_CPU_ONLINE 3U
#define ZEROOS_SMP_CPU_FAILED 4U

struct smp_cpu_record {
    uint32_t cpu_id;
    uint32_t apic_id;
    uint32_t state;
    uint32_t reserved;
    uint32_t startup_generation;
    uint32_t startup_attempts;
    uint64_t bootstrap_stack;
    uint64_t startup_cr0;
    uint64_t startup_cr3;
    uint64_t startup_efer;
    uint64_t startup_idt_base;
    uint64_t startup_idt_limit;
};

int smp_init(void);
/* The trampoline passes a generation-tagged startup token, not a bare ID. */
void smp_ap_entry(uint32_t startup_token);
uint32_t smp_discovered_count(void);
uint32_t smp_online_count(void);
int smp_is_degraded(void);
int smp_startup_self_test(void);
const struct smp_cpu_record *smp_cpu_record(uint32_t cpu_id);
uint64_t smp_bootstrap_stack(uint32_t cpu_id);

#endif
