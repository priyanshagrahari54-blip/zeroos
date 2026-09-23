#ifndef ZEROOS_TLB_H
#define ZEROOS_TLB_H

#include "types.h"

#define ZEROOS_TLB_SHOOTDOWN_VECTOR 0xf1U
#define ZEROOS_TLB_MAX_CPUS 64U

typedef int (*tlb_ipi_sender_t)(uint64_t target_mask, uint8_t vector);

/*
 * The shootdown service is the ownership boundary between page-table writers
 * and CPU-local translation caches. The bootstrap CPU is registered during
 * VMM initialization; additional CPUs may register only after an IPI sender
 * has been installed.
 */
int tlb_init(void);
int tlb_set_current_cpu(uint32_t cpu_id);
int tlb_register_cpu(uint32_t cpu_id);
int tlb_unregister_cpu(uint32_t cpu_id);
/* Removes a failing AP while it is still executing its own failure path. */
int tlb_unregister_current_cpu(uint32_t cpu_id);
int tlb_install_ipi_sender(tlb_ipi_sender_t sender);
int tlb_handle_ipi(uint32_t cpu_id);
int tlb_invalidate_page(uint64_t virtual_address);
int tlb_flush_all(void);
uint32_t tlb_online_count(void);
int tlb_cpu_is_online(uint32_t cpu_id);
uint64_t tlb_shootdown_sequence(void);
int tlb_debug_validate(void);

#endif
