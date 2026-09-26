#ifndef ZEROOS_NVME_H
#define ZEROOS_NVME_H

#include "types.h"
#include "sync.h"

#define ZEROOS_NVME_MAX_CONTROLLERS 4U
#define ZEROOS_NVME_MAX_NAMESPACES 8U
#define ZEROOS_NVME_MAX_QUEUES 16U

enum zeroos_nvme_state {
    ZEROOS_NVME_STOPPED = 0,
    ZEROOS_NVME_DORMANT,
    ZEROOS_NVME_WARM,
    ZEROOS_NVME_ACTIVE,
    ZEROOS_NVME_THROTTLED,
    ZEROOS_NVME_SUSPENDED
};

struct zeroos_nvme_queue {
    uint8_t used;
    uint16_t queue_id;
    uint16_t depth;
    uint64_t dma_phys;
    void *dma_virt;
    uint16_t head;
    uint16_t tail;
    struct spinlock lock;
};

struct zeroos_nvme_namespace {
    uint8_t used;
    uint32_t nsid;
    uint64_t block_count;
    uint32_t block_size;
    uint64_t block_device_id;
    struct spinlock lock;
};

struct zeroos_nvme_controller {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_nvme_state state;
    uint64_t mmio_phys;
    uint64_t mmio_virt;
    uint64_t mmio_size;
    uint8_t irq;
    struct zeroos_nvme_queue admin_q;
    struct zeroos_nvme_queue io_queues[ZEROOS_NVME_MAX_QUEUES];
    struct zeroos_nvme_namespace namespaces[ZEROOS_NVME_MAX_NAMESPACES];
    uint32_t namespace_count;
    struct spinlock lock;
};

int nvme_system_init(void);
int nvme_controller_register(uint64_t mmio_phys, uint64_t mmio_size, uint8_t irq, uint64_t *ctrl_id_out);
int nvme_controller_init_admin(uint64_t ctrl_id);
int nvme_namespace_scan(uint64_t ctrl_id);
struct zeroos_nvme_controller *nvme_controller_lookup(uint64_t ctrl_id);
int nvme_debug_validate(void);

#endif
