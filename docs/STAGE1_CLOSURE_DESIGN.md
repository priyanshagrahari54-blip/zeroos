# Stage 1 closure design

These are ZEROOS-owned resource and transition rules. Only descriptor, paging,
MSR and instruction encodings come from the x86-64 hardware standard.

## Ownership and failure atomicity

The PMM distinguishes reserved memory from allocated pages. A live allocation
may have one exclusive mapping claim; claims prevent accidental page_free and
second-space mappings. A successful user map transfers responsibility for that
page to its address space. Unmap returns the allocation to the caller;
destruction releases both mappings and allocations. Map failure must leave
ownership with the caller. Every allocation/claim/list mutation runs with IRQs
saved and disabled on this single CPU; nesting restores the original state.

The address space owns its PCID, acquired at create and released at destroy.
PCID is optional. Every CR3 load clears the no-flush bit, invalidating the
incoming context, so reuse is safe even without INVPCID. No global mappings
are installed. Processes must not reserve/release a second copy of the PCID.

Spawn is a transaction: root, code/data/stack, mappings, thread, publication.
Before publication only the builder can see the process. Each page is either
still builder-owned or transferred to the space, never both. Failure unwinds
both sets. The thread is created last while interrupts remain disabled; the
process/parent link is attached before any scheduling can observe it.

## Register boundary

Stage 1 deliberately provides integer-only user execution. Kernel C is built
with general registers only; CR0.TS remains set, and attempts to use x87/MMX/SSE
are contained user exceptions. No extended state can pass between threads.
Initial user GPRs are cleared except the documented entry argument. Adding
floating-point/vector support later requires explicit per-thread state ownership,
not lazy use of whatever registers happen to be on the CPU.

## W^X

Kernel sections are page-aligned: text RX, constants/user-image RO+NX, mutable
storage RW+NX. All remaining direct-map RAM is NX. User code is filled before
publication, then its direct-map alias becomes RO+NX before the user RX PTE is
installed. Removal clears the user mapping before restoring the physical alias
to RW+NX. A claimed executable frame may not acquire a writable second mapping.
General root mapping APIs cannot modify the permanent kernel identity map.

## Validation

Use native tests for arithmetic/list/permission invariants and actual QEMU
faults for hardware enforcement. Inject every page/heap allocation failure in
spawn and compare page, heap, task and PCID counts after rollback. Stress reuse
beyond all fixed slot counts. Exercise same-VA/different-PA spaces while live,
RX writes, NX execution, kernel-memory and port-I/O access, invalid return
state, integer-register sanitization, and disabled extended state. Run bounded
normal/fault cases across available CPU features and RAM sizes. Unsupported
hardware is rejected or follows a documented tested fallback; no green serial
message alone substitutes for these checks.

## Implementation choices after testing

The direct-map aperture uses preallocated 4 KiB leaves (1 MiB of tables for
512 MiB). This is a deliberate bounded-memory tradeoff: publication and
rollback can change one page's permissions without allocating a split table
mid-transaction. Kernel text/constant boundaries are linker-defined and page
aligned. The null page is absent. Generic mapping APIs cannot alter identity
leaves or create executable mappings; user RX publication is the single path
that seals a physical alias.

CI on the ownership change exposed a bootstrap IRQ-exit race. Both the tick
hook and IRQ-exit scheduler must recognize slot 0's boot-stack context until
task_start_first parks it; only real tasks undergo task-stack frame checks.
The fix preserves those checks for every schedulable thread.

### Terminal-thread reclamation

The longer actual user-fault sequence exhausted task slots before the next PIT
tick: process destruction had detached the thread but deferred its stack/slot
reclamation to the timer. A process reaper now reclaims its non-current ZOMBIE
thread under the task lock before releasing the address space. The running
stack is never eligible. A 64-process execution/reap test exceeds all slot
counts without depending on a timer arriving between successive exits.

### Heap allocation identity

A header-looking word sequence is not allocation identity. Free must locate
an exact live block by walking the allocator-owned chain; stale headers inside
coalesced free blocks and forged headers inside payloads do not authorize a
free. All walks validate nonzero aligned sizes against the remaining region
before advancing. Allocation/free fail fatally on chain corruption rather than
looping or following an out-of-range size. Validation itself reports failure.
The header canary detects header damage, not every possible payload overrun.
Native tests exercise forged and coalesced double frees, zero/overflowed block
sizes, damaged canaries, and initialization failure at every backing page.

### Hardware context reuse proof

Allocation bookkeeping cannot prove TLB isolation. The guest reuse probe keeps
B alive, destroys A, reserves and poisons A's retired physical data frame, then
reuses A's identifier with a different frame at the same VA. Sixty-four cycles
must read the new value, never the poison. The matrix records actual PCID and
INVPCID availability and, when KVM exists, runs host features plus a separate
INVPCID-disabled case. A CPU model name alone is not positive feature evidence.
