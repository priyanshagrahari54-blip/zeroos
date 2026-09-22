# ZEROOS Hardware Architecture

## Current target

ZEROOS currently targets x86-64 machines and QEMU's x86-64 virtual hardware.

## Boot-critical hardware

- Firmware/BIOS/UEFI-compatible boot path through the project's GRUB/Multiboot2 flow.
- CPU operating in x86-64 long mode.
- Programmable interrupt controller using the current 8259 PIC layer.
- PIT channel 0 at the current 100 Hz scheduler/timer frequency.
- Serial COM1 diagnostics.
- Conventional page-granular physical memory exposed through the Multiboot2 memory map.

## Current implementation boundary

Implemented hardware-facing layers include boot handoff validation, physical
page discovery/allocation with reserved-page protection, x86-64 page-table
management with W^X/range validation, CPUID/MSR capability discovery, IDT/ISR
entry, 8259 IRQ routing, Local APIC capability probing, PIT delivery,
invariant-TSC/CMOS clock abstractions and serial diagnostics.

ACPI RSDP/root-table/MADT discovery is implemented with checksum, length,
physical-window and entry-boundary validation. The validated controller path
maps LAPIC/IOAPIC MMIO and owns one PIT timer redirection, with an explicit PIC
rollback when validation or activation fails. Full non-timer IOAPIC routing,
PCI/PCIe enumeration, DMA/IOMMU, storage controllers, USB, GPU/display
drivers, audio, ACPI power management, and AP/SMP startup remain unsupported.
Those boundaries are explicit; no unsupported device is silently treated as
active.

## Engineering rule

Hardware-specific code stays behind narrow interfaces so later APIC, PCI, ACPI, storage, USB, graphics, and SMP work does not require rewriting portable scheduler or kernel policy.
