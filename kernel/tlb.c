#include "tlb.h"
#include "sync.h"
#include "cpu.h"

struct tlb_request {
    volatile uint64_t sequence;
    volatile uint64_t virtual_address;
    volatile uint64_t target_mask;
    volatile uint64_t acknowledged_mask;
    volatile uint8_t flush_all;
};

static struct spinlock tlb_lock;
static struct tlb_request request;
static tlb_ipi_sender_t ipi_sender;
static uint64_t online_mask;
static uint64_t sequence;
static uint64_t completed_shootdowns;
static uint8_t initialized;

static int canonical_address(uint64_t address) {
    uint64_t sign=(address>>47)&1ULL;
    uint64_t upper=address>>48;
    return sign ? upper==0xffffULL : upper==0;
}

static inline void invalidate_local(uint64_t virtual_address) {
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

static inline void flush_local(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3,%0" : "=r"(cr3) : : "memory");
    __asm__ volatile("mov %0,%%cr3" : : "r"(cr3) : "memory");
}

static inline void relax_local(void) {
    __asm__ volatile("pause" : : : "memory");
}

static uint32_t bit_count(uint64_t value) {
    uint32_t count=0;
    while (value) {
        value&=value-1ULL;
        ++count;
    }
    return count;
}

static int request_remote_locked(uint64_t target_mask, uint8_t flush_all,
                                 uint64_t virtual_address) {
    if (!target_mask)
        return 0;
    if (!ipi_sender)
        return -1;

    uint64_t next_sequence=sequence+1ULL;
    if (next_sequence==0)
        return -1;
    sequence=next_sequence;
    request.sequence=next_sequence;
    request.virtual_address=virtual_address;
    request.flush_all=flush_all;
    request.acknowledged_mask=0;
    __atomic_store_n(&request.target_mask,target_mask,__ATOMIC_RELEASE);

    if (ipi_sender(target_mask,ZEROOS_TLB_SHOOTDOWN_VECTOR)!=0) {
        __atomic_store_n(&request.target_mask,0,__ATOMIC_RELEASE);
        return -1;
    }

    for (uint64_t spins=0; spins<1000000ULL; ++spins) {
        uint64_t acknowledged=__atomic_load_n(&request.acknowledged_mask,
                                               __ATOMIC_ACQUIRE);
        if ((acknowledged&target_mask)==target_mask) {
            __atomic_store_n(&request.target_mask,0,__ATOMIC_RELEASE);
            ++completed_shootdowns;
            return 0;
        }
        relax_local();
    }

    /* A missing acknowledgement is a hard failure, never a silent success. */
    __atomic_store_n(&request.target_mask,0,__ATOMIC_RELEASE);
    return -1;
}

int tlb_init(void) {
    if (initialized)
        return 0;
    spinlock_init(&tlb_lock);
    request=(struct tlb_request){0};
    ipi_sender=0;
    online_mask=1ULL;
    sequence=0;
    completed_shootdowns=0;
    initialized=1;
    return 0;
}

int tlb_set_current_cpu(uint32_t cpu_id) {
    if (!initialized || cpu_id>=ZEROOS_TLB_MAX_CPUS ||
        cpu_current_id()!=cpu_id || !(online_mask&(1ULL<<cpu_id)))
        return -1;
    return 0;
}

int tlb_register_cpu(uint32_t cpu_id) {
    if (!initialized || cpu_id>=ZEROOS_TLB_MAX_CPUS)
        return -1;
    uint64_t flags=spin_lock_irqsave(&tlb_lock);
    if ((online_mask&(1ULL<<cpu_id))!=0) {
        spin_unlock_irqrestore(&tlb_lock,flags);
        return 0;
    }
    if (!ipi_sender) {
        spin_unlock_irqrestore(&tlb_lock,flags);
        return -1;
    }
    online_mask|=1ULL<<cpu_id;
    spin_unlock_irqrestore(&tlb_lock,flags);
    return 0;
}

int tlb_unregister_cpu(uint32_t cpu_id) {
    if (!initialized || cpu_id==0 || cpu_id>=ZEROOS_TLB_MAX_CPUS)
        return -1;
    uint64_t flags=spin_lock_irqsave(&tlb_lock);
    if (!(online_mask&(1ULL<<cpu_id)) || cpu_current_id()==cpu_id) {
        spin_unlock_irqrestore(&tlb_lock,flags);
        return -1;
    }
    online_mask&=~(1ULL<<cpu_id);
    spin_unlock_irqrestore(&tlb_lock,flags);
    return 0;
}

int tlb_install_ipi_sender(tlb_ipi_sender_t sender) {
    if (!initialized || (!sender && bit_count(online_mask)>1U))
        return -1;
    uint64_t flags=spin_lock_irqsave(&tlb_lock);
    ipi_sender=sender;
    spin_unlock_irqrestore(&tlb_lock,flags);
    return 0;
}

int tlb_handle_ipi(uint32_t cpu_id) {
    if (!initialized || cpu_id>=ZEROOS_TLB_MAX_CPUS ||
        !(online_mask&(1ULL<<cpu_id)))
        return -1;
    uint64_t target=__atomic_load_n(&request.target_mask,__ATOMIC_ACQUIRE);
    if (!(target&(1ULL<<cpu_id)))
        return -1;

    uint64_t observed_sequence=__atomic_load_n(&request.sequence,
                                               __ATOMIC_ACQUIRE);
    if (observed_sequence==0)
        return -1;
    if (request.flush_all)
        flush_local();
    else
        invalidate_local(request.virtual_address);
    __atomic_fetch_or(&request.acknowledged_mask,1ULL<<cpu_id,
                      __ATOMIC_RELEASE);
    return 0;
}

int tlb_invalidate_page(uint64_t virtual_address) {
    if (!initialized || !canonical_address(virtual_address) ||
        (virtual_address&0xfffULL)!=0)
        return -1;

    uint64_t flags=spin_lock_irqsave(&tlb_lock);
    uint32_t cpu_id=cpu_current_id();
    if (cpu_id>=ZEROOS_TLB_MAX_CPUS) {
        spin_unlock_irqrestore(&tlb_lock,flags);
        return -1;
    }
    uint64_t targets=online_mask&~(1ULL<<cpu_id);
    invalidate_local(virtual_address);
    int result=request_remote_locked(targets,0,virtual_address);
    spin_unlock_irqrestore(&tlb_lock,flags);
    return result;
}

int tlb_flush_all(void) {
    if (!initialized)
        return -1;

    uint64_t flags=spin_lock_irqsave(&tlb_lock);
    uint32_t cpu_id=cpu_current_id();
    if (cpu_id>=ZEROOS_TLB_MAX_CPUS) {
        spin_unlock_irqrestore(&tlb_lock,flags);
        return -1;
    }
    uint64_t targets=online_mask&~(1ULL<<cpu_id);
    flush_local();
    int result=request_remote_locked(targets,1,0);
    spin_unlock_irqrestore(&tlb_lock,flags);
    return result;
}

uint32_t tlb_online_count(void) {
    return initialized ? bit_count(online_mask) : 0;
}

uint64_t tlb_shootdown_sequence(void) {
    return initialized ? sequence : 0;
}

int tlb_debug_validate(void) {
    uint32_t cpu_id=cpu_current_id();
    if (!initialized || online_mask==0 || cpu_id>=ZEROOS_TLB_MAX_CPUS ||
        !(online_mask&(1ULL<<cpu_id)) ||
        bit_count(online_mask)>ZEROOS_TLB_MAX_CPUS)
        return -1;
    if (bit_count(online_mask)>1U && !ipi_sender)
        return -1;
    return 0;
}
