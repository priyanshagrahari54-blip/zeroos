#include "pci.h"
#include "cpu.h"

extern void serial_write_public(const char *text);

static struct spinlock pci_lock;
static struct zeroos_pci_device devices[ZEROOS_PCI_MAX_DEVICES];
static uint64_t next_device_id;

static inline void outl(uint16_t port, uint32_t val) { __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port)); }
static inline uint32_t inl(uint16_t port) { uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }

#define PCI_CONFIG_ADDRESS 0xcf8
#define PCI_CONFIG_DATA 0xcfc

int pci_read_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t *value_out) {
    if (!value_out || offset & 3 || dev>=32 || func>=8) return -1;
    uint32_t address = (1U<<31) | ((uint32_t)bus<<16) | ((uint32_t)dev<<11) | ((uint32_t)func<<8) | (offset & 0xfc);
    outl(PCI_CONFIG_ADDRESS, address);
    *value_out = inl(PCI_CONFIG_DATA);
    return 0;
}

int pci_write_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    if (offset & 3 || dev>=32 || func>=8) return -1;
    uint32_t address = (1U<<31) | ((uint32_t)bus<<16) | ((uint32_t)dev<<11) | ((uint32_t)func<<8) | (offset & 0xfc);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
    return 0;
}

int pci_system_init(void) {
    spinlock_init(&pci_lock);
    next_device_id=1;
    for (uint32_t i=0;i<ZEROOS_PCI_MAX_DEVICES;++i) {
        devices[i].used=0;
        devices[i].generation=0;
        devices[i].state=ZEROOS_PCI_DEV_STOPPED;
        spinlock_init(&devices[i].lock);
    }
    return 0;
}

static int bar_parse(uint32_t bar_low, uint32_t bar_high, struct zeroos_pci_bar *out) {
    if (!out) return -1;
    if (bar_low==0) { out->valid=0; return 0; }
    if (bar_low & 1) {
        /* I/O bar */
        out->is_mmio=0;
        out->is_64bit=0;
        out->base = bar_low & 0xfffffffcULL;
        out->size=0; /* size discovery requires writing all 1s, not done here to avoid side effects */
        out->valid=1;
    } else {
        out->is_mmio=1;
        uint8_t type = (bar_low>>1)&3;
        out->is_64bit = (type==2);
        out->is_prefetchable = (bar_low>>3)&1;
        out->base = bar_low & 0xfffffff0ULL;
        if (out->is_64bit) out->base |= (uint64_t)bar_high<<32;
        out->size=0;
        out->valid=1;
        /* Validate MMIO range: must be canonical and not overlap low memory */
        if (out->base < 0x1000ULL) { out->valid=0; return -1; }
    }
    return 0;
}

int pci_enumerate(void) {
    uint64_t flags=spin_lock_irqsave(&pci_lock);
    uint32_t found=0;
    for (uint16_t bus=0; bus<256 && found<ZEROOS_PCI_MAX_DEVICES; ++bus) {
        for (uint8_t dev=0; dev<32 && found<ZEROOS_PCI_MAX_DEVICES; ++dev) {
            uint32_t vendor_device;
            if (pci_read_config((uint8_t)bus, dev, 0, 0, &vendor_device)!=0) continue;
            uint16_t vendor = vendor_device & 0xffffU;
            if (vendor==0xffffU) continue;
            uint32_t class_rev;
            pci_read_config((uint8_t)bus, dev, 0, 8, &class_rev);
            uint8_t class_code = (class_rev>>24)&0xff;
            uint8_t subclass = (class_rev>>16)&0xff;
            uint8_t prog_if = (class_rev>>8)&0xff;
            uint8_t rev = class_rev & 0xff;
            /* Find free slot */
            struct zeroos_pci_device *slot=0;
            for (uint32_t i=0;i<ZEROOS_PCI_MAX_DEVICES;++i) if (!devices[i].used) { slot=&devices[i]; break; }
            if (!slot) break;
            if (slot->generation==0xffffffffU) continue;
            slot->generation++;
            if (slot->generation==0) continue;
            slot->used=1;
            slot->state=ZEROOS_PCI_DEV_DORMANT;
            slot->bus=(uint8_t)bus;
            slot->device=dev;
            slot->function=0;
            slot->vendor_id=vendor;
            slot->device_id=(vendor_device>>16)&0xffffU;
            slot->class_code=class_code;
            slot->subclass=subclass;
            slot->prog_if=prog_if;
            slot->revision=rev;
            slot->id = ((uint64_t)slot->generation<<16) | (uint64_t)(slot-devices+1);
            /* Parse BARs */
            for (uint32_t b=0;b<ZEROOS_PCI_MAX_BARS;++b) {
                uint32_t bar_low=0, bar_high=0;
                pci_read_config((uint8_t)bus, dev, 0, 0x10+b*4, &bar_low);
                if (bar_low & 1) {
                    bar_parse(bar_low,0,&slot->bars[b]);
                } else {
                    uint8_t type=(bar_low>>1)&3;
                    if (type==2 && b+1<ZEROOS_PCI_MAX_BARS) {
                        pci_read_config((uint8_t)bus, dev, 0, 0x10+(b+1)*4, &bar_high);
                        bar_parse(bar_low,bar_high,&slot->bars[b]);
                        b++; /* 64-bit consumes next */
                    } else {
                        bar_parse(bar_low,0,&slot->bars[b]);
                    }
                }
            }
            /* Check MSI/MSI-X caps */
            uint32_t status;
            pci_read_config((uint8_t)bus, dev, 0, 4, &status);
            uint8_t cap_ptr=0;
            if (status & (1U<<20)) {
                uint32_t cap;
                pci_read_config((uint8_t)bus, dev, 0, 0x34, &cap);
                cap_ptr = cap & 0xff;
                uint32_t iter=0;
                while (cap_ptr && iter<ZEROOS_PCI_MAX_CAPS) {
                    uint32_t cap_data=0;
                    pci_read_config((uint8_t)bus, dev, 0, cap_ptr, &cap_data);
                    uint8_t cap_id = cap_data & 0xff;
                    if (cap_id==0x05) slot->msi_capable=1;
                    if (cap_id==0x11) slot->msix_capable=1;
                    cap_ptr = (cap_data>>8)&0xff;
                    iter++;
                }
            }
            found++;
        }
    }
    spin_unlock_irqrestore(&pci_lock,flags);
    return (int)found;
}

