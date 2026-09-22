# ZEROOS Interrupt Architecture

Interrupts are the kernel event-delivery mechanism. CPU exceptions and hardware events enter through the IDT and normalized assembly entry stubs.

## IDT and entry path

ZEROOS creates 256 64-bit IDT gate descriptors.

The assembly layer normalizes the interrupt stack into:

    saved GPRs
        |
    vector
    error_code
        |
    CPU return frame
        |
      C dispatcher

Exceptions that architecturally push an error code keep that CPU-provided error code. Other vectors receive a synthetic zero error code. The common handler saves and restores all general-purpose registers before returning with IRETQ.

## Exception diagnostics

Fatal CPU exceptions report vector, decoded exception name, error code, saved RIP,
validated privilege/return-frame metadata and CR2 for page faults, then enter
a halted panic state. Page-fault diagnostics decode protection/write/user/
reserved/instruction-fetch bits without attempting unsafe recovery in the
current kernel-only execution boundary. Malformed normalized frames are
rejected before dispatch.

## IRQ ownership and dispatch

Hardware IRQs are now separated from device-specific handling:

    hardware IRQ
         |
         v
    IDT vector 32-47
         |
         v
    common ISR entry
         |
         v
    interrupt_dispatch()
         |
         +--> IRQ binding
                |
                +--> registered handler
                |
                +--> PIC EOI

irq_register() installs one owner for each legacy PIC IRQ. irq_unregister()
requires the same handler/context pair, preventing accidental removal of a
different binding.

The interface is controller-independent enough for later Local APIC/IOAPIC
routing to replace the current 8259 implementation without making drivers own
PIC details.

## Timer

The PIT remains a 100 Hz bootstrap clock. Its IRQ handler only performs tick
accounting and an optional tiny tick hook. It does not perform logging,
filesystem I/O, or scheduler policy.

The timer exposes:

- `timer_ticks()` for monotonic scheduler deadlines;
- `timer_frequency_hz()`;
- `timer_monotonic_ns()`, using invariant TSC when CPUID frequency data is
  valid and PIT fallback otherwise;
- `timer_wallclock_unix_seconds()`, sourced from a stable CMOS RTC sample;
- `timer_clocksource()` for diagnostics;
- `timer_register_tick_hook()`.

The tick hook is the scheduler insertion point. It runs in interrupt context,
so scheduler accounting remains bounded and non-sleeping. Wall-clock changes
never affect monotonic timeout ordering.

Keeping interrupt work small and separating interrupt-context synchronization
from task-context sleeping is consistent with established kernel designs.
citeturn0search1turn0search4

## Controller capability boundary

`kernel/apic.c` probes the Local APIC MSR and version register and exposes a
controller-neutral capability record. It deliberately keeps the 8259 PIC as
the active backend until ACPI MADT data can describe IOAPIC redirection and
interrupt ownership. A guessed APIC route is not considered support.

The active Stage 1 matrix therefore has an explicit legacy-PIC fallback, while
the LAPIC EOI and controller-selection interface is ready for the ACPI/APIC
stage. Per-CPU interrupt nesting and count are tracked in `struct cpu_local`.

## Production direction

| Current verified boundary | Next production boundary |
|---|---|
| 8259 PIC with capability probe | Local APIC + ACPI MADT + IOAPIC |
| PIT + invariant-TSC clocksource | APIC/HPET/TSC clock-event layer |
| Global periodic tick | Per-CPU event scheduling / idle tick suppression |
| One online CPU with per-CPU shape | AP startup and SMP interrupt routing |
| Single IRQ owner | Shared/managed device IRQ registration where required |
| Hard IRQ handler | Deferred work / threaded device handling |
| No TLB shootdown | SMP invalidation protocol |

The legacy path remains because it gives ZEROOS a deterministic early-boot
interrupt mechanism while unsupported modern routing is reported explicitly.
