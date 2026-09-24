#ifndef ZEROOS_PCI_H
#define ZEROOS_PCI_H
#include "types.h"

/*
 * PCI configuration mechanism #1 (0xCF8/0xCFC) enumeration.
 *
 * Contract:
 *  - pci_init() scans every bus/device/function once (brute force; no
 *    reliance on firmware bridge numbering) into a bounded table of
 *    ZEROOS_PCI_MAX_DEVICES entries. Overflow is reported, never silent.
 *  - BARs are sized with decoding disabled and restored afterwards.
 *  - pci_map_bar() maps a memory BAR uncached/NX into the kernel MMIO window
 *    (VMM_MMIO_BASE + 16 MiB .. +1 GiB, bump-allocated, never reused) which
 *    is shared by every address space, so drivers may touch registers from
 *    syscall context.
 *  - MSI/MSI-X messages target the BSP local APIC with fixed delivery. They
 *    are only offered when the LAPIC/IOAPIC controller is active.
 */
#define ZEROOS_PCI_MAX_DEVICES 64U
#define ZEROOS_PCI_CAP_MSI 0x05U
#define ZEROOS_PCI_CAP_PCIE 0x10U
#define ZEROOS_PCI_CAP_MSIX 0x11U

struct pci_bar {
    uint64_t base;
    uint64_t size;
    uint8_t is_io;
    uint8_t is_64;
    uint8_t prefetchable;
    uint8_t present;
};

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t header_type;
    uint16_t vendor;
    uint16_t device;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint8_t msi_cap;
    uint8_t msix_cap;
    uint8_t pcie_cap;
    uint8_t claimed;
    uint16_t msix_table_size;
    struct pci_bar bars[6];
};

int pci_init(void);
uint32_t pci_device_count(void);
struct pci_device *pci_device_at(uint32_t index);

uint32_t pci_config_read32(const struct pci_device *device, uint8_t offset);
uint16_t pci_config_read16(const struct pci_device *device, uint8_t offset);
uint8_t pci_config_read8(const struct pci_device *device, uint8_t offset);
void pci_config_write32(const struct pci_device *device, uint8_t offset,
                        uint32_t value);
void pci_config_write16(const struct pci_device *device, uint8_t offset,
                        uint16_t value);

/* Enable memory decoding + bus mastering; optionally mask legacy INTx. */
void pci_enable_device(struct pci_device *device, int disable_intx);
/* Map [bar.base, +length) (length 0 = whole BAR); returns kernel VA or 0. */
uint64_t pci_map_bar(struct pci_device *device, uint32_t bar, uint64_t length);
/* Single-vector MSI; returns 0 on success. */
int pci_enable_msi(struct pci_device *device, uint8_t vector);
void pci_disable_msi(struct pci_device *device);
/* Program MSI-X entry `entry` (table mapped by the driver via BIR). */
int pci_msix_setup(struct pci_device *device, uint64_t *table_va_out);
int pci_msix_set_entry(struct pci_device *device, uint64_t table_va,
                       uint16_t entry, uint8_t vector, int masked);
void pci_msix_enable(struct pci_device *device, int enable);
int pci_msi_supported(void);
/* Function-level presence check: vendor reads back 0xFFFF after surprise
 * removal. */
int pci_device_present(const struct pci_device *device);

#endif
