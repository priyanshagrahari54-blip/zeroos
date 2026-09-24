#include "dma.h"
#include "memory.h"
#include "vmm.h"

static struct spinlock dma_lock;
static struct zeroos_dma_mapping mappings[ZEROOS_DMA_MAX_MAPPINGS];
static uint64_t next_mapping_id;

int dma_system_init(void) {
    spinlock_init(&dma_lock);
    next_mapping_id=1;
    for (uint32_t i=0;i<ZEROOS_DMA_MAX_MAPPINGS;++i) {
        mappings[i].used=0;
        mappings[i].generation=0;
        mappings[i].state=ZEROOS_DMA_STOPPED;
        spinlock_init(&mappings[i].lock);
    }
    return 0;
}

static struct zeroos_dma_mapping *lookup_locked(uint64_t id) {
    uint32_t slot=(uint32_t)(id & 0xffffULL);
    uint32_t gen=(uint32_t)(id>>16);
    if (slot==0 || slot>ZEROOS_DMA_MAX_MAPPINGS || gen==0) return 0;
    struct zeroos_dma_mapping *m=&mappings[slot-1];
    if (!m->used || m->generation!=gen) return 0;
    return m;
}

int dma_map(uint64_t virt, uint64_t size, enum zeroos_dma_direction dir,
            uint64_t owner_device_id, uint64_t *phys_out, uint64_t *mapping_id_out) {
    if (!phys_out || !mapping_id_out || virt==0 || size==0 || (virt & 0xfffULL) || (size & 0xfffULL)) return -1;
    if (virt+size < virt) return -1; /* overflow */
    uint64_t flags=spin_lock_irqsave(&dma_lock);
    for (uint32_t i=0;i<ZEROOS_DMA_MAX_MAPPINGS;++i) {
        if (mappings[i].used) continue;
        if (mappings[i].generation==0xffffffffU) continue;
        mappings[i].generation++;
        if (mappings[i].generation==0) continue;
        mappings[i].used=1;
        mappings[i].state=ZEROOS_DMA_ACTIVE;
        mappings[i].direction=dir;
        mappings[i].virt=virt;
        mappings[i].size=size;
        mappings[i].owner_device_id=owner_device_id;
        mappings[i].coherent=1; /* x86_64 cache coherent */
        mappings[i].bounce_used=0;
        /* Translate via kernel VMM for now */
        uint64_t phys=vmm_translate(virt);
        if (!phys) {
            mappings[i].used=0;
            spin_unlock_irqrestore(&dma_lock,flags);
            return -1;
        }
        mappings[i].phys=phys;
        mappings[i].id = ((uint64_t)mappings[i].generation<<16) | (uint64_t)(i+1);
        *phys_out=phys;
        *mapping_id_out=mappings[i].id;
        spin_unlock_irqrestore(&dma_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&dma_lock,flags);
    return -1;
}

int dma_unmap(uint64_t mapping_id) {
    uint64_t flags=spin_lock_irqsave(&dma_lock);
    struct zeroos_dma_mapping *m=lookup_locked(mapping_id);
    if (!m) { spin_unlock_irqrestore(&dma_lock,flags); return -1; }
    uint64_t mflags=spin_lock_irqsave(&m->lock);
    if (m->bounce_used && m->bounce_virt) {
        page_free(m->bounce_virt);
        m->bounce_virt=0;
        m->bounce_phys=0;
        m->bounce_used=0;
    }
    m->used=0;
    m->state=ZEROOS_DMA_STOPPED;
    m->virt=0;
    m->phys=0;
    m->size=0;
    spin_unlock_irqrestore(&m->lock,mflags);
    spin_unlock_irqrestore(&dma_lock,flags);
    return 0;
}

int dma_sync_for_device(uint64_t mapping_id) {
    uint64_t flags=spin_lock_irqsave(&dma_lock);
    struct zeroos_dma_mapping *m=lookup_locked(mapping_id);
    if (!m) { spin_unlock_irqrestore(&dma_lock,flags); return -1; }
    /* On x86_64, coherent, need memory barrier */
    __asm__ volatile("mfence" ::: "memory");
    spin_unlock_irqrestore(&dma_lock,flags);
    return 0;
}

int dma_sync_for_cpu(uint64_t mapping_id) {
    uint64_t flags=spin_lock_irqsave(&dma_lock);
    struct zeroos_dma_mapping *m=lookup_locked(mapping_id);
    if (!m) { spin_unlock_irqrestore(&dma_lock,flags); return -1; }
    __asm__ volatile("mfence" ::: "memory");
    spin_unlock_irqrestore(&dma_lock,flags);
    return 0;
}

int dma_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&dma_lock);
    for (uint32_t i=0;i<ZEROOS_DMA_MAX_MAPPINGS;++i) {
        if (!mappings[i].used) continue;
        if (mappings[i].virt==0 || mappings[i].phys==0 || mappings[i].size==0) {
            spin_unlock_irqrestore(&dma_lock,flags);
            return -1;
        }
    }
    spin_unlock_irqrestore(&dma_lock,flags);
    return 0;
}
