#ifndef ZEROOS_PCI_H
#define ZEROOS_PCI_H

#include "types.h"
#include "sync.h"

#define ZEROOS_PCI_MAX_DEVICES 64U
#define ZEROOS_PCI_MAX_BARS 6U
#define ZEROOS_PCI_MAX_CAPS 16U
#define ZEROOS_PCI_CONFIG_SPACE_SIZE 256U

enum zeroos_pci_device_state {
    ZEROOS_PCI_DEV_STOPPED = 0,
    ZEROOS_PCI_DEV_DORMANT,
    ZEROOS_PCI_DEV_WARM,
    ZEROOS_PCI_DEV_ACTIVE,
    ZEROOS_PCI_DEV_THROTTLED,
    ZEROOS_PCI_DEV_SUSPENDED
};

struct zeroos_pci_bar {
    uint64_t base;
    uint64_t size;
    uint8_t is_mmio;
    uint8_t is_64bit;
    uint8_t is_prefetchable;
    uint8_t valid;
};

struct zeroos_pci_device {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_pci_device_state state;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    struct zeroos_pci_bar bars[ZEROOS_PCI_MAX_BARS];
    uint8_t msi_capable;
    uint8_t msix_capable;
    uint8_t msi_enabled;
    uint8_t msix_enabled;
    uint64_t msi_address;
    uint32_t msi_data;
    uint64_t msix_table_bar;
    uint64_t msix_table_offset;
    uint64_t driver_id;
    uint64_t error_count;
    struct spinlock lock;
};

struct zeroos_pci_driver {
    uint64_t id;
    const char *name;
    int (*probe)(struct zeroos_pci_device *dev);
    int (*remove)(struct zeroos_pci_device *dev);
    int (*suspend)(struct zeroos_pci_device *dev);
    int (*resume)(struct zeroos_pci_device *dev);
};

int pci_system_init(void);
int pci_enumerate(void);
struct zeroos_pci_device *pci_device_lookup(uint64_t id);
int pci_device_set_state(uint64_t id, enum zeroos_pci_device_state state);
int pci_read_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t *value_out);
int pci_write_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);
int pci_bar_map(struct zeroos_pci_device *dev, uint32_t bar_index, uint64_t *virt_out, uint64_t *phys_out, uint64_t *size_out);
int pci_enable_msi(struct zeroos_pci_device *dev, uint8_t vector);
int pci_enable_msix(struct zeroos_pci_device *dev, uint8_t vector);
int pci_register_driver(struct zeroos_pci_driver *driver);
int pci_debug_validate(void);

#endif
