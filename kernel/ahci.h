#ifndef ZEROOS_AHCI_H
#define ZEROOS_AHCI_H

#include "types.h"
#include "sync.h"
#include "block.h"

#define ZEROOS_AHCI_MAX_PORTS 32U

enum zeroos_ahci_port_state {
    ZEROOS_AHCI_PORT_DETACHED = 0,
    ZEROOS_AHCI_PORT_ATTACHED,
    ZEROOS_AHCI_PORT_ACTIVE,
    ZEROOS_AHCI_PORT_ERROR
};

struct zeroos_ahci_port {
    uint8_t used;
    enum zeroos_ahci_port_state state;
    uint8_t port_number;
    uint64_t block_device_id;
    uint64_t signature;
    uint8_t present;
    uint8_t active;
    struct spinlock lock;
};

struct zeroos_ahci_controller {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    uint64_t mmio_phys;
    uint64_t mmio_virt;
    uint64_t mmio_size;
    uint8_t irq;
    struct zeroos_ahci_port ports[ZEROOS_AHCI_MAX_PORTS];
    uint32_t port_count;
    struct spinlock lock;
};

int ahci_system_init(void);
int ahci_controller_register(uint64_t mmio_phys, uint64_t mmio_size, uint8_t irq, uint64_t *ctrl_id_out);
int ahci_port_scan(uint64_t ctrl_id);
struct zeroos_ahci_controller *ahci_controller_lookup(uint64_t ctrl_id);
int ahci_debug_validate(void);

#endif
