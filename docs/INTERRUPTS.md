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

## IST delivery for double fault and NMI

The double-fault (vector 8) and NMI (vector 2) gates use the TSS interrupt
stack table (IST index 1). A double fault can arrive with the current stack
unusable, so it must land on a dedicated, known-good stack instead of the
stack that faulted. The TSS RSP0 slot is set per task (see below), so an
IST double fault always lands on the current task's reserved interrupt
headroom.

Note on gate encoding: the IDT gate's dedicated "ist" byte (offset 4) is
reserved and must be zero. The IST index actually occupies the low three
bits of the access/type byte (offset 5). Storing the index in the reserved
byte silently produces a gate with IST 0, which defeats the whole purpose.

## Ring-3 exception containment

An exception taken while the current task is executing in user mode
(CPL 3, detected from the saved SS) is contained rather than fatal. The
current process is killed (marked zombie with a fault-derived exit code),
the fault is reported, and the scheduler switches to the next task through
the normal IRQ-exit path. The kernel continues running.

An exception taken while the kernel is executing (CPL 0) remains fatal:
it is reported and the machine halts.

The fault handler disables interrupts for its duration. A user-mode fault
arrives with the user's RFLAGS, which may have IF set; if a timer tick
landed in the window between marking the task a zombie and the scheduler
switching away, the tick hook would see a non-running current task and
panic. Disabling interrupts closes that window; the IRETQ epilogue
restores the (new) task's RFLAGS.

## Ring-3 stack pointer (TSS RSP0)

When an interrupt or exception is taken while a task executes in user
mode, the CPU loads RSP from TSS RSP0 (a privilege-level 0 stack). Each
task has a reserved interrupt headroom at the top of its kernel stack, and
the scheduler updates TSS RSP0 to point at that headroom every time it
switches to a different task. This guarantees a ring-3 interrupt always
lands on the current task's own kernel stack, never on a stale or foreign
stack.

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
