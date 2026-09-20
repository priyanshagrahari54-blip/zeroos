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

The foundation now has real physical memory discovery/allocation, an x86-64
virtual memory manager with per-address-space roots, PCID-based TLB
isolation (with full-flush fallback), a kernel heap with invariant checking,
normalized interrupt entry, IST-based double-fault/NMI delivery, IRQ
ownership, timer delivery, scheduler-independent synchronization, kernel
task objects, real x86-64 context switching, wait queues, timed sleep,
zombie reclamation, scheduler invariants, and IRQ-exit preemption.

Stage 1 additionally implements:

- **Process/thread model**: a `struct process` owns its `vmm_space`, an
  exclusive set of physical pages (code/data/stack), a PCID, and one main
  thread (`struct task`). The task owns the CPU context; the process owns
  the address space and memory.
- **GDT/TSS**: user code/data segments (DPL 3) and a task state segment
  whose RSP0 carries ring-3 exceptions/interrupts to the current task's
  kernel stack.
- **Ring-3 execution**: user threads start through an assembly entry that
  `iretq`s into the process address space at CPL 3 with IF enabled, so user
  code is genuinely timer-preemptible.
- **SYSCALL/SYSRET syscall ABI** (`docs/SYSCALL_ABI.md`): exit, yield,
  write, getpid, gettid, with whole-range user-pointer validation before
  any user byte is copied.
- **User fault containment**: user-mode page faults and general protections
  kill the current process and leave the kernel running; kernel-mode
  exceptions remain fatal.
- **Address-space isolation**: two processes mapping the same virtual
  address to different physical pages is verified at boot, as is W^X
  enforcement and exclusive physical-page ownership.

The next architectural boundary is an ELF loader with a real user init
process and an expanded syscall set, followed by the VFS/storage layer.
