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
    uint8_t backend_active;
    uint8_t timer_route_ready;
    uint64_t local_apic_virtual;
    uint32_t acpi_processor_count;
    uint32_t acpi_ioapic_count;
    uint32_t acpi_interrupt_override_count;
    uint64_t acpi_local_apic_address;
};

/* Discover and map only a validated ACPI-described controller topology. */
int apic_init(uint64_t multiboot_info);
/* Activate the validated LAPIC/IOAPIC topology for the PIT timer. */
int apic_activate_timer(void);
/* Route legacy IRQ (0..15) to vector 32+irq and unmask it on the IOAPIC
 * path; -1 when that path cannot serve it (PIC fallback). */
int apic_route_legacy_irq(uint8_t irq);
const struct apic_info *apic_info(void);
int apic_available(void);
enum irq_controller_kind apic_controller(void);
void apic_eoi(void);
uint32_t apic_local_id(void);
int apic_cpu_init(void);
int apic_send_ipi(uint32_t destination_apic_id, uint8_t vector);
int apic_send_init_sipi(uint32_t destination_apic_id, uint8_t vector);

#endif