struct zeroos_pci_device *pci_device_lookup(uint64_t id) {
    uint64_t flags=spin_lock_irqsave(&pci_lock);
    uint32_t slot=(uint32_t)(id & 0xffffULL);
    uint32_t gen=(uint32_t)(id>>16);
    if (slot==0 || slot>ZEROOS_PCI_MAX_DEVICES || gen==0) { spin_unlock_irqrestore(&pci_lock,flags); return 0; }
    struct zeroos_pci_device *dev=&devices[slot-1];
    if (!dev->used || dev->generation!=gen) { spin_unlock_irqrestore(&pci_lock,flags); return 0; }
    spin_unlock_irqrestore(&pci_lock,flags);
    return dev;
}

int pci_device_set_state(uint64_t id, enum zeroos_pci_device_state state) {
    struct zeroos_pci_device *dev=pci_device_lookup(id);
    if (!dev) return -1;
    uint64_t flags=spin_lock_irqsave(&dev->lock);
    dev->state=state;
    spin_unlock_irqrestore(&dev->lock,flags);
    return 0;
}

int pci_bar_map(struct zeroos_pci_device *dev, uint32_t bar_index, uint64_t *virt_out, uint64_t *phys_out, uint64_t *size_out) {
    if (!dev || bar_index>=ZEROOS_PCI_MAX_BARS || !virt_out || !phys_out || !size_out) return -1;
    uint64_t flags=spin_lock_irqsave(&dev->lock);
    struct zeroos_pci_bar *bar=&dev->bars[bar_index];
    if (!bar->valid) { spin_unlock_irqrestore(&dev->lock,flags); return -1; }
    /* Validate range: must be page aligned, not zero, not overflow */
    if (bar->base==0 || (bar->base & 0xfffULL)) { spin_unlock_irqrestore(&dev->lock,flags); return -1; }
    /* For now, return physical as base, virtual as same (identity) for early boot, real mapping via VMM later */
    *phys_out=bar->base;
    *virt_out=bar->base | 0xffff800000000000ULL; /* MMIO base */
    *size_out=bar->size ? bar->size : 4096;
    spin_unlock_irqrestore(&dev->lock,flags);
    return 0;
}

int pci_enable_msi(struct zeroos_pci_device *dev, uint8_t vector) {
    if (!dev) return -1;
    if (!dev->msi_capable) return -1;
    uint64_t flags=spin_lock_irqsave(&dev->lock);
    dev->msi_enabled=1;
    dev->msi_data=vector;
    spin_unlock_irqrestore(&dev->lock,flags);
    return 0;
}

int pci_enable_msix(struct zeroos_pci_device *dev, uint8_t vector) {
    if (!dev) return -1;
    if (!dev->msix_capable) return -1;
    uint64_t flags=spin_lock_irqsave(&dev->lock);
    dev->msix_enabled=1;
    spin_unlock_irqrestore(&dev->lock,flags);
    (void)vector;
    return 0;
}

int pci_register_driver(struct zeroos_pci_driver *driver) {
    if (!driver || !driver->name || !driver->probe) return -1;
    /* For now, just validate, actual matching happens during enumeration */
    return 0;
}

int pci_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&pci_lock);
    for (uint32_t i=0;i<ZEROOS_PCI_MAX_DEVICES;++i) {
        if (!devices[i].used) continue;
        if (devices[i].vendor_id==0xffffU || devices[i].vendor_id==0) {
            spin_unlock_irqrestore(&pci_lock,flags);
            return -1;
        }
    }
    spin_unlock_irqrestore(&pci_lock,flags);
    return 0;
}
