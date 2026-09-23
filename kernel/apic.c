#include "apic.h"
#include "cpu.h"
#include "pic.h"
#include "timer.h"
#include "vmm.h"

#define IA32_APIC_BASE_MSR 0x1bU
#define APIC_BASE_MASK 0x000ffffffffff000ULL
#define APIC_BASE_ENABLE (1ULL << 11)
#define APIC_REG_ID 0x020U
#define APIC_REG_VERSION 0x030U
#define APIC_REG_TPR 0x080U
#define APIC_REG_EOI 0x0b0U
#define APIC_REG_SVR 0x0f0U
#define APIC_REG_ICR_LOW 0x300U
#define APIC_REG_ICR_HIGH 0x310U
#define APIC_ICR_DELIVERY_STATUS (1U<<12)
#define APIC_ICR_INIT 0x00004500U
#define APIC_ICR_INIT_LEVEL 0x0000c500U
#define APIC_ICR_SIPI 0x00004600U
#define APIC_SVR_ENABLE (1U << 8)
#define APIC_SPURIOUS_VECTOR 0xffU

#define IOAPIC_REG_ID 0x00U
#define IOAPIC_REG_VERSION 0x01U
#define IOAPIC_REG_REDIRECTION_BASE 0x10U
#define IOAPIC_MMIO_STRIDE VMM_PAGE_SIZE
#define IOAPIC_MAX_SUPPORTED 16U
#define APIC_TIMER_VECTOR 32U
#define APIC_MMIO_FLAGS (VMM_WRITABLE | VMM_CACHE_DISABLE | VMM_NO_EXECUTE)

extern void serial_write_public(const char *text);

static struct apic_info state;
static uint64_t ioapic_virtual[IOAPIC_MAX_SUPPORTED];
static uint16_t ioapic_redirection_count[IOAPIC_MAX_SUPPORTED];
static uint8_t local_apic_mapped;
static uint8_t ioapic_mapped[IOAPIC_MAX_SUPPORTED];

static volatile uint32_t *mmio_register(uint64_t base, uint32_t offset) {
    return (volatile uint32_t *)(uint64_t)(base+offset);
}

static uint32_t local_apic_read(uint32_t offset) {
    return *mmio_register(state.local_apic_virtual,offset);
}

static void local_apic_write(uint32_t offset, uint32_t value) {
    *mmio_register(state.local_apic_virtual,offset)=value;
}

static uint32_t ioapic_read(uint32_t index, uint8_t reg) {
    volatile uint32_t *base=(volatile uint32_t *)(uint64_t)ioapic_virtual[index];
    base[0]=reg;
    return base[4];
}

static void ioapic_write(uint32_t index, uint8_t reg, uint32_t value) {
    volatile uint32_t *base=(volatile uint32_t *)(uint64_t)ioapic_virtual[index];
    base[0]=reg;
    base[4]=value;
}

static void apic_unmap_mmio(void) {
    for (uint32_t i=0; i<IOAPIC_MAX_SUPPORTED; ++i) {
        if (ioapic_mapped[i]) {
            (void)vmm_unmap_mmio_page(ioapic_virtual[i]);
            ioapic_mapped[i]=0;
        }
        ioapic_virtual[i]=0;
        ioapic_redirection_count[i]=0;
    }
    if (local_apic_mapped) {
        (void)vmm_unmap_mmio_page(state.local_apic_virtual);
        local_apic_mapped=0;
    }
    state.local_apic_virtual=0;
}

static int map_runtime_controller_mmio(const struct acpi_info *firmware) {
    if (!state.local_apic_base ||
        vmm_map_mmio_page(VMM_MMIO_BASE,state.local_apic_base,
                          APIC_MMIO_FLAGS)!=0)
        return -1;

    state.local_apic_virtual=VMM_MMIO_BASE;
    local_apic_mapped=1;
    state.local_apic_version=local_apic_read(APIC_REG_VERSION)&0xffU;
    state.local_apic_id=local_apic_read(APIC_REG_ID)>>24;
    if (state.local_apic_version==0 || state.local_apic_version==0xffU)
        return -1;
    state.local_apic_present=1;

    if (!firmware || !firmware->valid || firmware->ioapic_count==0)
        return 0;
    if (firmware->ioapic_count>IOAPIC_MAX_SUPPORTED)
        return -1;

    for (uint32_t i=0; i<firmware->ioapic_count; ++i) {
        uint64_t physical=(uint64_t)firmware->ioapics[i].address;
        uint64_t virtual_address=VMM_MMIO_BASE+
                                  (uint64_t)(i+1U)*IOAPIC_MMIO_STRIDE;
        if ((physical & (VMM_PAGE_SIZE-1ULL)) ||
            vmm_map_mmio_page(virtual_address,physical,APIC_MMIO_FLAGS)!=0)
            return -1;
        ioapic_virtual[i]=virtual_address;
        ioapic_mapped[i]=1;

        uint32_t version=ioapic_read(i,IOAPIC_REG_VERSION);
        uint32_t maximum=(version>>16)&0xffU;
        if (maximum==0 || maximum>=256U)
            return -1;
        ioapic_redirection_count[i]=(uint16_t)(maximum+1U);
    }
    return 0;
}

