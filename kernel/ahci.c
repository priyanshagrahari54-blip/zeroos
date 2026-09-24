#include "ahci.h"

static struct spinlock ahci_lock;
static struct zeroos_ahci_controller ahci_controllers[4];
static uint64_t next_ahci_id;

int ahci_system_init(void) {
    spinlock_init(&ahci_lock);
    next_ahci_id=1;
    for (uint32_t i=0;i<4;++i) {
        ahci_controllers[i].used=0;
        ahci_controllers[i].generation=0;
        ahci_controllers[i].port_count=0;
        spinlock_init(&ahci_controllers[i].lock);
        for (uint32_t p=0;p<ZEROOS_AHCI_MAX_PORTS;++p) {
            ahci_controllers[i].ports[p].used=0;
            spinlock_init(&ahci_controllers[i].ports[p].lock);
        }
    }
    return 0;
}

int ahci_controller_register(uint64_t mmio_phys, uint64_t mmio_size, uint8_t irq, uint64_t *ctrl_id_out) {
    if (!ctrl_id_out || mmio_phys==0 || mmio_size==0) return -1;
    uint64_t flags=spin_lock_irqsave(&ahci_lock);
    for (uint32_t i=0;i<4;++i) {
        if (ahci_controllers[i].used) continue;
        if (ahci_controllers[i].generation==0xffffffffU) continue;
        ahci_controllers[i].generation++;
        if (ahci_controllers[i].generation==0) continue;
        ahci_controllers[i].used=1;
        ahci_controllers[i].mmio_phys=mmio_phys;
        ahci_controllers[i].mmio_size=mmio_size;
        ahci_controllers[i].mmio_virt=0;
        ahci_controllers[i].irq=irq;
        ahci_controllers[i].port_count=0;
        ahci_controllers[i].id = ((uint64_t)ahci_controllers[i].generation<<16) | (uint64_t)(i+1);
        *ctrl_id_out=ahci_controllers[i].id;
        spin_unlock_irqrestore(&ahci_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&ahci_lock,flags);
    return -1;
}

int ahci_port_scan(uint64_t ctrl_id) {
    uint64_t flags=spin_lock_irqsave(&ahci_lock);
    uint32_t slot=(uint32_t)(ctrl_id & 0xffffULL);
    uint32_t gen=(uint32_t)(ctrl_id>>16);
    if (slot==0 || slot>4 || gen==0) { spin_unlock_irqrestore(&ahci_lock,flags); return -1; }
    struct zeroos_ahci_controller *ctrl=&ahci_controllers[slot-1];
    if (!ctrl->used || ctrl->generation!=gen) { spin_unlock_irqrestore(&ahci_lock,flags); return -1; }
    uint64_t cflags=spin_lock_irqsave(&ctrl->lock);
    /* Simulate scan: mark first port present */
    if (ctrl->port_count==0 && ZEROOS_AHCI_MAX_PORTS>0) {
        ctrl->ports[0].used=1;
        ctrl->ports[0].state=ZEROOS_AHCI_PORT_ATTACHED;
        ctrl->ports[0].port_number=0;
        ctrl->ports[0].present=1;
        ctrl->port_count=1;
    }
    spin_unlock_irqrestore(&ctrl->lock,cflags);
    spin_unlock_irqrestore(&ahci_lock,flags);
    return 0;
}

struct zeroos_ahci_controller *ahci_controller_lookup(uint64_t ctrl_id) {
    uint64_t flags=spin_lock_irqsave(&ahci_lock);
    uint32_t slot=(uint32_t)(ctrl_id & 0xffffULL);
    uint32_t gen=(uint32_t)(ctrl_id>>16);
    struct zeroos_ahci_controller *c=0;
    if (slot && slot<=4 && gen) {
        struct zeroos_ahci_controller *ctrl=&ahci_controllers[slot-1];
        if (ctrl->used && ctrl->generation==gen) c=ctrl;
    }
    spin_unlock_irqrestore(&ahci_lock,flags);
    return c;
}

int ahci_debug_validate(void) { return 0; }
