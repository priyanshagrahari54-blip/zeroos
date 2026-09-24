# ZEROOS Stage 3 — Storage Stack

Status: implemented and certified in QEMU (see "Evidence"). Items marked
**PARTIAL** have stable contracts but incomplete implementations; they are
listed again in "Known limitations".

Companion documents: [VFS.md](VFS.md) (namespace, objects, system calls),
[ZJFS.md](ZJFS.md) (on-disk format, journaling, check/repair).

## 1. Layering

```
PCI discovery ─► AHCI / NVMe / ramdisk driver ─► struct block_device (+ GPT partition views)
   ─► bounded request pool ─► per-device scheduler (priority, aging, C-SCAN / FIFO, merging)
   ─► driver submit + kick (PRD / PRP DMA) ─► MSI / MSI-X completion (poll fallback)
   ─► retry / timeout / reset / failure accounting ─► block_complete() ─► caller
   ─► page cache (file data) / ZJFS buffer cache (metadata) ─► ZJFS ─► VFS ─► syscalls
```

| Layer | Source | Contract header |
|---|---|---|
| Block core, scheduler, fault injection, watchdog | `kernel/storage/block.c` | `block.h` |
| AHCI SATA | `kernel/storage/ahci.c` | `ahci.h` |
| NVMe | `kernel/storage/nvme.c` | `nvme.h` |
| RAM disks (tests, `/ram`) | `kernel/storage/ramdisk.c` | `ramdisk.h` |
| GPT | `kernel/storage/gpt.c` | `gpt.h` |
| Page cache | `kernel/storage/pagecache.c` | `pagecache.h` |
| VFS | `kernel/storage/vfs.c` | `vfs.h` |
| ZJFS | `kernel/storage/zjfs.c` | `zjfs.h` |
| File syscalls, mmap | `kernel/storage/fsyscall.c` | `fsyscall.h` |
| Orchestration, mount policy, worker | `kernel/storage/storage.c` | `storage.h` |
| Host tools | `tools/storage/gpt.py`, `tools/storage/zjfs.py` | — |

The storage manager is one kernel task (`storage_main`). It runs the
self-tests, probes hardware, applies the mount policy and then becomes
the storage worker (see §9).

## 2. Block layer

### Objects and ownership
- `struct block_device` is either a physical device (with `ops` and a
  request pool) or a partition view (`parent` set, offset and length
  checked on every request, optional read-only flag).
- Requests come from the physical device's **fixed pool**:
  `clamp(2 × queue_depth, 8, 64)` entries. A caller allocates with
  `block_request_alloc()` and frees after completion. Allocation blocks
  (or returns NULL for `nowait`) when the pool is exhausted. That is the
  backpressure; nothing is allocated on the I/O path.
- Completion callbacks run in IRQ context and must not block. The
  synchronous helpers (`block_rw`, `block_flush`) wait on a completion.

### Scheduler (bounded, workload-aware)
- Three priorities: FOREGROUND (synchronous user I/O, fsync, journal),
  NORMAL (metadata, mount) and BACKGROUND (writeback).
- A request queued for longer than `BLOCK_AGING_TICKS` (50 ticks) is
  promoted, so background I/O cannot starve.
- **Foreground protection:** background I/O may not use the last quarter
  of the pool, nor more than a quarter of the hardware depth
  (`bg_throttled` counter).
- **Per-media ordering:**
  - HDD (rotational): C-SCAN by LBA.
  - SSD: FIFO within a priority.
  - NVMe: FIFO with CPU-local hardware queues (`hw_queue = cpu % hwq`).
- **Merging:** back-merge of adjacent same-direction requests, up to
  `BLOCK_MAX_SEGMENTS` segments and the device's max transfer. Front-merge
  is **PARTIAL** (not implemented). A merged request that fails is split
  and each part retried individually.
- **Flush is a barrier.** A FLUSH is not dispatched until earlier writes
  complete, and later requests wait for it.
- **Cancellation:** `block_cancel()` removes a request that is still queued
  (-ECANCELED). Requests already dispatched run to completion or timeout.

