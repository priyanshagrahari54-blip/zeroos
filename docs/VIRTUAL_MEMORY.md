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
- Supports 4 KiB map, unmap and software translation.
- Automatically splits a 2 MiB mapping into a 4 KiB PT when a fine-grained mapping is requested.
- Uses INVLPG for leaf mapping changes.
- Uses a CR3 reload when changing a paging-structure level during huge-page splitting, so stale translations cannot survive the page-size transition.

Hierarchical page tables avoid allocating a flat table for unused virtual address space, while large mappings reduce page-table depth and TLB pressure. citeturn3search3turn3search7

## Huge-page policy

ZEROOS does not blindly use 4 KiB pages for everything.

| Mapping | Policy |
|---|---|
| Kernel/bootstrap | 2 MiB where alignment/layout permit |
| Large contiguous regions | Prefer 2 MiB mappings |
| Fine-grained mappings | 4 KiB |
| Partial huge-page mapping | Split only the affected 2 MiB region |
| User address spaces | Future per-process policy |

Intel documents 2 MiB and 1 GiB x86 page sizes and notes their TLB/page-walk benefits, while also warning that large mappings must respect memory-type boundaries. citeturn0search0turn6search13

## TLB discipline

Changing a page-table entry without invalidating cached translations can leave the processor using stale mappings. ZEROOS therefore invalidates a changed leaf mapping and performs a full CR3 reload when the page-size level itself changes. Intel documents INVLPG and CR3 reloads as TLB/page-structure invalidation mechanisms. citeturn4search14turn4search15

## Current limits

Still intentionally not implemented:

- per-process address-space objects
- page-fault-driven demand allocation
- copy-on-write
- memory-mapped files
- swap/reclaim
- page-table page reclamation
- PCID/INVPCID
- SMP TLB shootdown
- 1 GiB mapping policy
- user/kernel higher-half layout

These are the next advanced VM layers, not replacements for the current page-table interface.