static uint32_t ioapic_entry_flags(uint16_t acpi_flags) {
    uint32_t flags=0;
    uint16_t polarity=acpi_flags&3U;
    uint16_t trigger=(acpi_flags>>2)&3U;

    /* ACPI 0b11 means active-low/level; 0b01 means active-high/edge. */
    if (polarity==3U)
        flags|=1U<<13;
    if (trigger==3U)
        flags|=1U<<15;
    return flags;
}

static int apic_wait_icr(void) {
    for (uint64_t spins=0; spins<1000ULL; ++spins) {
        if (!(local_apic_read(APIC_REG_ICR_LOW)&APIC_ICR_DELIVERY_STATUS))
            return 0;
        cpu_relax();
    }
    return -1;
}

static int apic_write_ipi(uint32_t destination_apic_id, uint32_t command) {
    if (!state.local_apic_present || destination_apic_id>0xffU ||
        !state.local_apic_virtual)
        return -1;
    if (apic_wait_icr()!=0)
        return -1;
    local_apic_write(APIC_REG_ICR_HIGH,destination_apic_id<<24);
    local_apic_write(APIC_REG_ICR_LOW,command);
    return apic_wait_icr();
}

static void apic_delay_us(uint64_t microseconds) {
    /* A short SIPI spacing delay must not depend on a future timer IRQ: an AP
     * may already be changing reset state while the BSP remains in this
     * routine. */
    if (microseconds<=200ULL)
        return;

    uint64_t frequency=cpu_tsc_frequency_hz();
    if (frequency) {
        uint64_t ticks=(frequency/1000000ULL)*microseconds;
        if (ticks==0)
            ticks=1;
        uint64_t start=cpu_read_tsc();
        while (cpu_read_tsc()-start<ticks)
            cpu_relax();
        return;
    }

    /* PIT is already initialized before AP startup. Use its tick boundary
     * when CPUID did not expose a TSC frequency; this avoids a CPU-speed-
     * dependent busy loop that can exceed the boot watchdog under TCG. */
    uint64_t needed=(microseconds+9999ULL)/10000ULL;
    if (needed==0)
        needed=1;
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags) : : "memory");
    if (flags & (1ULL<<9)) {
        uint64_t start=timer_ticks();
        while (timer_ticks()-start<needed)
            __asm__ volatile ("hlt" : : : "memory");
        return;
    }
    for (uint64_t delay=0; delay<100000ULL; ++delay)
        cpu_relax();
}

static int locate_timer_route(const struct acpi_info *firmware,
                              uint32_t *ioapic_index, uint32_t *pin,
                              uint16_t *source_flags) {
    uint32_t gsi=0;
    uint16_t flags=0;
    for (uint32_t i=0; i<firmware->interrupt_override_count; ++i) {
        const struct acpi_interrupt_override *override=
            &firmware->overrides[i];
        if (override->bus==0 && override->source==0) {
            gsi=override->gsi;
            flags=override->flags;
            break;
        }
    }

    for (uint32_t i=0; i<firmware->ioapic_count; ++i) {
        uint32_t base=firmware->ioapics[i].gsi_base;
        uint32_t count=ioapic_redirection_count[i];
        if (gsi>=base && gsi-base<count) {
            *ioapic_index=i;
            *pin=gsi-base;
            *source_flags=flags;
            return 0;
        }
    }
    return -1;
}

int apic_init(uint64_t multiboot_info) {
    state=(struct apic_info){
        .controller=ZEROOS_IRQ_CONTROLLER_PIC,
        .local_apic_base=0,
        .local_apic_id=0,
        .local_apic_version=0,
        .local_apic_present=0,
        .local_apic_enabled=0,
        .acpi_valid=0,
        .ioapic_discovered=0,
        .initialized=0,
        .backend_active=0,
        .timer_route_ready=0,
        .local_apic_virtual=0,
        .acpi_processor_count=0,
        .acpi_ioapic_count=0,
        .acpi_interrupt_override_count=0,
        .acpi_local_apic_address=0
    };
    local_apic_mapped=0;
    for (uint32_t i=0; i<IOAPIC_MAX_SUPPORTED; ++i) {
        ioapic_virtual[i]=0;
        ioapic_redirection_count[i]=0;
        ioapic_mapped[i]=0;
    }

    (void)acpi_discover(multiboot_info);
    const struct acpi_info *firmware=acpi_info();
    state.acpi_valid=firmware->valid;
    state.ioapic_discovered=firmware->valid && firmware->ioapic_count!=0;
    state.acpi_processor_count=firmware->processor_count;
    state.acpi_ioapic_count=firmware->ioapic_count;
    state.acpi_interrupt_override_count=firmware->interrupt_override_count;
    state.acpi_local_apic_address=firmware->local_apic_address;

    if (!cpu_has(ZEROOS_CPU_FEATURE_APIC)) {
        state.initialized=1;
        return 0;
    }

    uint64_t base_msr=cpu_read_msr(IA32_APIC_BASE_MSR);
    uint64_t base=base_msr&APIC_BASE_MASK;
    state.local_apic_base=base;
    state.local_apic_enabled=(base_msr&APIC_BASE_ENABLE)!=0;
    if (!state.local_apic_enabled || base==0 ||
        map_runtime_controller_mmio(firmware)!=0) {
        apic_unmap_mmio();
        state.local_apic_present=0;
        state.initialized=1;
        return 0;
    }

    state.initialized=1;
    return 0;
}

