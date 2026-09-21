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
          +--> Process / thread layer
          |       +--> process containers
          |       +--> generation-tagged PID/TID
          |       +--> parent/child ownership
          |       +--> per-process address spaces
          |       +--> thread lifecycle/reaping
          +--> Runtime GDT / TSS
          |       +--> kernel/user selectors
          |       +--> TSS.RSP0
          |       +--> future per-thread entry stacks
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
scheduler-independent synchronization, kernel task objects, real x86-64 context switching, wait queues, timed sleep,
zombie reclamation, scheduler invariants, and IRQ-exit preemption.

The scheduler layer now includes wait queues, timed sleep, zombie reclamation, cooperative context switching, and timer-driven IRQ-exit preemption. Fresh tasks enter through an assembly trampoline with 264 bytes of interrupt headroom. The process/thread layer now provides explicit process containers, generation-tagged PID/TID identity, parent/child ownership, private per-process address-space roots, and explicit thread/process reaping. The runtime GDT/TSS layer is installed and self-tested, with a dedicated initial TSS.RSP0 entry stack. The next architectural boundary is per-user-thread kernel stacks and the Ring-3 entry/return path.
