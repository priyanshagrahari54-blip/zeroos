# ZEROOS Memory Architecture

| Layer | Purpose | Current implementation | Production direction |
|---|---|---|---|
| Firmware map | Discover RAM and reserved ranges | Multiboot2 memory map | Keep hardware-derived map |
| Physical allocator | Allocate/free 4 KiB pages | Compact bitmap + summary index | Zones/buddy + per-CPU caches |
| Allocation search | Find a free page quickly | Hierarchical summary, bounded scan | Per-CPU fast paths |
| Kernel metadata | Avoid corrupting boot/kernel data | Page 0, kernel image and Multiboot info reserved | Full boot-memory reservation model |
| Physical range | Keep bootstrap simple | First 512 MiB tracked | Extend from firmware map as higher-memory support lands |

## Current allocator

ZEROOS starts with all tracked pages reserved and releases only pages reported as type 1 (available RAM) by the Multiboot2 memory map. The kernel image, page zero and the Multiboot information structure are then reserved again.

The allocator stores one bit per 4 KiB page. The 512 MiB bootstrap range therefore needs 16 KiB for the primary bitmap. A small summary bitmap records which bitmap words still contain free pages.

This changes the old first-fit design from potentially scanning all 131,072 pages to scanning at most the small summary hierarchy plus one 64-bit word. Allocation/free accounting remains deterministic and the metadata stays very small.

## Why this is an intentional intermediate architecture

A full production allocator needs zones, fragmentation management, higher-order contiguous allocations and eventually per-CPU caches. Linux, for example, uses zones and a buddy allocator, with per-CPU page sets to keep frequent allocations away from global allocator contention. citeturn1search0turn1search12

ZEROOS is not pretending the current allocator is the final NUMA/driver-grade allocator. It is now a fast bootstrap allocator with an interface that can later be backed by those mechanisms without changing callers.

## Resource budget

| Resource | Current cost |
|---|---:|
| Tracked physical range | 512 MiB |
| Base-page size | 4 KiB |
| Primary bitmap | 16 KiB |
| Summary bitmap | 256 bytes |
| Allocation search | Bounded by summary levels |
| Per-allocation dynamic metadata | None |

The allocator itself does not reserve a large heap or create per-page structs, so the bootstrap footprint stays small.

## Kernel heap (Stage 1)

On top of the page allocator ZEROOS has a small kernel object heap
(`kernel/heap.{h,c}`). At boot it consumes contiguous physical pages from
the PMM (a 1 MiB floor, up to 4 MiB) into one strictly linear region that
is already covered by the kernel's identity mappings, and runs a first-fit
block allocator over it.

Block layout: a 32-byte header (`size`, in-use magic, canary, padding)
followed by the payload. Blocks are 16-byte multiples and tile the region
exactly; the padded header keeps every payload 16-byte aligned. `kmalloc`
splits the first free block that fits (leaving a minimum-size remainder),
`kcalloc` is overflow-safe and zero-fills, and `kfree` rejects NULL,
unaligned, out-of-region, and interior pointers, detects double frees, and
panics on canary corruption (a buffer overflow past the allocated chunk).

Coalescing in `kfree` merges the freed block with its physically preceding
block (if free) and with every physically following block while free, so
adjacent free blocks never persist and the largest contiguous run stays
available.

`heap_self_test()` runs at boot and is fully bounded and deterministic:
small/odd allocations with full-payload writes, 16-byte alignment,
negative frees (double free, NULL, unaligned, out-of-region, interior
pointer), exact-capacity and over-capacity boundaries, a near-maximum
allocation, kcalloc zero-fill and count-overflow rejection, whole-region
exhaustion followed by drain, and a bounded interleaved alloc/free stress
(LCG-driven). `heap_validate()` walks the block chain and is used as a
post-condition after the exhaustion and stress phases.

## Next memory layers

The intended progression is:

1. Kernel object allocator for sub-page objects.
2. Higher-order/contiguous allocation.
3. Page ownership/reference tracking.
4. Per-process address spaces.
5. Demand paging and copy-on-write.
6. Reclaim/page cache/swap policies where useful.

That keeps the current fast page primitive useful instead of replacing every caller later.
