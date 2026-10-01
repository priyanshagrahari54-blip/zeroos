# ZEROOS Virtual Memory

## Architecture

ZEROOS uses the x86-64 four-level paging hierarchy:

    Virtual address
          |
          v
        PML4
          |
          v
        PDPT
          |
          v
         PD
          |
          +---- 2 MiB huge page
          |
          v
         PT
          |
          v
       4 KiB page
          |
          v
      Physical memory

A PDE with PS=1 maps a 2 MiB page; ordinary PTEs map 4 KiB pages.

## Current implementation

- Creates a dedicated PML4 and switches CR3 to it.
- Builds missing paging levels from physical pages supplied by the allocator.
- Establishes a compact 2 MiB identity/direct mapping for the current 512 MiB bootstrap physical range.
- Keeps the first 2 MiB executable for the bootstrap/kernel image.
- Marks the remaining bootstrap RAM mappings non-executable.
- Supports 4 KiB map, unmap and software translation for isolated address-space objects.
- Automatically splits a 2 MiB mapping into a 4 KiB PT when a fine-grained mapping is requested.
- Tracks the root loaded in CR3, exposes an explicit kernel-root activation path and rejects destruction of the active address space.
- Uses the TLB service for synchronous page invalidation across registered CPUs before a mapped frame is released; address-space roots are tracked per CPU.
- Unlinks empty private page-table levels only after the leaf shootdown, then invalidates the affected VA again before freeing the table page, preventing stale page-walk references to recycled tables.
- Uses a CR3 reload when changing a paging-structure level during huge-page splitting, so stale translations cannot survive the page-size transition.
- Reclaims empty private PT, PD and PDPT pages after an unmap; the root is retained until the address space is destroyed.
- Address-space destruction is an explicit success/failure operation; callers must activate the kernel root before destroying an active space.
- Explicit leaf mappings require a usable, currently allocated physical page, retain a frame reference and enforce W^X flags; unmap/space teardown release the mapping reference.
- W^X requests remain explicit when NX is absent, but the hardware NX bit is emitted only after CPUID capability detection so unsupported CPUs never consume it as a reserved bit.
- Each isolated address space tracks owned mapped pages and an explicit nonzero page ceiling; the process mapping wrappers use this counter for quota enforcement and cross-check it during teardown.
- Isolated user-range validation walks the space's own page tables and checks present, user and writable permissions without trusting the active kernel root.
- Validated supervisor MMIO pages can be mapped without pretending device registers are allocator-owned RAM; the reserved high-half MMIO window is used by the LAPIC/IOAPIC activation path and is unmapped on failed setup.

Hierarchical page tables avoid allocating a flat table for unused virtual address space, while large mappings can reduce page-table depth and TLB pressure.

## Huge-page policy

ZEROOS does not blindly use 4 KiB pages for everything.

| Mapping | Policy |
|---|---|
| Kernel/bootstrap | 2 MiB where alignment/layout permit |
| Large contiguous regions | Prefer 2 MiB mappings |
| Fine-grained mappings | 4 KiB |
| Partial huge-page mapping | Split only the affected 2 MiB region |
| User address spaces | 4 KiB isolated mappings through process-owned quota wrappers |

Large mappings must respect alignment and memory-type boundaries; ZEROOS currently uses 2 MiB pages where its validated layout permits and does not enable 1 GiB pages.

## TLB discipline

Changing a page-table entry without invalidating cached translations can leave
a CPU using stale mappings. ZEROOS tracks active CR3 roots per CPU and routes
mapping changes through the sequence-numbered TLB service. A page invalidation
executes local `INVLPG`, sends a bounded IPI request to every other registered
CPU, and waits for each acknowledgement before the caller can release a mapped
frame. Address-space activation publishes the root only after loading CR3.

Unmap first removes the leaf entry and completes the synchronous shootdown,
then releases the retained frame reference. If private page-table levels
become empty, each parent entry is unlinked and the affected VA is invalidated
again before the page-table backing page is freed. Address-space destruction
is rejected while any registered CPU still has that root active. A missing IPI
sender or unacknowledged remote invalidation is a hard failure, never a silent
stale-translation risk. QEMU boot tests cover remote invalidation and the
frame/table retirement boundaries; longer concurrent shootdown stress, soak
and physical-hardware validation remain open.

## Current limits

Still intentionally not implemented:

- page-fault-driven demand allocation
- copy-on-write
- swap/reclaim
- PCID/INVPCID
- long-duration concurrent SMP TLB shootdown stress/soak and physical-hardware validation
- 1 GiB mapping policy
- user/kernel higher-half layout
- broader user-fault recovery beyond the bounded fail-closed process path

These are the next advanced VM layers, not replacements for the current page-table interface.
