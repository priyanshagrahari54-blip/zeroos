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
    uint64_t bootstrap_stack;
};

int smp_init(void);
void smp_ap_entry(uint32_t cpu_id);
uint32_t smp_discovered_count(void);
uint32_t smp_online_count(void);
const struct smp_cpu_record *smp_cpu_record(uint32_t cpu_id);

#endif
