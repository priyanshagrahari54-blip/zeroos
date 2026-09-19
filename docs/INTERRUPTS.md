# ZEROOS Interrupt Architecture

Interrupts are the kernel event-delivery mechanism. A CPU exception such as an invalid instruction or page fault, and later a hardware event such as a timer tick or keyboard input, enter the kernel through an interrupt handler.

## Vector numbers

x86 provides 256 interrupt vectors. Intel reserves vectors 0-31 for architecture-defined exceptions; vectors 32-255 are available for software-defined/device interrupt use.

## IDT

ZEROOS creates 256 64-bit IDT gate descriptors. Each descriptor stores the handler address, kernel code selector, and gate attributes. The CPU locates the table through IDTR, and the kernel installs it with LIDT.

## Entry path

Assembly stubs normalize entry into: vector, error_code, then the CPU-pushed return frame. Exceptions that architecturally push an error code do not receive an extra dummy value. Other vectors receive a zero placeholder.

The common assembly handler saves general-purpose registers, calls the C dispatcher, restores registers, removes vector and error_code, and returns with IRETQ.

## Current policy

During the foundation stage, CPU exceptions are reported over the serial console and halt the kernel. Hardware IRQ dispatch is the next step.

## Why this comes before the scheduler

The scheduler needs a periodic timer interrupt. The timer cannot safely drive scheduling until the kernel has a reliable IDT and interrupt-return path.

Reference: Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 3, interrupt and exception handling.
