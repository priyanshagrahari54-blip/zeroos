# ZEROOS x86-64 entry-boundary invariants

This is a hardware-boundary correction, not an adoption of another OS's
process or scheduler model. Descriptor encodings and exception frames are
fixed by x86-64. ZEROOS retains its existing task/process ownership and APIs.

## Problem and ownership

Interrupts, faults and ring transitions require CPU-readable descriptors and
trusted kernel stacks. The GDT module owns a single CPU's GDT/TSS and two
separate emergency stacks. The interrupt module owns the IDT. The scheduler
updates only TSS.RSP0 when choosing a user thread; it does not repoint IST.
The bootstrap fatal IDT is replaced only after the full GDT/TSS is valid.

## Invariants

- IDT gates are 16 bytes, with IST in byte 4 (bits 0..2), type/DPL/present
  in byte 5, and zero in bytes 12..15. A gate's IST index is NOT pushed.
- A normalized frame, ascending in memory, is vector, error, RIP, CS,
  RFLAGS, RSP, SS. Hardware supplies error codes only on the specified
  exception vectors. A C entry clears DF and aligns the kernel stack.
- LGDT loads the GDT; LIDT loads the IDT. Readback validates base/limit.
- User data is writable, ordinary expand-up data (access 0xF2); user code
  is readable 64-bit code (access 0xFA, L=1, D=0). Kernel gates have DPL0.
- The 104-byte TSS puts RSP0 at offset 4, IST1 at 36, and I/O-map base at
  102. A base of 104 excludes all I/O bitmap bytes, denying user port I/O.
- TSS consumes two GDT slots. LTR receives an available 64-bit TSS (0x89)
  and makes it busy. Software does not pre-mark the descriptor busy.
- Double fault uses IST1, NMI uses IST2. Each owns a separate 16 KiB stack,
  independent of task stacks. Stage 1 is single-CPU; nested NMI/DF recovery
  is not claimed. Fatal faults halt; they do not attempt unsafe recovery.
- NX is required and enabled before NX entries become reachable. Absence
  of NX causes a controlled boot rejection, not executable data fallback.

## Validation and evolution

Compile-time size/offset assertions prevent packing mistakes. Binary checks
verify the actual early IDT and assembly frame consumption. Bounded QEMU
fault injection checks the real delivery path, not just table readback.
Normal integration tests must exercise timer preemption and ring transitions.
User-pointer checks, return-state validation, W^X, and per-thread register
state ownership are separate obligations; correct tables alone do not certify
security. Future multi-CPU support requires one TSS and emergency-stack set
per CPU, with the same ownership rules.
