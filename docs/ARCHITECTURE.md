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
          +--> Physical page allocator
          +--> Virtual memory
          |       +--> CR3 / PML4
          |       +--> 2 MiB mappings
          |       +--> 4 KiB mappings
          |       +--> huge-page splitting
          +--> Synchronization
          |       +--> atomic counters
          |       +--> spinlocks
          |       +--> IRQ-safe locking
          +--> Interrupt subsystem
          |       +--> IDT
          |       +--> normalized ISR frames
          |       +--> exception diagnostics
          |       +--> IRQ ownership
          |       +--> timer delivery
          +--> Task / context layer
          |       +--> kernel task objects
          |       +--> kernel stacks
          |       +--> context switching
          |       +--> bounded scheduler
          +--> PIT/8259 bootstrap timer
          |
          v
       Future kernel core
          +--> blocking/wakeup
          +--> preemptive scheduler
          +--> syscall ABI
          +--> user address spaces
          +--> driver framework
          +--> storage/networking/graphics/audio/security
          |
          v
       Userspace
          +--> ZERO Terminal
          +--> Desktop/UI
          +--> Study Center
          +--> App framework
          +--> Forge AI bridge

## Engineering rules

- Every subsystem needs a real hardware/software verification path.
- Avoid temporary APIs that force later callers to depend on implementation details.
- Prefer compact metadata and bounded fast paths.
- Use large pages where they materially reduce translation overhead.
- Keep interrupt handlers minimal.
- Keep synchronization scheduler-independent until task blocking exists.
- Keep idle CPUs asleep rather than generating unnecessary periodic work.
- Keep architecture-specific code isolated from portable kernel logic.
- Keep scheduling policy separate from task/context mechanics.

## Current status

The foundation now has real physical memory discovery/allocation, x86-64
virtual memory, normalized interrupt entry, IRQ ownership, timer delivery,
scheduler-independent synchronization, kernel task objects, and real x86-64
context switching.

The next architectural layer is wait queues plus blocking/wakeup, followed by
interrupt-safe preemption and a fuller scheduler policy.
