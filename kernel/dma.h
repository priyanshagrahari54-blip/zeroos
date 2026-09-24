#ifndef ZEROOS_DMA_H
#define ZEROOS_DMA_H

#include "types.h"
#include "sync.h"

#define ZEROOS_DMA_MAX_MAPPINGS 128U
#define ZEROOS_DMA_MAX_BOUNCE 64U

enum zeroos_dma_direction {
    ZEROOS_DMA_TO_DEVICE = 0,
    ZEROOS_DMA_FROM_DEVICE,
    ZEROOS_DMA_BIDIRECTIONAL
};

enum zeroos_dma_state {
    ZEROOS_DMA_STOPPED = 0,
    ZEROOS_DMA_DORMANT,
    ZEROOS_DMA_WARM,
    ZEROOS_DMA_ACTIVE,
    ZEROOS_DMA_THROTTLED,
    ZEROOS_DMA_SUSPENDED
};

struct zeroos_dma_mapping {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_dma_state state;
    enum zeroos_dma_direction direction;
    uint64_t virt;
    uint64_t phys;
    uint64_t size;
    uint64_t owner_device_id;
    uint8_t coherent;
    uint8_t bounce_used;
    uint64_t bounce_phys;
    void *bounce_virt;
    struct spinlock lock;
};

int dma_system_init(void);
int dma_map(uint64_t virt, uint64_t size, enum zeroos_dma_direction dir,
            uint64_t owner_device_id, uint64_t *phys_out, uint64_t *mapping_id_out);
int dma_unmap(uint64_t mapping_id);
int dma_sync_for_device(uint64_t mapping_id);
int dma_sync_for_cpu(uint64_t mapping_id);
int dma_debug_validate(void);

#endif
