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

Implemented hardware-facing layers include boot handoff validation, physical page discovery/allocation, x86-64 page-table management, IDT/ISR entry, 8259 IRQ routing, PIT timer delivery, and serial diagnostics.

Not yet implemented as production hardware abstractions: PCI/PCIe enumeration, APIC/IOAPIC, HPET/TSC clocksource selection, DMA/IOMMU, storage controllers, USB, GPU/display drivers, audio, ACPI power management, and SMP.

## Engineering rule

Hardware-specific code stays behind narrow interfaces so later APIC, PCI, ACPI, storage, USB, graphics, and SMP work does not require rewriting portable scheduler or kernel policy.
