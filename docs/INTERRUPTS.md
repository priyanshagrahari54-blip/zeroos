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

Fatal CPU exceptions report vector, decoded exception name, error code, saved RIP, and CR2 for page faults, then enter a halted panic state.

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

- timer_ticks()
- timer_frequency_hz()
- timer_register_tick_hook()

The tick hook is the scheduler insertion point. It runs in interrupt context,
so future scheduler accounting must remain bounded and non-sleeping.

Keeping interrupt work small and separating interrupt-context synchronization
from task-context sleeping is consistent with established kernel designs.
citeturn0search1turn0search4

## Production direction

| Current | Advanced direction |
|---|---|
| 8259 PIC | Local APIC + IOAPIC |
| PIT | APIC/HPET/TSC-backed clock-event layer |
| Global periodic tick | Per-CPU event scheduling / idle tick suppression |
| Single CPU | SMP-aware interrupt routing |
| Single IRQ owner | Shared/managed device IRQ registration where required |
| Hard IRQ handler | Deferred work / threaded device handling |
| No TLB shootdown | SMP invalidation protocol |

The legacy path remains because it gives ZEROOS a deterministic early-boot
interrupt mechanism before the modern interrupt controller and scheduler layers
exist.
