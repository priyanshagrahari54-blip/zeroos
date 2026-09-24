#ifndef ZEROOS_ACPI_H
#define ZEROOS_ACPI_H

#include "types.h"

/* Discovery is bounded to the firmware data visible in the bootstrap map. */
#define ZEROOS_ACPI_ERROR_NONE 0U
#define ZEROOS_ACPI_ERROR_NO_HANDOFF 1U
#define ZEROOS_ACPI_ERROR_BAD_MULTIBOOT 2U
#define ZEROOS_ACPI_ERROR_BAD_RSDP 3U
#define ZEROOS_ACPI_ERROR_BAD_ROOT 4U
#define ZEROOS_ACPI_ERROR_NO_MADT 5U
#define ZEROOS_ACPI_ERROR_BAD_MADT 6U
#define ZEROOS_ACPI_ERROR_NO_RSDP 7U

#define ZEROOS_ACPI_MAX_PROCESSORS 256U
#define ZEROOS_ACPI_MAX_IOAPICS 16U
#define ZEROOS_ACPI_MAX_OVERRIDES 32U

struct acpi_processor {
    uint8_t processor_uid;
    uint8_t apic_id;
    uint32_t flags;
};

struct acpi_ioapic {
    uint8_t id;
    uint32_t address;
    uint32_t gsi_base;
};

struct acpi_interrupt_override {
    uint8_t bus;
    uint8_t source;
    uint32_t gsi;
    uint16_t flags;
};

struct acpi_info {
    uint8_t initialized;
    uint8_t valid;
    uint8_t rsdp_revision;
    uint8_t error;

    uint64_t rsdp_physical;
    uint64_t madt_physical;
    uint64_t local_apic_address;
    uint32_t local_apic_flags;
    uint32_t processor_count;
    uint32_t ioapic_count;
    uint32_t interrupt_override_count;
    uint32_t first_ioapic_address;

    struct acpi_processor processors[ZEROOS_ACPI_MAX_PROCESSORS];
    struct acpi_ioapic ioapics[ZEROOS_ACPI_MAX_IOAPICS];
    struct acpi_interrupt_override overrides[ZEROOS_ACPI_MAX_OVERRIDES];
};

int acpi_discover(uint64_t multiboot_info);
const struct acpi_info *acpi_info(void);

#endif
