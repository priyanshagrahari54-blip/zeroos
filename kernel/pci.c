#include "pci.h"
#include "apic.h"
#include "cpu.h"
#include "kstring.h"
#include "sync.h"
#include "vmm.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC
#define PCI_MMIO_WINDOW_BASE (VMM_MMIO_BASE + 0x1000000ULL)
#define PCI_MMIO_WINDOW_LIMIT (VMM_MMIO_BASE + 0x40000000ULL)

static struct pci_device pci_devices[ZEROOS_PCI_MAX_DEVICES];
static uint32_t pci_count;
static uint32_t pci_overflow;
static struct spinlock pci_lock;
static uint64_t pci_mmio_next=PCI_MMIO_WINDOW_BASE;
static int pci_ready;

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t pci_raw_read(uint8_t bus, uint8_t slot, uint8_t function,
                             uint8_t offset) {
    uint32_t address=0x80000000U|((uint32_t)bus<<16)|((uint32_t)slot<<11)|
                     ((uint32_t)function<<8)|(offset&0xfcU);
    uint64_t flags=spin_lock_irqsave(&pci_lock);
    outl(PCI_CONFIG_ADDRESS,address);
    uint32_t value=inl(PCI_CONFIG_DATA);
    spin_unlock_irqrestore(&pci_lock,flags);
    return value;
}

static void pci_raw_write(uint8_t bus, uint8_t slot, uint8_t function,
                          uint8_t offset, uint32_t value) {
    uint32_t address=0x80000000U|((uint32_t)bus<<16)|((uint32_t)slot<<11)|
                     ((uint32_t)function<<8)|(offset&0xfcU);
    uint64_t flags=spin_lock_irqsave(&pci_lock);
    outl(PCI_CONFIG_ADDRESS,address);
    outl(PCI_CONFIG_DATA,value);
    spin_unlock_irqrestore(&pci_lock,flags);
}

uint32_t pci_config_read32(const struct pci_device *device, uint8_t offset) {
    return pci_raw_read(device->bus,device->slot,device->function,offset);
}

uint16_t pci_config_read16(const struct pci_device *device, uint8_t offset) {
    return (uint16_t)(pci_config_read32(device,offset)>>((offset&2U)*8U));
}

uint8_t pci_config_read8(const struct pci_device *device, uint8_t offset) {
    return (uint8_t)(pci_config_read32(device,offset)>>((offset&3U)*8U));
}

void pci_config_write32(const struct pci_device *device, uint8_t offset,
                        uint32_t value) {
    pci_raw_write(device->bus,device->slot,device->function,offset,value);
}

void pci_config_write16(const struct pci_device *device, uint8_t offset,
                        uint16_t value) {
    /* Read-modify-write of the containing dword. Callers only use this for
     * registers whose neighbour half has no write-1-to-clear bits
     * (command/MSI control), except status, which is written as zero. */
    uint32_t shift=(offset&2U)*8U;
    uint32_t current=pci_config_read32(device,offset);
    if ((offset&0xfcU)==0x04U)
        current&=0x0000ffffU;       /* never write-1-clear the status word */
    current&=~(0xffffU<<shift);
    current|=(uint32_t)value<<shift;
    pci_config_write32(device,offset,current);
}

/* Display controllers (class 03h) are never sized: their framebuffer BAR
 * is already live (kernel/fb.c maps and verifies it at boot) and sizing
 * requires turning memory decoding off. Their BAR bases are recorded
 * read-only with size 0; pci_map_bar() refuses size-0 BARs. */
static void pci_record_bars_readonly(struct pci_device *device, uint32_t bar_count) {
    for (uint32_t i=0; i<bar_count; ++i) {
        uint32_t original=pci_config_read32(device,(uint8_t)(0x10U+i*4U));
        struct pci_bar *bar=&device->bars[i];
        if (original&1U) {
            bar->is_io=1;
            bar->base=original&~0x3U;
        } else {
            bar->base=original&~0xfULL;
            bar->prefetchable=(uint8_t)((original>>3)&1U);
            if (((original>>1)&3U)==2U && i+1U<bar_count) {
                bar->base|=(uint64_t)pci_config_read32(device,(uint8_t)(0x14U+i*4U))<<32;
                bar->is_64=1;
                ++i;
            }
        }
        bar->present=0;            /* not sized: unusable for pci_map_bar */
    }
}

