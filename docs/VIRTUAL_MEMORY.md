# ZEROOS Virtual Memory

## What this subsystem does

The virtual memory manager (VMM) gives ZEROOS a real x86-64 page-table hierarchy. The CPU translates virtual addresses through a hierarchy rooted at CR3.

Current structure:

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
          v
         PT
          |
          v
      Physical page

## Current implementation

- Creates a new PML4 during vmm_init().
- Builds the required PDPT and page directory using the physical page allocator.
- Switches CR3 to the new address space.
- Preserves a 2 MiB identity mapping for the early kernel and hardware access.
- Provides 4 KiB map, unmap, and translation routines.
- Uses INVLPG after changing a leaf mapping.
- Keeps user/read-write/cache/execute-related flags in the API for later process isolation.

## Why it matters

Physical addresses alone are not enough for a modern multitasking OS. Virtual memory provides controlled address translation and forms the basis for separate process address spaces and memory protection. The CPU's MMU and TLB make these translations part of normal memory access.

## Current limits

This is an early kernel VMM, not the final process memory manager. It does not yet implement:

- per-process address spaces
- demand paging
- page-fault allocation
- copy-on-write
- swap
- memory-mapped files
- ASLR
- page-table reclamation
- automatic splitting of the bootstrap 2 MiB mapping

The bootstrap 2 MiB mapping is deliberately retained while ZEROOS is still executing entirely in low physical memory.

## Performance

Hierarchical tables avoid a giant flat table for the whole virtual address space. Unused regions need no lower-level tables. TLB caching reduces repeated translation cost; page-table walks are mainly needed after translation-cache misses.

## Next

The next memory step is to integrate page-fault handling and then create per-process address spaces. That will turn the VMM into the foundation for real userspace isolation.
