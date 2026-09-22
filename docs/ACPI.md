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

The parser does not program IOAPIC redirection entries, mask the PIC, enable
LAPIC delivery, or claim AP startup. Activation requires, at minimum:

- runtime virtual mappings for LAPIC and every IOAPIC MMIO window;
- verified MMIO read/write behavior;
- a routing ownership table covering legacy IRQs and future GSIs;
- PIC quiescence and EOI ordering;
- per-CPU interrupt-controller state;
- failure rollback to a safe controller state.

Until that boundary is implemented, `apic_controller()` deliberately returns
`ZEROOS_IRQ_CONTROLLER_PIC`, even when a valid MADT is discovered.

## Diagnostics and validation

Boot diagnostics report MADT validity, enabled processor count, IOAPIC count,
source-override count and the parser error code. The boot invariant rejects a
record marked valid unless it has a MADT, a Local-APIC address and at least one
enabled processor. QEMU and supported-hardware validation must still cover
missing ACPI, malformed checksums, truncated records, multiple IOAPICs and
source overrides before APIC activation is considered production-ready.
