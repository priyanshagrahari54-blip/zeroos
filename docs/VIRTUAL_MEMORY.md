# ZEROOS virtual memory

## Problem and ownership boundary

Processes need identical virtual layouts without sharing mutable storage.
ZEROOS assigns each address space a root and an exclusive list of user frames;
a process owns that space, while a thread owns execution state. The x86-64
four-level table format is the hardware boundary, not an imported internal API.
See [the closure design](STAGE1_CLOSURE_DESIGN.md) for transaction invariants.

## Permanent kernel map

After `vmm_init`, the first 512 MiB aperture uses 4 KiB leaves, with page zero
absent and every mapping supervisor-only. Linker-aligned regions are:

| Region | Permissions |
|---|---|
| Kernel text | read/execute, not writable |
| Constants and embedded user-image source | read-only, NX |
| Kernel data, stacks and other direct-map RAM | read/write, NX |

CR0.WP and EFER.NXE enforce these permissions for the kernel too. Unsupported
NX CPUs are rejected. The temporary assembly paging used to enter long mode
precedes this policy; no user thread exists during that bootstrap transition.
Preallocating the aperture's 256 leaf tables costs 1 MiB and avoids allocation
or huge-page splitting in permission-publication/rollback transactions.
General root mapping/protection/unmap APIs cannot alter the aperture or create
executable aliases. Root and user mutators reject unsupported flag bits.

## User spaces and W^X

Each `vmm_space` has an independent PML4. Slot 0 shares the supervisor kernel
map; user mappings are confined to slot 254:
`0x00007f0000000000 .. 0x00007fffffffffff`.
The initial layout has one RX code page, an RW/NX data page, and an RW/NX
stack page, separated by unmapped holes. Writable-executable leaves are denied.

The PMM distinguishes reservations, runtime allocations and exclusive mapping
claims. Mapping a reserved/free frame or an already claimed frame fails,
including cross-space duplication. A claim also prevents premature `page_free`.
Code is filled through an RW/NX physical alias **before** publication. Its
physical alias is changed to RO/NX before the user RX PTE becomes visible.
Unmap removes the RX mapping before restoring the physical alias to RW/NX.
Thus the same frame cannot remain writable through the kernel direct map.

A successful map transfers responsibility to the space. Unmap returns the
unclaimed allocation to the caller; destroy frees its remaining user frames,
private tables, descriptors and optional PCID. A failed map retains caller
ownership; empty private tables can remain until space destruction. Process
spawn rollback destroys the entire partial space and frees untransferred pages.

## TLB and PCID discipline

PCID is optional and detected from CPUID, not assumed from the CPU model name.
A space acquires its own ID (1–31) and releases it at destruction. Every CR3
load clears bit 63: the incoming context is flushed, even with a PCID. No
GLOBAL mappings are installed and **no retained-TLB performance benefit is
claimed**. INVPCID is used only if separately supported; safe reuse does not
depend on it. Non-PCID CPUs use the full-flush fallback.

Current-space leaf changes invalidate cached translations. Every later
activation flushes the incoming context, including shared-alias permission
changes made while a different space was active. This is a single-CPU rule;
SMP will require a separate invalidation/ownership design.

## User range validation

The walker checks canonical endpoints, overflow, the user window, every covered
page, and effective present/user/write permissions at all paging levels.
Unsupported upper-level huge entries are rejected rather than dereferenced as
tables. No user byte is copied before the complete range passes validation.
The production mapper installs base pages only; software walking of existing
2 MiB leaves does not imply a user huge-page allocation policy.

## Evidence and limits

Native tests exercise range boundaries, ownership, claim/free ordering,
allocation failures, and PCID allocation/exhaustion/reuse. Guest tests perform
64 alternating live-CR3 accesses to the same VA with different physical
backing, then check resource reclamation. Sixty-four identifier-reuse cycles
retain and poison the retired data frame while mapping a different frame at
the same VA, detecting stale translations even when allocator reuse might
otherwise conceal them. Separate fatal images provoke kernel
text writes, NX execution, and writes to a sealed executable alias; CPL3 cases
try code writes, data/stack execution and kernel reads. Exact run results and
actual PCID hardware coverage are in [VALIDATION.md](VALIDATION.md).

Not implemented: demand allocation, COW, shared user pages, file mappings,
swap/reclaim, general huge-page policy, SMP shootdown, or higher-half relocation.
These are future designs, not claims about the current foundation.
