#include "apic.h"
#include "cpu.h"

#define IA32_APIC_BASE_MSR 0x1bU
#define APIC_BASE_MASK 0x000ffffffffff000ULL
#define APIC_BASE_ENABLE (1ULL << 11)
#define APIC_REG_ID 0x020U
#define APIC_REG_VERSION 0x030U
#define APIC_REG_EOI 0x0b0U

static struct apic_info state;

static volatile uint32_t *apic_register(uint64_t base, uint32_t offset) {
    return (volatile uint32_t *)(uint64_t)(base+offset);
}

int apic_init(void) {
    state=(struct apic_info){
        .controller=ZEROOS_IRQ_CONTROLLER_PIC,
        .local_apic_base=0,
        .local_apic_id=0,
        .local_apic_version=0,
        .local_apic_present=0,
        .local_apic_enabled=0,
        .ioapic_discovered=0,
        .initialized=0
    };

    if (!cpu_has(ZEROOS_CPU_FEATURE_APIC)) {
        state.initialized=1;
        return 0;
    }

    uint64_t base_msr=cpu_read_msr(IA32_APIC_BASE_MSR);
    uint64_t base=base_msr & APIC_BASE_MASK;
    state.local_apic_base=base;
    state.local_apic_enabled=(base_msr & APIC_BASE_ENABLE)!=0;

    /*
     * The bootstrap page tables identity-map the low 1 GiB, whereas the
     * architectural LAPIC default is 0xfee00000. Reject an out-of-window
     * relocation instead of dereferencing an unmapped MMIO address.
     */
    if (base==0 || base>=0x40000000ULL || !state.local_apic_enabled) {
        state.initialized=1;
        return 0;
    }

    uint32_t id=*apic_register(base,APIC_REG_ID);
    uint32_t version=*apic_register(base,APIC_REG_VERSION);
    state.local_apic_id=id >> 24;
    state.local_apic_version=version & 0xffU;
    state.local_apic_present=state.local_apic_version!=0;

    /*
     * Do not switch delivery from the legacy PIC without ACPI MADT data.
     * Discovery is still observable and the stable EOI hook is available for
     * the future APIC backend.
     */
    state.initialized=1;
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
        *apic_register(state.local_apic_base,APIC_REG_EOI)=0;
}
