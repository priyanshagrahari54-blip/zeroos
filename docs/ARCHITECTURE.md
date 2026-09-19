# ZEROOS Architecture

## Current implemented foundation

    Firmware / GRUB
          |
          v
    x86-64 long-mode entry
          |
          v
    Kernel bootstrap
          |
          +--> Serial diagnostics
          |
          +--> Physical page allocator
          |       +--> Multiboot2 memory map
          |       +--> compact bitmap
          |       +--> summary index
          |
          +--> Virtual memory
          |       +--> CR3 / PML4
          |       +--> 2 MiB mappings
          |       +--> 4 KiB mappings
          |       +--> huge-page splitting
          |
          +--> Interrupt subsystem
          |       +--> IDT
          |       +--> normalized ISR frames
          |       +--> exception diagnostics
          |       +--> timer IRQ
          |
          +--> PIT/8259 bootstrap timer
          |
          v
       Future kernel core
          |
          +--> scheduler / threads
          +--> syscall ABI
          +--> user address spaces
          +--> driver framework
          +--> storage
          +--> networking
          +--> graphics
          +--> audio
          +--> security
          |
          v
       Userspace
          |
          +--> ZERO Terminal
          +--> Desktop/UI
          +--> Study Center
          +--> App framework
          +--> Forge AI bridge

## Engineering rules

- A subsystem must have a real hardware/software verification path.
- Avoid temporary APIs that force later callers to depend on implementation details.
- Prefer compact metadata and bounded fast paths.
- Use large pages where they materially reduce translation overhead, but split only when fine-grained mappings require it.
- Keep interrupt handlers minimal.
- Keep idle CPUs asleep rather than generating unnecessary periodic work.
- Keep architecture-specific code isolated from portable kernel logic.
- Do not copy another operating system's visual identity or internal implementation.

## Current status

The foundation is no longer just a boot stub: it has real physical memory discovery/allocation, hardware interrupt entry, timer delivery, and x86-64 virtual memory.

It is still not a usable desktop OS. The next major architectural layer is the process/thread + scheduler + syscall boundary, because that is what turns the kernel foundation into a real multitasking operating system.
