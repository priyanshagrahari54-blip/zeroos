# ZEROOS Interrupt Architecture

Interrupts are the kernel event-delivery mechanism. CPU exceptions and hardware events enter through the IDT and normalized assembly entry stubs.

## IDT and entry path

ZEROOS creates 256 64-bit IDT gate descriptors.

The assembly layer normalizes the interrupt stack into:

    dedicated IST stack where required
            |
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

Kernel and platform-fatal CPU exceptions report vector, decoded exception
name, error code, saved RIP, validated privilege/return-frame metadata and CR2
for page faults, then enter a halted panic state. Double fault, NMI, machine
check, page fault and segment/protection faults enter on dedicated TSS IST
pages before diagnostics; this keeps a damaged current/task stack from
becoming the diagnostic stack. Page-fault diagnostics decode
protection/write/user/reserved/instruction-fetch bits.

For a containable exception arriving from Ring 3, the dispatcher records the
fault identity, retires the owning thread with a deterministic fault status,
and never publishes the IST frame as a scheduler-owned task context. Thread
and process lifetime code then performs the normal zombie/reap transition. A
missing thread owner, malformed frame, double fault, NMI or machine check
remains fatal. The policy is armed now; Ring-3 entry and executable fault
injection are later Stage 2 validation gates. Malformed normalized frames are
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
                +--> controller EOI (PIC fallback or LAPIC)

irq_register() installs one owner for each legacy PIC/IOAPIC timer IRQ. irq_unregister()
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

`kernel/acpi.c` validates the Multiboot2 ACPI RSDP, root table and MADT
before exposing processor, IOAPIC and interrupt-override counts. After VMM
initialization, `kernel/apic.c` maps the validated LAPIC/IOAPIC MMIO pages,
checks their version registers, resolves the PIT GSI, and can activate one
masked-then-unmasked timer redirection. The PIC is masked only after the LAPIC
and IOAPIC route is fully programmed; any failure retains the PIC backend.
A guessed APIC route is never considered support.

The active Stage 1 matrix therefore has an explicit legacy-PIC fallback and a
validated LAPIC/IOAPIC timer path. The SMP boundary also uses Local-APIC IPIs
for AP startup and fail-closed TLB shootdowns; non-timer IRQ ownership,
per-CPU device-controller state and multi-CPU scheduling/routing remain
separate gates. Per-CPU interrupt nesting and count are tracked in
`struct cpu_local`.

## Production direction

| Current verified boundary | Next production boundary |
|---|---|
| 8259 PIC fallback or validated LAPIC + IOAPIC timer route | Full Local APIC + IOAPIC IRQ ownership |
| PIT + invariant-TSC clocksource | APIC/HPET/TSC clock-event layer |
| Global periodic tick | Per-CPU event scheduling / idle tick suppression |
| AP startup boundary with per-CPU shape | Per-CPU event scheduling and full SMP interrupt routing |
| Single IRQ owner | Shared/managed device IRQ registration where required |
| Hard IRQ handler | Deferred work / threaded device handling |
| No TLB shootdown | SMP invalidation protocol |

The legacy path remains as a deterministic rollback when firmware, MMIO
mapping, or timer-route validation is unavailable. Boot diagnostics report
which controller path was actually published.
