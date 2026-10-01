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

/* Why S5 (soft-off) is unavailable; reported at boot and by the power path. */
#define ZEROOS_ACPI_S5_OK 0U
#define ZEROOS_ACPI_S5_NO_FADT 1U
#define ZEROOS_ACPI_S5_HW_REDUCED 2U
#define ZEROOS_ACPI_S5_NO_PM1A 3U
#define ZEROOS_ACPI_S5_NO_DSDT 4U
#define ZEROOS_ACPI_S5_NO_OBJECT 5U
#define ZEROOS_ACPI_S5_BAD_OBJECT 6U
#define ZEROOS_ACPI_S5_NOT_DISCOVERED 7U

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

    /* Power management, parsed once from FADT/DSDT at discovery. The
     * power-off/reset path touches only these I/O ports and never
     * dereferences firmware memory late in shutdown. */
    uint64_t fadt_physical;
    uint64_t dsdt_physical;
    uint8_t s5_supported;
    uint8_t s5_error;             /* ZEROOS_ACPI_S5_* */
    uint8_t reset_supported;      /* FADT RESET_REG in system I/O space */
    uint8_t acpi_enable_value;
    uint8_t reset_value;
    uint8_t reserved_power[3];
    uint16_t pm1a_control_port;
    uint16_t pm1b_control_port;   /* 0 when absent */
    uint16_t smi_command_port;    /* 0 when the platform is ACPI-only */
    uint16_t reset_port;
    uint16_t slp_typ_a;           /* \_S5_ package element 0 (3 bits) */
    uint16_t slp_typ_b;           /* \_S5_ package element 1 (3 bits) */

    struct acpi_processor processors[ZEROOS_ACPI_MAX_PROCESSORS];
    struct acpi_ioapic ioapics[ZEROOS_ACPI_MAX_IOAPICS];
    struct acpi_interrupt_override overrides[ZEROOS_ACPI_MAX_OVERRIDES];
};

int acpi_discover(uint64_t multiboot_info);
const struct acpi_info *acpi_info(void);
/* Short reason string for an s5_error value (never NULL). */
const char *acpi_s5_error_name(uint8_t error);
/* Pure AML helper, exposed for self-testing: find `Name(_S5_, Package{a,b,..})`
 * in a DSDT/SSDT body. Returns ZEROOS_ACPI_S5_OK, _NO_OBJECT or _BAD_OBJECT. */
uint8_t acpi_parse_s5(const uint8_t *aml, uint32_t length,
                      uint16_t *slp_typ_a, uint16_t *slp_typ_b);
/* Runs the AML parser against built-in good/malformed vectors. 0 = pass. */
int acpi_s5_self_test(void);

#endif