static void pci_size_bars(struct pci_device *device) {
    uint32_t bar_count=(device->header_type&0x7fU)==0 ? 6U :
                       ((device->header_type&0x7fU)==1 ? 2U : 0U);
    if (device->class_code==0x03U) {
        pci_record_bars_readonly(device,bar_count);
        return;
    }
    uint16_t command=pci_config_read16(device,0x04);
    /* Disable decoding while BARs temporarily hold all-ones. */
    pci_config_write16(device,0x04,(uint16_t)(command&~0x0003U));
    for (uint32_t i=0; i<bar_count; ++i) {
        uint8_t offset=(uint8_t)(0x10U+i*4U);
        uint32_t original=pci_config_read32(device,offset);
        struct pci_bar *bar=&device->bars[i];
        if (original&1U) {
            pci_config_write32(device,offset,0xffffffffU);
            uint32_t mask=pci_config_read32(device,offset)&~0x3U;
            pci_config_write32(device,offset,original);
            if (mask) {
                bar->is_io=1;
                bar->base=original&~0x3U;
                bar->size=(uint64_t)((~mask+1U)&0xffffU);
                bar->present=1;
            }
            continue;
        }
        uint32_t type=(original>>1)&3U;
        pci_config_write32(device,offset,0xffffffffU);
        uint64_t mask=pci_config_read32(device,offset)&~0xfULL;
        pci_config_write32(device,offset,original);
        uint64_t base=original&~0xfULL;
        if (type==2U && i+1U<bar_count) {
            uint8_t high_offset=(uint8_t)(offset+4U);
            uint32_t high_original=pci_config_read32(device,high_offset);
            pci_config_write32(device,high_offset,0xffffffffU);
            uint64_t high_mask=pci_config_read32(device,high_offset);
            pci_config_write32(device,high_offset,high_original);
            mask|=high_mask<<32;
            base|=(uint64_t)high_original<<32;
            bar->is_64=1;
        } else {
            mask|=0xffffffff00000000ULL;
        }
        if (mask&~0xfULL) {
            bar->base=base;
            bar->size=~mask+1ULL;
            bar->prefetchable=(uint8_t)((original>>3)&1U);
            bar->present=base!=0;
        }
        if (bar->is_64)
            ++i;
    }
    pci_config_write16(device,0x04,command);
}

static void pci_scan_capabilities(struct pci_device *device) {
    uint16_t status=pci_config_read16(device,0x06);
    if (!(status&0x10U))
        return;
    uint8_t pointer=(uint8_t)(pci_config_read8(device,0x34)&0xfcU);
    /* Bounded walk: at most 48 capabilities in 256 bytes of config space. */
    for (int guard=0; pointer>=0x40U && guard<48; ++guard) {
        uint8_t id=pci_config_read8(device,pointer);
        if (id==ZEROOS_PCI_CAP_MSI && !device->msi_cap)
            device->msi_cap=pointer;
        else if (id==ZEROOS_PCI_CAP_MSIX && !device->msix_cap) {
            device->msix_cap=pointer;
            device->msix_table_size=(uint16_t)(
                (pci_config_read16(device,(uint8_t)(pointer+2U))&0x7ffU)+1U);
        } else if (id==ZEROOS_PCI_CAP_PCIE && !device->pcie_cap)
            device->pcie_cap=pointer;
        pointer=(uint8_t)(pci_config_read8(device,(uint8_t)(pointer+1U))&0xfcU);
    }
}

