# ZEROOS ACPI and Interrupt-Topology Discovery

## Scope

ZEROOS consumes ACPI only as a validated firmware description during the
bootstrap phase. It does not treat a discovered table as permission to enable
an interrupt controller. The active Stage 1 delivery backend remains the
8259 PIC until all MMIO and routing ownership contracts are installed.

## Discovery path

`acpi_discover(multiboot_info)`:

1. bounds-checks the Multiboot2 information block and tag walk;
2. prefers a valid Multiboot2 ACPI-new RSDP tag and falls back to ACPI-old;
3. validates the RSDP signature, revision, length and checksum;
4. validates the RSDT/XSDT physical window, header, length and checksum;
5. locates and validates the `APIC` MADT;
6. validates every MADT record boundary and minimum record size;
7. retains bounded processor, IOAPIC and interrupt-source-override records.

All firmware addresses consumed by this early parser must be below the
bootstrap identity-map limit. Higher addresses are rejected rather than
accessed through an assumed mapping. A missing or malformed ACPI handoff is
reported in `struct acpi_info.error` and leaves the explicit PIC fallback
available.

## Retained topology

The immutable discovery record contains:

- enabled Local-APIC processor records;
- IOAPIC ID, MMIO address and GSI base records;
- legacy IRQ to GSI source overrides and polarity/trigger flags;
- MADT Local-APIC address and flags;
- physical addresses of the validated RSDP and MADT;
- bounded aggregate counts and an error code.

The arrays have fixed supported bounds. Exceeding a bound fails MADT
validation, so firmware cannot cause an unbounded allocation or silent record
truncation.

## Activation boundary

The parser remains observation-only: it never programs firmware-described
hardware. After the kernel VMM is live, `apic_init()` maps the validated LAPIC
and IOAPIC pages into a reserved supervisor MMIO window, validates the LAPIC
and IOAPIC version registers, and retains the mappings only when all accesses
are within the discovered topology. `apic_activate_timer()` then owns the
narrow first activation boundary:

- it resolves the PIT IRQ through the MADT source-override table;
- programs one masked IOAPIC redirection to the bootstrap LAPIC;
- enables the LAPIC with a known spurious vector and task-priority state;
- unmasks the timer only after the destination and trigger fields are valid;
- masks the legacy PIC before publishing the LAPIC/IOAPIC backend state.

If any validation fails, activation is not published and the PIC remains the
safe backend. Non-timer legacy IRQ routing, per-CPU controller state, AP
startup, and full rollback for a future multi-route transition remain separate
Stage 1 gates.

## Diagnostics and validation

Boot diagnostics report MADT validity, enabled processor count, IOAPIC count,
source-override count and the parser error code. The boot invariant rejects a
record marked valid unless it has a MADT, a Local-APIC address and at least one
enabled processor. The QEMU gate also requires an explicit timer-routing
outcome, whether validated LAPIC/IOAPIC activation succeeds or the legacy PIC
fallback is retained. Supported-hardware validation must still cover missing
ACPI, malformed checksums, truncated records, multiple IOAPICs and source
overrides before broader APIC activation is production-ready.
