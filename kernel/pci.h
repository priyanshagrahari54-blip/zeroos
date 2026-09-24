#ifndef ZEROOS_PCI_H
#define ZEROOS_PCI_H
#include "types.h"

#define PCI_MAX_DEVICES 256
#define PCI_MAX_CAPABILITIES 48
#define PCI_STATUS_CAP_LIST 0x10
#define PCI_CLASS_BRIDGE 0x06
#define PCI_SUBCLASS_PCI_BRIDGE 0x04

enum pci_bar_kind { PCI_BAR_NONE=0, PCI_BAR_IO=1, PCI_BAR_MEMORY=2 };
struct pci_bar { uint64_t address; uint8_t kind; uint8_t is_64bit; uint8_t prefetchable; };
struct pci_capability { uint8_t id, offset; };
struct pci_device {
    uint8_t bus, slot, function, header_type;
    uint16_t vendor_id, device_id, command, status;
    uint8_t class_code, subclass, programming_interface, revision;
    struct pci_bar bars[6];
    struct pci_capability capabilities[PCI_MAX_CAPABILITIES];
    uint8_t capability_count;
};
struct pci_inventory { struct pci_device devices[PCI_MAX_DEVICES]; uint16_t count; uint32_t errors; uint8_t truncated; };

/* Observation only: this stage never sizes/assigns BARs nor enables bus mastering. */
int pci_enumerate(struct pci_inventory *out);
#endif
