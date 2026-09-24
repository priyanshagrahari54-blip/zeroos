#include "nvme.h"

static struct spinlock nvme_lock;
static struct zeroos_nvme_controller nvme_controllers[ZEROOS_NVME_MAX_CONTROLLERS];

int nvme_system_init(void) {
    spinlock_init(&nvme_lock);
    for (uint32_t i=0;i<ZEROOS_NVME_MAX_CONTROLLERS;++i) {
        nvme_controllers[i].used=0;
        nvme_controllers[i].generation=0;
        nvme_controllers[i].state=ZEROOS_NVME_STOPPED;
        spinlock_init(&nvme_controllers[i].lock);
        spinlock_init(&nvme_controllers[i].admin_q.lock);
        for (uint32_t q=0;q<ZEROOS_NVME_MAX_QUEUES;++q) spinlock_init(&nvme_controllers[i].io_queues[q].lock);
        for (uint32_t n=0;n<ZEROOS_NVME_MAX_NAMESPACES;++n) spinlock_init(&nvme_controllers[i].namespaces[n].lock);
    }
    return 0;
}

int nvme_controller_register(uint64_t mmio_phys, uint64_t mmio_size, uint8_t irq, uint64_t *ctrl_id_out) {
    if (!ctrl_id_out || mmio_phys==0 || mmio_size==0) return -1;
    uint64_t flags=spin_lock_irqsave(&nvme_lock);
    for (uint32_t i=0;i<ZEROOS_NVME_MAX_CONTROLLERS;++i) {
        if (nvme_controllers[i].used) continue;
        if (nvme_controllers[i].generation==0xffffffffU) continue;
        nvme_controllers[i].generation++;
        if (nvme_controllers[i].generation==0) continue;
        nvme_controllers[i].used=1;
        nvme_controllers[i].state=ZEROOS_NVME_DORMANT;
        nvme_controllers[i].mmio_phys=mmio_phys;
        nvme_controllers[i].mmio_size=mmio_size;
        nvme_controllers[i].mmio_virt=0;
        nvme_controllers[i].irq=irq;
        nvme_controllers[i].namespace_count=0;
        nvme_controllers[i].id = ((uint64_t)nvme_controllers[i].generation<<16) | (uint64_t)(i+1);
        *ctrl_id_out=nvme_controllers[i].id;
        spin_unlock_irqrestore(&nvme_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&nvme_lock,flags);
    return -1;
}

int nvme_controller_init_admin(uint64_t ctrl_id) {
    uint64_t flags=spin_lock_irqsave(&nvme_lock);
    uint32_t slot=(uint32_t)(ctrl_id & 0xffffULL);
    uint32_t gen=(uint32_t)(ctrl_id>>16);
    if (slot==0 || slot>ZEROOS_NVME_MAX_CONTROLLERS || gen==0) { spin_unlock_irqrestore(&nvme_lock,flags); return -1; }
    struct zeroos_nvme_controller *c=&nvme_controllers[slot-1];
    if (!c->used || c->generation!=gen) { spin_unlock_irqrestore(&nvme_lock,flags); return -1; }
    uint64_t cflags=spin_lock_irqsave(&c->lock);
    c->state=ZEROOS_NVME_WARM;
    spin_unlock_irqrestore(&c->lock,cflags);
    spin_unlock_irqrestore(&nvme_lock,flags);
    return 0;
}

int nvme_namespace_scan(uint64_t ctrl_id) {
    uint64_t flags=spin_lock_irqsave(&nvme_lock);
    uint32_t slot=(uint32_t)(ctrl_id & 0xffffULL);
    uint32_t gen=(uint32_t)(ctrl_id>>16);
    if (slot==0 || slot>ZEROOS_NVME_MAX_CONTROLLERS || gen==0) { spin_unlock_irqrestore(&nvme_lock,flags); return -1; }
    struct zeroos_nvme_controller *c=&nvme_controllers[slot-1];
    if (!c->used || c->generation!=gen) { spin_unlock_irqrestore(&nvme_lock,flags); return -1; }
    uint64_t cflags=spin_lock_irqsave(&c->lock);
    if (c->namespace_count==0) {
        c->namespaces[0].used=1;
        c->namespaces[0].nsid=1;
        c->namespaces[0].block_count=0;
        c->namespaces[0].block_size=512;
        c->namespace_count=1;
    }
    c->state=ZEROOS_NVME_ACTIVE;
    spin_unlock_irqrestore(&c->lock,cflags);
    spin_unlock_irqrestore(&nvme_lock,flags);
    return 0;
}

struct zeroos_nvme_controller *nvme_controller_lookup(uint64_t ctrl_id) {
    uint64_t flags=spin_lock_irqsave(&nvme_lock);
    uint32_t slot=(uint32_t)(ctrl_id & 0xffffULL);
    uint32_t gen=(uint32_t)(ctrl_id>>16);
    struct zeroos_nvme_controller *ctrl=0;
    if (slot && slot<=ZEROOS_NVME_MAX_CONTROLLERS && gen) {
        struct zeroos_nvme_controller *c=&nvme_controllers[slot-1];
        if (c->used && c->generation==gen) ctrl=c;
    }
    spin_unlock_irqrestore(&nvme_lock,flags);
    return ctrl;
}

int nvme_debug_validate(void) { return 0; }