static void pci_probe_function(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t id=pci_raw_read(bus,slot,function,0x00);
    if ((id&0xffffU)==0xffffU)
        return;
    if (pci_count>=ZEROOS_PCI_MAX_DEVICES) {
        ++pci_overflow;
        return;
    }
    struct pci_device *device=&pci_devices[pci_count++];
    memset(device,0,sizeof(*device));
    device->bus=bus;
    device->slot=slot;
    device->function=function;
    device->vendor=(uint16_t)(id&0xffffU);
    device->device=(uint16_t)(id>>16);
    uint32_t class_reg=pci_raw_read(bus,slot,function,0x08);
    device->revision=(uint8_t)class_reg;
    device->prog_if=(uint8_t)(class_reg>>8);
    device->subclass=(uint8_t)(class_reg>>16);
    device->class_code=(uint8_t)(class_reg>>24);
    device->header_type=(uint8_t)(pci_raw_read(bus,slot,function,0x0c)>>16);
    uint32_t irq=pci_raw_read(bus,slot,function,0x3c);
    device->irq_line=(uint8_t)irq;
    device->irq_pin=(uint8_t)(irq>>8);
    pci_size_bars(device);
    pci_scan_capabilities(device);
}

int pci_init(void) {
    if (pci_ready)
        return 0;
    spinlock_init(&pci_lock);
    for (uint32_t bus=0; bus<256U; ++bus) {
        for (uint8_t slot=0; slot<32U; ++slot) {
            uint32_t id=pci_raw_read((uint8_t)bus,slot,0,0x00);
            if ((id&0xffffU)==0xffffU)
                continue;
            uint8_t header=(uint8_t)(pci_raw_read((uint8_t)bus,slot,0,0x0c)>>16);
            uint8_t functions=(header&0x80U) ? 8U : 1U;
            for (uint8_t function=0; function<functions; ++function)
                pci_probe_function((uint8_t)bus,slot,function);
        }
    }
    pci_ready=1;
    klog("ZEROOS: PCI enumeration: functions=%u overflow=%u msi=%s.",
         pci_count,pci_overflow,pci_msi_supported() ? "available" : "unavailable");
    for (uint32_t i=0; i<pci_count; ++i) {
        struct pci_device *d=&pci_devices[i];
        if (d->class_code==0x01U)
            klog("ZEROOS: PCI storage %02x:%02x.%u %04x:%04x class=%02x.%02x.%02x msi=%u msix=%u.",
                 d->bus,d->slot,d->function,d->vendor,d->device,d->class_code,
                 d->subclass,d->prog_if,d->msi_cap ? 1U : 0U,
                 d->msix_cap ? (unsigned)d->msix_table_size : 0U);
    }
    return 0;
}

uint32_t pci_device_count(void) {
    return pci_count;
}

struct pci_device *pci_device_at(uint32_t index) {
    return index<pci_count ? &pci_devices[index] : 0;
}

void pci_enable_device(struct pci_device *device, int disable_intx) {
    uint16_t command=pci_config_read16(device,0x04);
    command|=0x0006U;                   /* memory space + bus master */
    if (disable_intx)
        command|=0x0400U;
    else
        command&=(uint16_t)~0x0400U;
    pci_config_write16(device,0x04,command);
}

uint64_t pci_map_bar(struct pci_device *device, uint32_t index, uint64_t length) {
    if (index>=6U)
        return 0;
    struct pci_bar *bar=&device->bars[index];
    if (!bar->present || bar->is_io || bar->size==0)
        return 0;
    if (length==0 || length>bar->size)
        length=bar->size;
    uint64_t offset=bar->base&(VMM_PAGE_SIZE-1ULL);
    uint64_t physical=bar->base-offset;
    uint64_t pages=(length+offset+VMM_PAGE_SIZE-1ULL)/VMM_PAGE_SIZE;

    uint64_t flags=spin_lock_irqsave(&pci_lock);
    uint64_t va=pci_mmio_next;
    if (pages>(PCI_MMIO_WINDOW_LIMIT-va)/VMM_PAGE_SIZE) {
        spin_unlock_irqrestore(&pci_lock,flags);
        return 0;
    }
    pci_mmio_next+=pages*VMM_PAGE_SIZE+VMM_PAGE_SIZE;   /* guard page */
    spin_unlock_irqrestore(&pci_lock,flags);

    for (uint64_t i=0; i<pages; ++i) {
        if (vmm_map_mmio_page(va+i*VMM_PAGE_SIZE,physical+i*VMM_PAGE_SIZE,
                              VMM_WRITABLE|VMM_CACHE_DISABLE|
                              VMM_WRITE_THROUGH|VMM_NO_EXECUTE)!=0) {
            for (uint64_t j=0; j<i; ++j)
                (void)vmm_unmap_mmio_page(va+j*VMM_PAGE_SIZE);
            return 0;
        }
    }
    return va+offset;
}