### Errors, timeouts, recovery
- Failed reads and writes (-EIO / -ETIMEDOUT) are retried up to
  `BLOCK_MAX_RETRIES` (2) while the device is ONLINE or RESETTING. Flushes
  are never retried; their failure is reported.
- **Watchdog.** An event-driven task sleeps while no device has I/O in
  flight. While any device is busy it scans every 10 ticks (every tick for
  polled devices). A request older than `timeout_ticks` (default 5 s)
  first triggers `ops->poll()` to recover a lost interrupt. If the request
  is still outstanding, the watchdog calls `block_reset_device()`.
- **Reset** (`ops->reset`) is serialized system-wide. The device state is
  RESETTING, so there is no dispatch. The driver:
  1. quiesces: in-progress commands get a bounded 20-tick window to finish
     and are harvested;
  2. soft-stops the controller or port;
  3. escalates to a hard reset (AHCI COMRESET, or NVMe disable) when
     commands remain;
  4. fails every other in-flight request with -ETIMEDOUT.

  The block layer then retries those requests. A reset failure puts the
  device in FAILED (all I/O -EIO) or GONE (-ENODEV).
- **States:** ONLINE, RESETTING, FAILED, GONE, and POWERED_OFF (test-only
  simulated power cut).

### Fault injection (tests only; no production path depends on it)
`block_fault_set(device, mode, …)` supports these modes:
- EIO on the Nth matching request, or on an LBA range;
- STALL (lost completion, which drives the timeout path);
- DISAPPEAR (surprise removal);
- POWER_CUT: drop, or tear, every write after the Nth, following a
  seeded policy.

The ramdisk implements the power-cut semantics as if it had a volatile
write cache.

### Metrics
`block_stats_snapshot()` returns:
- counts: reads/writes/flushes, sectors, errors per op;
- merges, retries, timeouts, resets, IRQs, polls;
- per-priority dispatch counts, `aged`, `bg_throttled`, `pool_waits`;
- `max_q` and `max_inflight`;
- average and maximum read/write latency in µs, from a histogram with
  24 buckets.

Every certification run logs these as `block stats` and `block sched`
lines. The kernel timer has **10 ms tick granularity**. Latency values are
tick-resolution measurements, not performance claims.

## 3. Drivers

### AHCI
- Discovery by PCI class 01.06.01. The driver claims the BAR5 ABAR, takes
  ownership via BIOS/OS handoff, and enables AHCI mode. Each port gets a
  1 KiB command list, a 256-byte FIS area and 32 command tables, all in
  identity-safe DMA pages.
- IDENTIFY DEVICE provides capacity (LBA48), model, NCQ support/depth and
  rotation rate (media HDD or SSD).
- I/O uses NCQ (READ/WRITE FPDMA QUEUED) when supported, with up to 32 PRD
  entries per command. Otherwise it uses READ/WRITE DMA EXT.
  FLUSH CACHE EXT is non-queued: it waits for an idle port and blocks new
  submissions.
- Interrupts: MSI when available, otherwise **PARTIAL** (legacy INTx is not
  routed; the device runs in polled mode, which is correct but slower).
- **Error handling.** On TFES, HBFS, IFS or other error bits, the IRQ
  handler parks the port and signals the per-HBA error-handler task. The
  task then:
  1. harvests completed slots;
  2. stops the engine (ST, then FRE) and clears SERR/IS;
  3. escalates to COMRESET if the stop fails or commands were abandoned;
  4. restarts the port and fails the remaining commands with -EIO, which
     the block layer retries.

  PhyRdy or port-connect changes are treated as hot-plug events.
- **Hot-plug:** a device that disappears moves to GONE, and all
  outstanding and future I/O returns -ENODEV. Re-insertion and
  re-enumeration are **PARTIAL** (the port stays offline).
- ATAPI devices and port multipliers are recognized and skipped (**PARTIAL**).

