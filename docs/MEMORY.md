# ZEROOS memory ownership

## Physical pages

The PMM starts with all pages unavailable, releases only complete pages from
Multiboot2 type-1 RAM ranges, then reserves the first 1 MiB, kernel image and
Multiboot information block. The complete tag/entry/end-marker structure and
range arithmetic are validated before any frame is released. Non-available
records dominate overlapping available records regardless of ordering.
Reservations round outward; available RAM rounds
inward. The tracked aperture is capped at 512 MiB, independent of installed RAM.

Three bitmaps distinguish unavailable/free pages, runtime allocations, and
exclusive mapping claims. Each costs 16 KiB for the full aperture; a 256-byte
summary indexes bitmap words containing free pages. There is no dynamic
per-allocation PMM metadata. `page_alloc` uses the bounded summary scan;
`memory_find_free_run` performs a separate bounded contiguous-run search.

A reserved or free page cannot be claimed. A runtime allocation can have one
claim; a second claim or `page_free` while claimed is rejected. Free also
rejects invalid alignment, unallocated pages and double frees. Releasing a
claim does not free the allocation. IRQ save/restore makes bitmap transitions
atomic on the single CPU and preserves nesting state.

This compact representation follows ZEROOS's current bounded ownership needs.
It does not commit future DMA, NUMA, sharing, or physical-memory growth to any
other operating system's allocator architecture.

## Kernel object heap

The heap reserves a contiguous 4 MiB run, falling back to 1 MiB. A failed
backing-page allocation releases every earlier page. It does not repeatedly
allocate/free disconnected pages hoping they become contiguous.

Blocks tile the region: a 32-byte header (`size`, state, header canary, padding)
followed by a 16-byte-aligned payload. First-fit allocation splits only when a
usable remainder remains. `kcalloc` checks multiplication overflow and clears
the requested bytes. Free merges adjacent free blocks in both directions.

Allocation identity comes from the allocator-owned block chain, **not** a
header-looking word sequence before a supplied pointer. Free rejects NULL,
unaligned/out-of-region/interior pointers and stale headers absorbed by
coalescing. Every walk validates nonzero aligned size, remaining bounds and
state/canary before advancing. Alloc/free panic on corrupt metadata instead
of looping or walking outside the region; `heap_validate` reports failure.
IRQ-safe locking covers heap operations and validation.

The canary is in the header: it detects header damage, not every possible
payload overrun. There is no claim of universal buffer-overflow detection.
`kmalloc(0)` returns a minimum allocation; zero-count/size `kcalloc` returns
NULL. Allocation remains first-fit and is not a constant-time allocator.

## Validation

The native PMM suite covers malformed and overlapping boot maps, range
overflow, inward/outward rounding, aperture caps, claims, forbidden frees,
allocation failures, exhaustion/drain and contiguous-run bounds. Native heap
tests use real allocator code with a backing-memory/IRQ fixture: failure at
every initialization page, fallback, exact capacity, overflow, zeroing,
coalesced double frees, forged interior headers, damaged/zero/overflowed header
fields, exhaustion and 10,000 deterministic interleaved operations.

Guest boot tests cover actual writable backing, tiny/odd allocations, full
near-capacity payload writes, invalid frees, calloc, exhaustion/drain and
20,000 mixed operations. Exhaustion uses 1024-byte payloads to bound first-fit
chain visits without abandoning whole-region exhaustion. Fault-enabled builds
also test a forged header. Process failure injection compares page/heap/task/
PCID baselines at every failed spawn step; lifetime stress repeatedly fills
and drains fixed capacities. See VALIDATION.md for actual run results.

Demand paging, shared-frame reference counting, reclaim, page cache, swap and
SMP allocation policy are not part of Stage 1.
