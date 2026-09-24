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

The Intel architecture defines a PDE with PS=1 as a 2 MiB mapping; ordinary PTEs map 4 KiB pages. citeturn2search12turn2search14

## Current implementation

- Creates a dedicated PML4 and switches CR3 to it.
- Builds missing paging levels from physical pages supplied by the allocator.
- Establishes a compact 2 MiB identity/direct mapping for the current 512 MiB bootstrap physical range.
- Keeps the first 2 MiB executable for the bootstrap/kernel image.
- Marks the remaining bootstrap RAM mappings non-executable.
- Supports 4 KiB map, unmap and software translation for isolated address-space objects.
- Automatically splits a 2 MiB mapping into a 4 KiB PT when a fine-grained mapping is requested.
- Tracks the root loaded in CR3, exposes an explicit kernel-root activation path and rejects destruction of the active address space.
- Uses INVLPG for active leaf mapping changes and a CR3 reload after active unmap/pruning.
- Uses a CR3 reload when changing a paging-structure level during huge-page splitting, so stale translations cannot survive the page-size transition.
- Reclaims empty private PT, PD and PDPT pages after an unmap; the root is retained until the address space is destroyed.
- Address-space destruction is an explicit success/failure operation; callers must activate the kernel root before destroying an active space.
- Explicit leaf mappings require a usable, currently allocated physical page, retain a frame reference and enforce W^X flags; unmap/space teardown release the mapping reference.
- W^X requests remain explicit when NX is absent, but the hardware NX bit is emitted only after CPUID capability detection so unsupported CPUs never consume it as a reserved bit.
- Each isolated address space tracks owned mapped pages and an explicit nonzero page ceiling; the process mapping wrappers use this counter for quota enforcement and cross-check it during teardown.
- Isolated user-range validation walks the space's own page tables and checks present, user and writable permissions without trusting the active kernel root.
- Validated supervisor MMIO pages can be mapped without pretending device registers are allocator-owned RAM; the reserved high-half MMIO window is used by the LAPIC/IOAPIC activation path and is unmapped on failed setup.

Hierarchical page tables avoid allocating a flat table for unused virtual address space, while large mappings reduce page-table depth and TLB pressure. citeturn3search3turn3search7

## Huge-page policy

ZEROOS does not blindly use 4 KiB pages for everything.

| Mapping | Policy |
|---|---|
| Kernel/bootstrap | 2 MiB where alignment/layout permit |
| Large contiguous regions | Prefer 2 MiB mappings |
| Fine-grained mappings | 4 KiB |
| Partial huge-page mapping | Split only the affected 2 MiB region |
| User address spaces | 4 KiB isolated mappings through process-owned quota wrappers |

Intel documents 2 MiB and 1 GiB x86 page sizes and notes their TLB/page-walk benefits, while also warning that large mappings must respect memory-type boundaries. citeturn0search0turn6search13

## TLB discipline

Changing a page-table entry without invalidating cached translations can leave the processor using stale mappings. ZEROOS tracks the active CR3 root: active leaf changes use the TLB service's INVLPG path, active unmaps use a full TLB flush after page-table pruning, and active root switches update the tracker only after loading CR3. Non-active address spaces are modified without local TLB invalidation; they must be activated before execution.

The TLB service owns a sequence-numbered shootdown request and acknowledgement
protocol. The bootstrap CPU is registered locally; additional CPUs are refused
unless an IPI sender is installed, and an unacknowledged remote flush is a
hard failure rather than a silent stale-translation risk. The SMP startup gate
now exercises a remote full flush after AP publication; longer shootdown
stress/soak and address-space concurrency remain separate gates. Intel documents
INVLPG and CR3 reloads as TLB/page-structure invalidation mechanisms. citeturn4search14turn4search15

## Current limits

Still intentionally not implemented:

- page-fault-driven demand allocation
- copy-on-write
- memory-mapped files
- swap/reclaim
- PCID/INVPCID
- SMP TLB shootdown stress/soak coverage beyond the startup acknowledgement
- 1 GiB mapping policy
- user/kernel higher-half layout
- complete per-process VM lifetime integration and fault recovery

These are the next advanced VM layers, not replacements for the current page-table interface.
