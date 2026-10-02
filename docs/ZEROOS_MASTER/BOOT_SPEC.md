# ZEROOS Boot Specification

## Current boot contract

1. GRUB loads the ZEROOS kernel and supplies a Multiboot2 information structure.
2. The boot assembly establishes the x86-64 execution environment, installs a
   bootstrap-stack guard and transfers control to the kernel.
3. kernel_main validates the stack guard and Multiboot2 magic value before
   consuming the handoff.
4. The kernel discovers physical memory from the Multiboot2 memory map.
5. Virtual memory, synchronization, interrupt routing, and timer infrastructure are initialized in kernel bootstrap order.
6. Scheduler self-tests run before normal kernel task execution.
7. The first scheduler task is entered through the cooperative task context path.
8. Timer IRQs are handled by the normalized ISR path; scheduling is deferred to IRQ exit.

## Boot invariants

- Multiboot2 magic must match the expected value.
- The bootstrap stack guard must remain intact before C initialization.
- Kernel memory and boot metadata must remain reserved by the memory subsystem.
- IDT must be installed before interrupts are enabled.
- Timer IRQ registration must succeed before scheduler preemption is enabled.
- The scheduler must have a valid idle task before the bootstrap context is parked.
- Fatal architectural exceptions halt with serial diagnostics.

## Future boot stages

UEFI-native loading, richer firmware discovery, measured boot, and secure-boot
integration remain roadmap work. The current GRUB/Multiboot2 path validates
ACPI MADT topology, activates a validated LAPIC/IOAPIC timer route, and has a
bounded AP startup boundary with one generation-checked retry and explicit
BSP-only recovery; complete SMP scheduling and broader hardware support remain
later production gates.
