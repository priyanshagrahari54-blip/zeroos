# ZEROOS architecture

ZEROOS owns its subsystem boundaries, internal APIs, object lifetimes and
security rules. Multiboot2 and x86-64 define interoperability encodings only.
The design question is whether each ownership rule remains coherent without
assuming another operating system exists; familiar terminology is not a
license to import its architecture or implementation.

## Implemented foundation

```text
GRUB / Multiboot2
  -> x86-64 bootstrap, serial diagnostics, early fatal IDT
  -> physical pages: reservations, allocation and exclusive claims
  -> permanent paging: supervisor RX/RO-NX/RW-NX, isolated user roots
  -> kernel heap: bounded validated block chains and coalescing
  -> GDT/TSS/IDT: trusted entry stacks, independent DF/NMI emergency stacks
  -> PIC/PIT: owned IRQ bindings and a 100 Hz clock
  -> task/scheduler: contexts, wait/sleep, IRQ-exit preemption, reclamation
  -> process: identity, parent link, address-space/frame ownership
  -> CPL3 integer-only execution
  -> ZEROOS syscall ABI: exit, yield, write-debug, getpid, gettid
```

## Resource and execution ownership

A process owns its address space, user frames, identity and parent relationship.
A thread (`struct task`) owns CPU continuation state and a kernel stack. Stage 1
has one main thread per user process; a kernel task need not have a process.
The address space owns its optional PCID, not the process builder separately.
Spawn publishes only after resources and the thread/process link are complete;
failure releases both transferred and not-yet-transferred resources.

A terminal thread cannot free its own executing stack. Timer reclamation or a
different task reaping its terminal process releases it under the task lock.
A saved interrupt frame and a cooperative continuation are distinct ownership
states; IRQ-exit scheduling never substitutes one for the other.

## Security boundary

User mappings are restricted to one virtual window, with effective permission
checks through every paging level. Every pointer range is validated before any
copy. Kernel mappings remain supervisor-only. RX user frames have read-only,
non-executable physical aliases; exclusive claims prevent another writable
mapping. Kernel text is RX, constants RO/NX, mutable storage RW/NX after VMM
initialization. CR0.WP and EFER.NXE are required.

User entry clears GPRs except its entry argument and initializes segment state.
CR0.TS enforces the explicit integer-only ABI; unsupported extended-register
instructions fault rather than exposing another thread's state. Syscalls use a
trusted kernel stack and validate return RIP/RSP/flags before SYSRET. Ordinary
user exceptions terminate only that process; kernel exceptions, NMI and #DF
halt. Unimplemented SYSENTER entry is disabled where the CPU implements it.

## Scope and evidence

The current foundation is single-CPU, bounded to a 512 MiB physical aperture,
base-page mappings and fixed process/task capacities. PCID is optional and
incoming-context flushes remain mandatory; no PCID performance benefit is
claimed. [VALIDATION.md](VALIDATION.md) separates implementation, native tests,
actual guest evidence and outstanding hardware coverage.

No ELF loader, general executable ABI, filesystem, device framework, networking,
UI, SMP or floating-point context subsystem is implemented by this task.
Those require their own first-principles designs after Stage 1 closure.