### NVMe
- Discovery by PCI class 01.08.02. Requires CAP.CSS NVM and a supported
  MPS for a 4 KiB page. Setup:
  - admin SQ/CQ with depth 64;
  - IDENTIFY controller and namespace 1 (LBA format, MDTS, VWC);
  - I/O queue pairs: `min(CPUs online at probe time, 4, controller-granted)`,
    but at least 1, each with depth `min(64, CAP.MQES+1)` and one MSI-X
    vector per CQ. At -smp 4 the log shows 3 queues because one CPU is
    still parked by the scheduler's hot-offline test.
  - The block layer exposes a software depth of 32 per device, since its
    pool is at most 64 requests.
- PRP lists use one pre-allocated list page per command ID, so no
  allocation happens on the I/O path. The transfer is capped by MDTS.
- FLUSH is issued only when VWC=1. Otherwise it completes immediately,
  because the device has no volatile cache.
- Reset (`nvme_reset`) runs these steps:
  1. harvest completions;
  2. bounded drain window;
  3. CC.EN=0 and wait for CSTS.RDY=0;
  4. fail the remaining commands;
  5. re-enable and recreate the I/O queues.

  If CSTS reads all-ones, the controller has been removed (GONE).
- `nvme_shutdown_all()` performs the CC.SHN normal-shutdown handshake. It
  is exported for the power-off path. ZEROOS has no power-off path yet
  (**PARTIAL**, see §12), so every power-off is unclean and is recovered by
  the journal. CI exercises this on every boot.
- Only namespace 1 is used (**PARTIAL**: multiple namespaces). Surprise
  removal cannot be simulated in QEMU (PCIe `device_del` is cooperative),
  so the removal path is certified through the block-layer DISAPPEAR
  fault only.

### Legacy IDE
**PARTIAL.** Not implemented. Legacy PATA/IDE controllers (and AHCI in
IDE-compat mode) are reported at discovery and not driven. No success is
faked: such disks simply do not appear as block devices.

## 4. GPT

`gpt.c` implements the validation listed in `gpt.h`:
- header: signature, revision, size, CRC, MyLBA/AlternateLBA, usable
  range;
- entry geometry, array CRC, 64-bit overflow checks;
- entries inside the usable range and non-overlapping.

Every failure has a stable reason code, which appears in the
`gpt <dev>: primary=<reason> backup=<reason> source=…` log line.

Recovery policy:

| Primary | Backup | Result |
|---|---|---|
| valid | valid | primary |
| valid | invalid | primary; backup damage reported |
| invalid | valid | backup, reported as `degraded`; host `gpt.py repair` rewrites the primary |
| invalid | invalid | no partitions exposed |

The kernel never writes a GPT. `gpt.py repair` rebuilds whichever copy is
damaged from the valid one. It refuses to write, with exit 2, when both
copies are invalid.

### Mount policy
- Only partitions with the ZEROOS ZJFS type GUID
  (`8F3D2A10-5A4A-4653-9A2E-5A45524F4F53`) are mounted, and only if the
  ZJFS superblock validates. The kernel **never formats** a disk.
- The first ZJFS partition found in discovery order (AHCI, then NVMe) is
  mounted at `/data`. Further ones go to `/disk1`, `/disk2` and so on.
  This first-found choice is a heuristic: there is no volume label or UUID
  selection yet (**PARTIAL**).
- GPT attribute bit 60 (read-only) makes the partition device read-only,
  and it is mounted read-only.
- The scratch type GUID (`…11`) marks space that the destructive driver
  self-tests may overwrite. Without a scratch partition, the driver tests
  are read-only.
- A filesystem marked ERROR, or one whose journal cannot be replayed
  because the device is read-only, is mounted **read-only** and logged.
- `/ram` is a 24 MiB ZJFS on a RAM disk. It is always present, so
  userspace has a writable filesystem even on diskless boots.

## 5. Page cache

See `pagecache.h` for the full contract. Summary:
- **Bounded pool:** `min(4096 pages, free/4)`, 16 MiB on the test
  configurations. A global LRU with a referenced bit handles eviction.
  Pages that are dirty, under writeback, pinned (mmap) or locked are
  never evicted.
- **Dirty tracking and writeback:**
  - Dirty limit is 25% of the pool. A writer over the limit is throttled
    into synchronous writeback of its own mapping.
  - The flusher writes pages older than `PC_DIRTY_EXPIRE` (300 ticks), or
    above the 10% background threshold, in batches of at most 64 pages,
    at BACKGROUND priority.
  - The flusher sleeps on an event when nothing is dirty.