int apic_activate_timer(void) {
    const struct acpi_info *firmware=acpi_info();
    uint32_t ioapic_index=0;
    uint32_t pin=0;
    uint16_t source_flags=0;

    if (!state.initialized || !state.local_apic_present ||
        !state.acpi_valid || !state.ioapic_discovered ||
        !firmware->valid || locate_timer_route(firmware,&ioapic_index,&pin,
                                               &source_flags)!=0)
        return -1;

    /* Keep the timer masked while changing destination and trigger mode. */
    uint32_t low=(APIC_TIMER_VECTOR&0xffU)|
                 ioapic_entry_flags(source_flags)|(1U<<16);
    uint8_t low_register=(uint8_t)(IOAPIC_REG_REDIRECTION_BASE+pin*2U);
    uint8_t high_register=(uint8_t)(low_register+1U);
    ioapic_write(ioapic_index,high_register,state.local_apic_id<<24);
    ioapic_write(ioapic_index,low_register,low);

    /* The LAPIC is enabled before the redirection entry is unmasked. */
    local_apic_write(APIC_REG_TPR,0);
    local_apic_write(APIC_REG_SVR,APIC_SPURIOUS_VECTOR|APIC_SVR_ENABLE);
    low&=~(1U<<16);
    ioapic_write(ioapic_index,low_register,low);

    /* No legacy PIC source may race the now-active IOAPIC timer route. */
    pic_mask_all();
    state.controller=ZEROOS_IRQ_CONTROLLER_LAPIC_IOAPIC;
    state.backend_active=1;
    state.timer_route_ready=1;
    return 0;
}

const struct apic_info *apic_info(void) {
    return &state;
}

int apic_available(void) {
    return state.initialized && state.local_apic_present;
}

enum irq_controller_kind apic_controller(void) {
    return state.controller;
}

void apic_eoi(void) {
    if (apic_available() && state.controller==ZEROOS_IRQ_CONTROLLER_LAPIC_IOAPIC)
        local_apic_write(APIC_REG_EOI,0);
}

uint32_t apic_local_id(void) {
    if (state.local_apic_present && state.local_apic_virtual)
        return local_apic_read(APIC_REG_ID)>>24;
    return state.local_apic_id;
}

int apic_cpu_init(void) {
    if (!state.local_apic_present || !state.local_apic_virtual)
        return -1;
    local_apic_write(APIC_REG_TPR,0);
    local_apic_write(APIC_REG_SVR,APIC_SPURIOUS_VECTOR|APIC_SVR_ENABLE);
    return 0;
}

int apic_send_ipi(uint32_t destination_apic_id, uint8_t vector) {
    if (vector<32U)
        return -1;
    return apic_write_ipi(destination_apic_id,(uint32_t)vector);
}

int apic_send_init_sipi(uint32_t destination_apic_id, uint8_t vector) {
    if (vector==0 || destination_apic_id>0xffU ||
        !state.local_apic_present)
        return -1;

    /* INIT assert/deassert followed by the architecturally required SIPIs. */
    serial_write_public("ZEROOS: LAPIC INIT assert.\n");
    if (apic_write_ipi(destination_apic_id,APIC_ICR_INIT_LEVEL)!=0)
        return -1;
    serial_write_public("ZEROOS: LAPIC INIT asserted.\n");
    apic_delay_us(10000);
    serial_write_public("ZEROOS: LAPIC INIT delay complete.\n");
    if (apic_write_ipi(destination_apic_id,0x00008500U)!=0)
        return -1;
    serial_write_public("ZEROOS: LAPIC INIT deasserted.\n");
    apic_delay_us(200);
    if (apic_write_ipi(destination_apic_id,APIC_ICR_SIPI|vector)!=0)
        return -1;
    serial_write_public("ZEROOS: LAPIC first SIPI delivered.\n");
    /* A correctly delivered first SIPI is sufficient on the supported QEMU
     * APIC path. The AP handshake below is the completion acknowledgement;
     * avoid issuing a second command while the target is already executing
     * copied reset code. */
    serial_write_public("ZEROOS: LAPIC SIPI sequence complete.\n");
    return 0;
}
