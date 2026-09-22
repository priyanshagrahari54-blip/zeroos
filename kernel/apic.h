#ifndef ZEROOS_APIC_H
#define ZEROOS_APIC_H

#include "types.h"
#include "acpi.h"

enum irq_controller_kind {
    ZEROOS_IRQ_CONTROLLER_PIC=0,
    ZEROOS_IRQ_CONTROLLER_LAPIC_IOAPIC=1
};

struct apic_info {
    enum irq_controller_kind controller;
    uint64_t local_apic_base;
    uint32_t local_apic_id;
    uint32_t local_apic_version;
    uint8_t local_apic_present;
    uint8_t local_apic_enabled;
    uint8_t acpi_valid;
    uint8_t ioapic_discovered;
    uint8_t initialized;
    uint32_t acpi_processor_count;
    uint32_t acpi_ioapic_count;
    uint32_t acpi_interrupt_override_count;
    uint64_t acpi_local_apic_address;
};

/*
 * Probe only: the current supported boot path intentionally retains the PIC
 * until ACPI MADT routing is available. This prevents silently programming a
 * guessed IOAPIC topology. The capability and activation boundary is stable
 * for the APIC/IOAPIC driver that follows.
 */
int apic_init(uint64_t multiboot_info);
const struct apic_info *apic_info(void);
int apic_available(void);
enum irq_controller_kind apic_controller(void);
void apic_eoi(void);

#endif
