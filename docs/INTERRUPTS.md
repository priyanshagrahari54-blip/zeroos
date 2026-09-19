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

The common return path is deliberately explicit because register-frame corruption in an interrupt path can destroy the entire kernel.

## Exception diagnostics

Fatal CPU exceptions now report:

- vector
- decoded exception name
- error code
- saved RIP
- CR2 for page faults

The kernel then enters a halted panic state. This gives the next memory/protection stages a reliable diagnostic path instead of silently hanging.

## Current hardware IRQ path

The current bring-up timer path remains:

    PIT channel 0
          |
          v
        IRQ0
          |
          v
       8259 PIC
          |
          v
      IDT vector 32
          |
          v
       ISR stub
          |
          v
    interrupt_dispatch()
          |
          v
       timer_tick()

The PIC is remapped away from CPU exception vectors. Only IRQ0 is currently enabled.

## Timer

The PIT currently provides a 100 Hz bootstrap scheduling clock. The handler performs only tick accounting and EOI work.

This is intentionally kept small: timer interrupts should not perform scheduler policy, I/O, filesystem work or logging in the hard interrupt path.

The production timer design will move toward APIC-based per-CPU clock events and dynamic/tickless idle. Periodic scheduler ticks are useful for preemption, but waking an otherwise idle CPU purely for a periodic tick wastes idle residency; dynamic tick designs avoid that when no timer event requires the wakeup. citeturn5search1turn5search6

## Production direction

| Current | Advanced direction |
|---|---|
| 8259 PIC | Local APIC + IOAPIC |
| PIT | APIC/HPET/TSC-backed clock-event layer |
| Global periodic tick | Per-CPU event scheduling / idle tick suppression |
| Single CPU | SMP-aware interrupt routing |
| No IRQ ownership | Driver IRQ registration |
| No deferred work | Top-half/bottom-half style split |
| No TLB shootdown | SMP invalidation protocol |

The legacy path remains only because it gives ZEROOS a deterministic early-boot interrupt mechanism before the modern interrupt controller and scheduler layers exist.
