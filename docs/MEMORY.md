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

On top of the page allocator ZEROOS now has a small kernel object heap
(`kernel/heap.{h,c}`). It allocates physical pages from the PMM, extends
them into the kernel identity region through the VMM (identity mappings
only exist in the kernel root; PCID-1 TLB entries from user mode are never
flushed by kernel identity maps), and carves 8-byte-aligned chunks from a
per-page freelist.

Each chunk stores its size in the low bits of the previous chunk's tail, and
the heap keeps a generation counter plus a canary region that must survive
every operation. `heap_self_test()` runs at boot and verifies: allocation
and free, repeated growth, double free, out-of-range free, canary
integrity, and freelist corruption detection — all without any page
allocator or VMM modification.

## Next memory layers

The intended progression is:

1. Kernel object allocator for sub-page objects.
2. Higher-order/contiguous allocation.
3. Page ownership/reference tracking.
4. Per-process address spaces.
5. Demand paging and copy-on-write.
6. Reclaim/page cache/swap policies where useful.

That keeps the current fast page primitive useful instead of replacing every caller later.
