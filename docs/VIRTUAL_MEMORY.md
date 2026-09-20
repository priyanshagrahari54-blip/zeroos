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
- Automatically splits a 2 MiB mapping into a 4 KiB PT when a fine-grained mapping is requested (the split invalidates the exact 4 KiB range with INVLPG; it never reloads CR3, so address spaces other than the one being split are never disturbed).
- Uses INVLPG for leaf mapping changes.

## Per-process address spaces

Each process owns an independent PML4 root (`struct vmm_space`):

- Slot 0 is shared with the kernel root (the identity/direct map), so
  kernel code always runs with the same translations.
- User mappings are allowed only in PML4 slot 254
  (`0x00007f0000000000 .. 0x00007fffffffffff`) and only with the USER flag.
- A user page must never be both writable and executable (W^X): the
  mapper rejects writable+executable combinations outright.
- Every mapped physical page is recorded in a per-space ownership list;
  mapping a page already owned by the same space fails, and destroying
  the space returns every owned page to the physical allocator.
- Each space gets a PCID (1-31) when the CPU supports PCID. CR3 loads are
  PCID-tagged, so switching between processes does not flush the kernel
  TLB entries. When a space is destroyed, its PCID is retired from the
  TLB with INVPCID before the value can be reused by another process; on
  CPUs without PCID, a full TLB flush is performed on every
  address-space switch.

The process layer uses this to give every process a private code page
(executable, not writable), a data page (writable, not executable) and a
stack page (writable, not executable). Two processes may map the same
virtual address to different physical pages; the boot certification
verifies exactly that, in both directions, through the per-space
software translation interface.

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

Changing a page-table entry without invalidating cached translations can leave the processor using stale mappings. ZEROOS therefore invalidates a changed leaf mapping with INVLPG and, when a PCID-tagged address space is destroyed, retires the whole PCID with INVPCID so a reused PCID can never expose the dead space's translations. Intel documents INVLPG, CR3 writes and INVPCID as TLB/page-structure invalidation mechanisms. citeturn4search14turn4search15

## Current limits

Still intentionally not implemented:

- page-fault-driven demand allocation
- copy-on-write
- memory-mapped files
- swap/reclaim
- page-table page reclamation beyond space destruction
- SMP TLB shootdown
- 1 GiB mapping policy
- user/kernel higher-half layout

These are the next advanced VM layers, not replacements for the current page-table interface.