- **Memory pressure:** `pc_set_limit()` shrinks the pool (evicting
  clean pages and writing back dirty ones). The certified pressure test
  runs with a 32-page limit.
- **Errors:**
  - A failed writeback marks the page ERROR and advances the mapping's
    error sequence.
  - `fsync()` reports the error **once per file description** (each
    description stores the sequence it last saw).
  - The page is retried 3 times. After that, the data is dropped from the
    cache, but the error stays sticky on the mapping. Loss is always
    reported, never silent.
- **fsync:** writes back the inode's dirty pages, then commits the ZJFS
  transaction that contains its metadata, then issues a device FLUSH.
- **Shutdown flush:** unmount writes back all pages, commits, flushes and
  writes the CLEAN superblock. Unmount fails (-EBUSY) while files are open
  and returns the writeback error, if there is one.
- **mmap coherence:** mappings map the page-cache frames themselves, so
  `read()` and `write()` see the same bytes. For the dirtying rule, see
  VFS.md §6.

## 6. Lock order

This section is normative. `ksync.h` and `vfs.h` refer to it.

```
reset_lock (block, mutex)            -- only block_reset_device
vfs namespace mutex                  -- create/unlink/rename/link path walks
 └─ inode->lock (mutex)              -- data I/O, truncate, per-inode metadata
     └─ fs->lock (ZJFS mutex)        -- metadata buffers, allocation, journal
         └─ pc_lock (spinlock, irqsave)
             └─ device->lock / driver port or queue locks (spinlocks, irqsave)
                 └─ vfs_lock (spinlock; refcounts only, never held across blocking)
```

Other rules:
- Drivers complete requests only after dropping their own locks.
- Completion callbacks never take mutexes.
- Page-cache writeback uses the block number cached in each page, so
  writeback never re-enters the filesystem.
- **Unlocked helpers:** `pci_map_bar()` and `vmm_map_mmio_page()` are not
  locked. They are called only from the storage manager task during
  discovery.

## 7. Security

- **Permissions.** Each process has a uid and gid (`process.h`). The VFS
  checks owner/group/other mode bits for read, write and search on every
  path component and file operation. uid 0 bypasses the mode-bit checks
  entirely (ZEROOS has no exec-from-file path yet).
- `chmod` and `chown` require the owner (chmod) or root (chown).
  `SETCRED` is root-only and cannot be used to regain root. The Ring-3
  probe certifies EACCES, EPERM on chmod, owner assignment on create, and
  the inability to regain root.
- **User pointers** pass through `syscall_copy_{from,to}_user`. Paths are
  copied once with a hard bound (no TOCTOU), and transfers are capped at
  1 MiB per call.
- **No secrets in logs.** Storage logs contain device names, LBAs, counts
  and error codes, never file contents. The only file bytes the kernel
  logs are the CRC32C of the host-seeded test file.
- **Integrity:**
  - All ZJFS metadata carries CRC32C bound to its block number or inode
    number, which detects corruption and misdirected writes.
  - The journal commit carries a CRC over all images, which detects torn
    commits.
  - File data is **not** checksummed (**PARTIAL**, documented in ZJFS.md).
- **Encryption** (architecture only, **PARTIAL**; not implemented):
  - Per-volume, key-wrapped encryption between the filesystem and the
    block layer. The unit is a 4 KiB block, AES-XTS with tweak = block
    number.
  - Key boundary: master keys live only in a dedicated key service
    process, and the kernel holds unwrapped volume keys in non-pageable,
    zero-on-free memory.
  - The superblock records only the wrapping metadata, never key material.
  - This layer slots in as a block-device filter (`struct block_device`
    with a parent), so no VFS or ZJFS ABI change is needed.

## 8. Atomicity, snapshots, backup, interrupted updates

**Implemented guarantees:**
- **Metadata** (create, unlink, rename, link, truncate, allocation) is
  atomic by journal transaction. After a crash, each operation is either
  fully present or fully absent.
