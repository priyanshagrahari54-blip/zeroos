#ifndef ZEROOS_IRQ_STATE_H
#define ZEROOS_IRQ_STATE_H
#include "types.h"

/* Single-CPU mutation guard. Native tests replace only this privileged edge. */
static inline uint64_t irq_save(void) {
#ifdef ZEROOS_HOST_TEST
    return 0;
#else
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
#endif
}
static inline void irq_restore(uint64_t flags) {
#ifndef ZEROOS_HOST_TEST
    if (flags & 0x200ULL) __asm__ volatile ("sti" ::: "memory");
#else
    (void)flags;
#endif
}
#endif
