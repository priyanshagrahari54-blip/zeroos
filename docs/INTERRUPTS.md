# ZEROOS Interrupt Architecture

Interrupts are the kernel event-delivery mechanism. A CPU exception such as an invalid instruction or page fault, and hardware events such as the timer or keyboard, enter the kernel through an interrupt handler.

## IDT

ZEROOS creates 256 64-bit IDT gate descriptors. Each descriptor stores the handler address, kernel code selector, and gate attributes. The CPU locates the table through IDTR, and the kernel installs it with LIDT. Intel documents the IDT and interrupt/exception handling in the system programming manual.

## Entry path

Assembly stubs normalize entry into: vector, error_code, then the CPU-pushed return frame. Exceptions that architecturally push an error code do not receive an extra dummy value. Other vectors receive a zero placeholder.

The common assembly handler saves general-purpose registers, calls the C dispatcher, restores registers, removes vector and error_code, and returns with IRETQ.

## Hardware IRQ path

The first hardware interrupt source is the legacy 8259 PIC plus PIT timer:

    PIT channel 0
          |
          v
        IRQ0
          |
          v
    8259 PIC master
          |
          v
    vector 32
          |
          v
      IDT entry
          |
          v
    isr_stub_32
          |
          v
    interrupt_dispatch()
          |
          v
      timer_tick()
          |
          v
       tick count
          |
          v
       scheduler

The 8259 PIC is remapped so IRQ0-IRQ15 use vectors 32-47 instead of overlapping CPU exception vectors. ZEROOS currently unmasks only IRQ0 and keeps the slave cascade line masked except for the required master cascade configuration. The PIC is a compatibility/foundation mechanism; modern x86 systems generally use APIC-family interrupt controllers instead.

## PIT timer

PIT channel 0 is connected to IRQ0. ZEROOS programs it for 100 Hz, giving the kernel a periodic tick about every 10 ms. The timer tick is the time base that will later allow preemptive scheduling.

## Current policy

CPU exceptions are reported over the serial console and halt the kernel. IRQ0 drives the timer counter. The kernel prints a heartbeat after 100 ticks so QEMU can verify that timer interrupts are actually arriving.

## Why this comes before the scheduler

A scheduler needs a reliable periodic interrupt to regain control from a running task. Establishing the timer path first gives ZEROOS a hardware-driven preemption clock.

## Performance and design trade-off

The 8259/PIT path is intentionally simple and useful for early bring-up, but it is legacy hardware. Later ZEROOS should move to Local APIC/IOAPIC or another modern timer/interrupt source for SMP and production scheduling. The lightweight part is the small handler path: the IRQ does only accounting and EOI work, while larger scheduling decisions can happen outside the lowest-level interrupt path.

References: Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 3; OSDev references for 8259 PIC and PIT bring-up.