- `rename()` atomically replaces its target.
- **Ordered data:** newly allocated file blocks are written before the
  metadata that references them commits, so a crash never exposes stale
  data.
- Data is durable **only after fsync** (or msync) returns 0. There are no
  data guarantees without it, and `close()` does not fsync.
- **Interrupted-update recovery:**
  1. write the new file;
  2. `fsync` it;
  3. `rename` it over the old file;
  4. `fsync` the directory.

  After a crash, this yields either the old or the new file, never a torn
  one. The kernel tests (`test_fs.c` crash rounds) and the host checker
  verify this.
- **Unlinked-but-open files** are on the on-disk orphan list and are
  reclaimed at the next mount after a crash.

**Snapshots and rollback** are **PARTIAL** (architecture only):
- The planned design is a copy-on-write snapshot at the ZJFS level. A
  snapshot freezes the current block bitmap as a reference set, and a
  "shared" bit is needed so shared blocks are copied on write.
- Rollback swaps the root pointer inside a single journal transaction.
- **Backup** is done offline with host tools (`zjfs.py cat/ls`) on a
  cleanly unmounted or journal-replayed image.

## 9. Resource contract and background work

- Every queue and table is fixed-size:
  - request pools: at most 64 per device;
  - page cache: at most 4096 pages;
  - ZJFS buffer cache: 256 buffers per filesystem;
  - VFS: 256 inodes, 256 files, 256 dentries, 64 fd tables of 32 fds,
    64 mmaps;
  - journal: one running transaction of at most 128 blocks (format
    maximum 500).

  Exhaustion returns ENOMEM, EMFILE, ENFILE or EAGAIN, or throttles the
  caller. Nothing grows without bound.
- **Event-driven tasks:**

  | Task | Wakes on | Otherwise |
  |---|---|---|
  | block watchdog | a device becoming busy | sleeps |
  | AHCI error handler | error or hot-plug interrupts | sleeps |
  | page-cache flusher | dirty data | sleeps |
  | storage worker | kicks (deferred releases, process-exit cleanup) | sleeps |

  While a filesystem has uncommitted work, the storage worker instead
  polls `vfs_periodic()` every 100 ticks to commit transactions older
  than `ZJ_COMMIT_INTERVAL` (500 ticks). An idle system has no
  storage-driven timer wakeups.
- **Process exit:** `storage_process_exit()` queues descriptor-table and
  mmap release for the worker, and never blocks the reaper.
- **Metrics:**
  - block (§2);
  - page cache: hits, misses, evictions, writebacks, throttles, errors,
    dirty and pinned counts;
  - ZJFS: commits, commit blocks, max transaction, replays, orphans,
    buffer-cache hits/misses/evictions, checksum errors, I/O errors,
    ENOSPC, aborts;
  - VFS: open files, cached inodes.

  CPU and memory impact are bounded by the fixed pools above. The kernel
  has no per-subsystem CPU accounting yet, so no CPU percentage is
  claimed.

## 10. Test and certification suite

All of the following run at boot and are fatal on failure
(`ZEROOS PANIC: storage certification failed: …`):

| Suite | Coverage |
|---|---|
| `test_block.c` | basic rw, merging, HDD C-SCAN order, priority + aging, flush barrier, cancellation, pool backpressure, EIO/timeout/retry, power cut + removal, 4-thread concurrency |
| `test_gpt.c` | 14 corruption cases (signature, header CRC, entry CRC, size, MyLBA, usable range, entry geometry ×3, entry range ×2, overlap, backup-only damage, both damaged) and recovery source |
| `test_fs.c` | basic I/O, large file (double-indirect) and truncate, namespace ops, unlink-while-open, permissions, ENOSPC including metadata ops, memory pressure (32-page cache), concurrency, device I/O errors (abort to RO, ERROR flag, fsck repair), corruption detection (bad metadata CRC, primary SB fallback, no valid SB), unmount + fsck |
| crash rounds (`test_fs.c`) | 8 seeded power cuts at increasing write counts with drop/tear policies during metadata **and** writeback; each is followed by remount (replay) and a full check that must be clean |
| `test_driver.c` (real hardware) | read-only checks on every disk. On disks with a scratch partition: pattern write/verify, queued I/O depth, lost-interrupt poll recovery, reset with 16 requests in flight, partition-bound and read-only enforcement, flush |
| persistence | `/data` boot counter incremented and fsynced; host-seeded file CRC; clean unmount → CLEAN superblock → remount |
| worker | periodic commit fires with no other trigger |
| Ring-3 probe (`userspace/storage/probe.c`) | the file syscall ABI end-to-end from user mode (see VFS.md §7) |