int pci_msi_supported(void) {
    return apic_controller()==ZEROOS_IRQ_CONTROLLER_LAPIC_IOAPIC;
}

static uint32_t pci_msi_address(void) {
    struct cpu_local *bsp=cpu_local_for_id(0);
    uint32_t apic_id=bsp ? bsp->apic_id : 0;
    return 0xFEE00000U|((apic_id&0xffU)<<12);
}

int pci_enable_msi(struct pci_device *device, uint8_t vector) {
    if (!device->msi_cap || !pci_msi_supported())
        return -1;
    uint8_t cap=device->msi_cap;
    uint16_t control=pci_config_read16(device,(uint8_t)(cap+2U));
    control&=(uint16_t)~0x0071U;        /* disable, MME=1 vector */
    pci_config_write16(device,(uint8_t)(cap+2U),control);
    pci_config_write32(device,(uint8_t)(cap+4U),pci_msi_address());
    if (control&0x0080U) {
        pci_config_write32(device,(uint8_t)(cap+8U),0);
        pci_config_write16(device,(uint8_t)(cap+12U),vector);
    } else {
        pci_config_write16(device,(uint8_t)(cap+8U),vector);
    }
    pci_config_write16(device,(uint8_t)(cap+2U),(uint16_t)(control|0x0001U));
    return 0;
}

void pci_disable_msi(struct pci_device *device) {
    if (!device->msi_cap)
        return;
    uint8_t cap=device->msi_cap;
    uint16_t control=pci_config_read16(device,(uint8_t)(cap+2U));
    pci_config_write16(device,(uint8_t)(cap+2U),(uint16_t)(control&~0x0001U));
}

int pci_msix_setup(struct pci_device *device, uint64_t *table_va_out) {
    if (!device->msix_cap || !pci_msi_supported() || !table_va_out)
        return -1;
    uint32_t table=pci_config_read32(device,(uint8_t)(device->msix_cap+4U));
    uint32_t bir=table&7U;
    uint64_t offset=table&~7U;
    if (bir>=6U || !device->bars[bir].present)
        return -1;
    uint64_t length=offset+(uint64_t)device->msix_table_size*16ULL;
    if (length>device->bars[bir].size)
        return -1;
    uint64_t base=pci_map_bar(device,bir,length);
    if (!base)
        return -1;
    *table_va_out=base+offset;
    /* Mask every entry before enabling the function. */
    for (uint16_t i=0; i<device->msix_table_size; ++i)
        *(volatile uint32_t *)(uint64_t)(*table_va_out+i*16ULL+12ULL)=1U;
    return 0;
}

int pci_msix_set_entry(struct pci_device *device, uint64_t table_va,
                       uint16_t entry, uint8_t vector, int masked) {
    if (!table_va || entry>=device->msix_table_size)
        return -1;
    volatile uint32_t *slot=(volatile uint32_t *)(uint64_t)(table_va+entry*16ULL);
    slot[3]=1U;
    slot[0]=pci_msi_address();
    slot[1]=0;
    slot[2]=vector;
    slot[3]=masked ? 1U : 0U;
    return 0;
}

void pci_msix_enable(struct pci_device *device, int enable) {
    if (!device->msix_cap)
        return;
    uint8_t cap=(uint8_t)(device->msix_cap+2U);
    uint16_t control=pci_config_read16(device,cap);
    control&=(uint16_t)~0x4000U;        /* clear function mask */
    if (enable)
        control|=0x8000U;
    else
        control&=(uint16_t)~0x8000U;
    pci_config_write16(device,cap,control);
}

int pci_device_present(const struct pci_device *device) {
    return (pci_config_read32(device,0x00)&0xffffU)!=0xffffU;
}
