# ZEROOS Memory Architecture

| Layer | Purpose | Current ZEROOS design | Later design |
|---|---|---|---|
| Physical memory map | Discover usable RAM | Multiboot2 memory map | Keep hardware-derived map |
| Page allocator | Allocate/free 4 KiB pages | Bitmap first-fit allocator | Per-zone/per-CPU allocator |
| Virtual memory | Map virtual addresses | Bootstrap identity map only | Full x86-64 page-table manager |
| Kernel heap | Dynamic allocations | Not implemented yet | Slab/size-class allocator |
| User memory | Process address spaces | Not implemented yet | Per-process virtual address spaces |

## Current allocator

ZEROOS reads the Multiboot2 memory map and treats only entries marked available as allocatable. The allocator tracks up to 512 MiB of physical memory using a bitmap. One bit represents one 4 KiB page, so the bitmap itself is 16 KiB.

The kernel image, page zero, and the Multiboot information structure are reserved so the allocator cannot hand them out.

page_alloc() uses a first-fit scan and returns a 4 KiB physical page. page_free() returns a page to the free pool.

## Why this design

A bitmap is compact and deterministic, which is valuable during early OS bring-up. The trade-off is allocation latency: a first-fit linear scan can become slower as the memory map becomes fragmented. Later ZEROOS can use free lists, zones, and per-CPU caches.

## Performance

The bitmap costs only 16 KiB while covering 512 MiB of physical address space. The current first-fit allocator is simple rather than maximally fast. This is an early kernel primitive, not the final production allocator.

## Next step

Build a virtual-memory manager on top of the physical page allocator. That will let ZEROOS create isolated address spaces, map kernel/user memory, and eventually support process isolation and copy-on-write.