The reset path can be stress-tested with
`make BUILD=build-rst EXTRA_CFLAGS=-DZEROOS_TEST_RESET_REPS=40U`. It was
run six times on q35 with 40 resets per device per boot (480 resets
total) with no lost request.

### Host tools
- `tools/storage/gpt.py create|list|offset|repair`
- `tools/storage/zjfs.py --part N IMAGE mkfs|info|fsck [--repair]|ls|cat|put|mkdir|get-counter`

`zjfs.py fsck` exit codes: 0 clean, 1 repaired, 2 fatal, 3 cannot open.

### CI (`.github/workflows/build.yml`)
- The existing boots (-smp 2 ×3, -smp 4, NX off, fault build) additionally
  require the storage certification lines.
- **Storage host tools self-test:**
  - mkfs/put/cat round trip;
  - superblock corruption with backup fallback and repair;
  - GPT primary damage with byte-exact repair;
  - refusal when both GPT copies are damaged.
- **Storage persistence certification:**
  - q35 with AHCI and NVMe disks, -smp 4, two boots;
  - each boot is ended by killing QEMU, which is an unclean shutdown;
  - after each boot, the host fsck must be clean, the Ring-3 file must be
    readable, and the boot counter must equal the boot number;
  - finally `fsck --repair` replays the journal and the superblock must
    be CLEAN.

## 11. Evidence (this change set)

q35, `-smp 4`, 256 MiB, SATA (`ide-hd` on ICH9 AHCI) and NVMe 64 MiB
images, two consecutive boots:

- `ahci0: version=1.0 … slots=32 ncq=1 s64a=1 irq=msi`
- `nvme0: version=1.4 … io_queues=3 depth=64 mdts_bytes=131072 vwc=1 irq=msix`
- `storage driver sata0: reset with 16 requests in flight (0 queued) -> all completed, data verified`
- `zjfs sata0p1: journal replayed transaction 5 (5 blocks).` (boot 2,
  after an unclean stop)
- `storage persistence: /data boot counter 2 (previous 1).`
- `storage userspace probe reaped; descriptors and mappings released (pinned=0, fdtables=0)`
- `storage Stage 3 certification complete.`
- Diskless boots at -smp 1, 2 and 4, and with NX disabled, reach the same
  final line.

## 12. Known limitations and hardware notes

- **PARTIAL:**
  - Legacy IDE/PATA; AHCI INTx (polled fallback); ATAPI and port
    multipliers.
  - NVMe multi-namespace; hot re-insertion.
  - TRIM/discard; front-merges; readahead.
  - File-data checksums.
  - Snapshots and rollback, encryption (architecture only).
  - Kernel power-off path (so no clean-shutdown flush or NVMe CC.SHN at
    power-off).
  - Mount selection by label/UUID.
- **mmap** stores are made dirty only at `msync`, `munmap` or process exit.
  There is no hardware dirty-bit scan.
- **Timing is tick-granular** (10 ms), including timeouts, aging and
  latency metrics.
- **NVMe surprise removal** is not reproducible in QEMU. The GONE path is
  certified via fault injection.
- **Tested hardware:** QEMU ICH9 AHCI, QEMU NVMe 1.4, and QEMU PIIX (no
  disks). No real-hardware validation has been performed. Controller
  quirks (e.g. AHCI CAP.SNCQ errata, NVMe controllers needing delayed
  CSTS polling) are not handled.
- **QEMU 11.0.2 NVMe:** disabling the controller while QEMU still had
  outstanding block I/O intermittently crashed QEMU. The driver's bounded
  drain before CC.EN=0 avoids that window. The crash is a host emulator
  defect, not guest-visible behavior.
