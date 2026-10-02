# ZEROOS — STAGE 3 IMPLEMENTATION PROMPT
Version: 1.0 — Production-Grade Storage and Files
Repository: https://github.com/priyanshagrahari54-blip/zeroos

## Mission
Continue from current `main` after Stage 2. Build real persistent storage and filesystem infrastructure. Inspect actual implementation before coding; preserve valid kernel/userspace contracts.

Read the master prompt and relevant BOOT/HARDWARE/ARCHITECTURE/TECHSPEC/RULES docs first.

## 1. Block-device architecture
Create stable layers:
device discovery → block device → request queue → scheduler → DMA/interrupt completion → cache/filesystem.

Support architecture for:
- SATA/AHCI
- NVMe
- legacy-compatible storage where required
- DMA
- queueing/completions
- timeouts
- cancellation where possible
- error recovery
- hotplug where hardware permits.

Never fake device success.

## 2. Storage scheduling
Implement workload-aware I/O:
- bounded queue depth
- batching
- write coalescing
- foreground protection
- I/O priorities
- HDD-aware seek policy
- SSD parallelism
- NVMe multi-queue behavior
- timeout/error accounting.

Avoid unbounded queues and uncontrolled background writeback.

## 3. Partitioning
Implement production GPT handling:
- header validation
- partition-entry validation
- bounds/overflow checks
- usable-LBA validation
- discovery
- recovery/error reporting
- mount policy.

Malformed partition metadata must not corrupt memory or silently mount unsafe ranges.

## 4. VFS
Implement explicit ownership/lifetime for:
- superblock
- mount
- inode/node
- directory entry
- file object
- descriptor/handle
- permissions
- timestamps
- metadata.

Provide as supported:
open/close/read/write/pread/pwrite/seek/stat/readdir/create/delete/rename/link/fsync/mmap.

Define:
- blocking semantics
- errors
- permission checks
- lifetime/refcounts
- concurrent access
- cancellation
- crash behavior.

## 5. Initial filesystem
Select and document one initial filesystem based on:
crash consistency, maturity, recovery, performance, tooling, licensing and hardware constraints.

Production requirements:
- journaling or equivalent consistency mechanism
- atomic metadata operations
- corruption detection
- repair/check tooling
- mount recovery
- full-disk behavior
- device-error behavior
- clean unmount
- crash recovery.

Do not call a filesystem complete if only read/write happy paths work.

## 6. Page cache
Implement:
- bounded cache
- clean/dirty tracking
- writeback
- eviction
- read-ahead only where justified
- memory-pressure integration
- mmap coherence rules
- fsync semantics
- shutdown flushing
- error propagation.

Ensure dirty data cannot be silently lost after reported successful persistence.

## 7. Storage security
Prepare/implement:
- filesystem permissions
- encryption architecture
- key handling boundaries
- integrity verification
- secure metadata handling
- secure deletion policy only where hardware supports it.

Never log secrets/keys.

## 8. Snapshots/recovery foundation
Implement architecture for:
- snapshots
- rollback metadata
- backup metadata
- interrupted update recovery
- filesystem consistency checking
- recovery environment integration.

Atomicity must be explicit.

## 9. Resource contract
Storage components must be bounded and event-driven. Under pressure:
pause optional work → reduce background priority → reclaim caches → protect foreground I/O → recover only when required.

Measure:
latency, throughput, queue depth, cache hit/miss, writeback, wakeups, CPU overhead and memory use.

## 10. Failure testing
Test:
- corrupted GPT
- malformed filesystem
- full disk
- out-of-space during metadata operation
- I/O timeout
- device disappearance where possible
- failed read/write
- interrupted write
- crash during metadata update
- crash during writeback
- unclean shutdown
- fsync errors
- concurrent access
- memory pressure
- recovery/check tooling.

Run QEMU storage tests and real hardware where applicable.

## 11. Completion
Update VFS/storage/architecture/requirements docs. Zero warnings. No disabled tests. Provide exact evidence and known hardware limitations.

Definition of done: supported persistent storage is bounded, crash-consistent, recoverable, observable and testable through stable userspace interfaces.
