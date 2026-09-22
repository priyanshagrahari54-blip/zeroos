# ZEROOS — MEMORY AND STATE MANAGEMENT
Version: 1.0

## 1. Scope
This document defines physical memory, virtual memory, process memory, caches, dormant feature state, memory pressure and persistence rules.

## 2. Physical Memory
The physical allocator owns only firmware-reported available page frames.
Boot-reserved memory includes the kernel image, boot information and required
early structures. The current supported matrix bounds the bootstrap allocator
to the first 512 MiB and reports that boundary explicitly rather than treating
higher memory as usable.

The allocator maintains separate usable and allocation bitmaps plus a summary
bitmap and a per-frame reference count. `page_alloc()` creates the initial
owner reference; `memory_page_retain()` and `memory_page_release()` manage
shared mappings, while `page_free()` releases one owner/reference. A frame is
returned to the free pool only when its reference count reaches zero. Reserved
pages, invalid releases and reference underflow do not mutate accounting; all
allocation/reference operations are serialized by `memory_lock`.

Required mature metadata remains:
- frame state;
- owner/type;
- allocation site in debug builds;
- reference count when shared; (`memory_page_references()` exposes the current debug count);
- zeroed/nonzeroed state where relevant;
- eventual per-CPU/scalable allocation path.

## 3. Virtual Memory
Each process owns an address-space root.
Kernel mappings are controlled and consistent.
User mappings have explicit read/write/execute permissions. Explicit leaf
mapping APIs reject writable-and-executable mappings, reject physical pages
outside the usable allocator range and validate range arithmetic before any
partial mapping is attempted.
Page faults are classified as:
- valid lazy allocation,
- copy-on-write,
- mapped file,
- stack growth,
- invalid access.

Invalid faults terminate the offending process where safe; kernel faults produce diagnostic panic/recovery behavior according to context.

## 4. Memory Classes
### Active
Currently executing or immediately required pages.

### Warm
Small state retained for fast activation.

### Cache
Reclaimable data that can be recreated.

### Dormant
State retained at low cost for quick resume.

### Cold/Discarded
State removed; reconstruction is required.

## 5. Feature Memory Contract
Every optional service declares:
- base resident memory,
- active memory ceiling,
- cache ceiling,
- reclaim priority,
- persistence requirements,
- wake latency target.

The system should keep tiny controllers warm for frequent features while allowing heavy engines to be cold.

## 6. Browser Memory
Tab states:
ACTIVE: full renderer.
IDLE: reduced scheduling.
FROZEN: execution paused, state retained.
DISCARDED: renderer memory reclaimed; navigation/session metadata retained.

Media, downloads and unsaved forms receive special preservation rules.

## 7. AI Memory
AI model memory is not permanently reserved by default.
Model manager:
discover backend -> load model -> serve request -> cache according to policy -> unload/reclaim.
If hardware supports GPU/NPU acceleration, placement is capability-dependent.
Context windows have explicit limits.

## 8. Android/Windows Memory
Compatibility runtimes use separate memory budgets.
When no compatible application is active, runtime memory can be reclaimed.
A tiny launcher/controller may remain warm without keeping the complete runtime resident.

## 9. Kernel Object Memory
Objects such as processes, threads, file descriptors, VM areas and IPC endpoints require explicit lifetime management.
Preferred pattern:
reference ownership -> last reference -> cleanup.
Debug builds should detect leaks and use-after-free.

## 10. Page Reclamation
Pressure levels:
P0 normal
P1 cache reclaim
P2 background throttling
P3 aggressive reclaim
P4 emergency recovery.

Reclaim order should prefer reconstructible caches and dormant services before active foreground state.

## 11. Compression and Swap
Memory compression may reduce disk I/O at the cost of CPU.
Swap is optional and policy-driven.
Do not present swap as equivalent to RAM.
Under sustained pressure, foreground responsiveness is prioritized.

## 12. HDD Rules
HDD systems require conservative background memory/disk behavior:
- batch metadata updates,
- sequentialize maintenance,
- avoid repeated scans,
- defer indexing,
- coalesce writes,
- keep hot metadata cached,
- minimize random I/O.

## 13. GPU Memory
Graphics allocations have ownership and eviction policy.
Textures, surfaces and shader caches must be reclaimable.
The compositor retains only active scene resources where possible.

## 14. Audio/Video Buffers
Buffers are bounded and sized to latency mode.
Playback uses ring buffers and hardware decode/processing when available.
Background media is paused or reduced when user policy allows.

## 15. Memory Safety
No raw pointer crosses a privilege boundary without validation.
No userspace pointer is trusted.
Length arithmetic must detect overflow.
DMA buffers require explicit mapping and lifetime.

## 16. State Persistence
Persistent state is separated from volatile runtime state.
Examples:
settings, document metadata, package state and update slots are persistent.
Scheduler queues, renderer caches and decoded thumbnails are volatile/reclaimable.

## 17. Fast Switch Principle
For instant feature switching:
User action
-> warm controller
-> restore compact state
-> activate heavy engine
-> progressively load optional resources.

The objective is to avoid unnecessary cold boot while avoiding permanent residency of heavy components.

## 18. Memory Telemetry
Expose:
used, free, reclaimable, cached, compressed, mapped, shared, per-process resident and pressure level.
Do not mislead users by treating cache as permanently unavailable memory.

## 19. Testing
Current Stage 1 certification covers:
- allocation/free churn;
- reserved-page release rejection;
- double-free accounting protection;
- process creation;
- address-space destruction;
- W^X mapping rejection;
- mapping range-overflow rejection;
- repeated QEMU scheduler/process boots.

The following remain later production gates:
- page-fault demand allocation;
- concurrent mapping;
- memory pressure and reclaim;
- cache eviction;
- suspend/resume;
- compatibility runtime load/unload.

## 20. Invariants
- no double free,
- no use-after-free,
- no writable alias without policy,
- no executable mapping without explicit permission,
- no unbounded cache,
- no silent memory leak,
- no foreground starvation due to reclaim.

## 21. Long-Term Evolution
Bootstrap allocator -> scalable physical allocator -> object/slab allocator -> demand paging -> COW -> page cache/reclaim -> NUMA-aware policy if required.
Every step must preserve existing ABI and test contracts.


## 22. Production Memory Maturity

Memory management must not be intentionally frozen at a bootstrap/basic level merely because earlier stages are still in progress.

### Physical Memory

Production architecture must account for:
- ownership;
- reference counting;
- fragmentation;
- scalable allocation;
- per-CPU fast paths where justified;
- DMA memory;
- zeroing;
- reserved regions;
- memory pressure.

### Virtual Memory

Production architecture must account for:
- per-process address spaces;
- page faults;
- demand allocation;
- COW;
- mmap-style mappings;
- guard pages;
- stack policy;
- NX/W^X;
- ASLR;
- TLB management;
- SMP shootdowns;
- page-table reclamation.

### Reclaim

Reclaim must distinguish:
- anonymous memory;
- file-backed memory;
- page cache;
- dormant feature state;
- compressed memory;
- optional swap.

Foreground work receives protection under pressure.

### Memory Production Gate

No memory subsystem is production while it has a known critical:
- double free;
- use-after-free;
- ownership violation;
- executable/writable mapping violation;
- unbounded cache;
- silent leak;
- foreground starvation.

All supported paths require stress and fault testing.
