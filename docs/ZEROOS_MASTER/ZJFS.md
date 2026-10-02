# ZJFS — ZEROOS Journaling File System (format version 1)

ZJFS is the initial ZEROOS disk filesystem. The C definitions in
`kernel/storage/zjfs.h` are normative. `tools/storage/zjfs.py` is an
independent host implementation of the same format (mkfs, fsck/repair, ls,
cat, put, mkdir). CI checks that the two agree: images written by the kernel
are checked by the host, and images made by the host are mounted by the
kernel. Any change to the format must bump `ZJ_VERSION` or a feature bit,
and must update both implementations and this document.

## 1. Design goals and non-goals

**Goals:**
- Atomic, crash-consistent metadata through a physical (block-image)
  redo journal.
- Ordered data, so a crash never exposes stale or uninitialized blocks.
- Every metadata block is self-identifying and checksummed.
- Mount-time replay and orphan recovery.
- A full offline checker with repair.
- Bounded memory: fixed buffer cache and bounded transactions.

**Non-goals (version 1, PARTIAL where noted):**
- File-data checksums (PARTIAL).
- Extents, symlinks, xattrs, quotas.
- Snapshots (architecture in STORAGE.md §8).
- Online fsck, TRIM (PARTIAL).

## 2. Layout

All integers are little-endian. The block size is 4096. Block pointers in
files are u32 (maximum volume 16 TiB); superblock and journal fields are
u64.

```
block 0            superblock (struct zj_super)
1 .. J             journal: block 1 = journal superblock, 2 = descriptor,
                   3 .. 2+n = block images, 3+n = commit record
                   inode bitmap   (ceil(inodes / 32704) blocks)
                   block bitmap   (ceil(blocks / 32704) blocks)
                   inode table    (inodes / 16 blocks, 256-byte inodes)
data_start ..      data, directory and indirect blocks
last block         backup superblock (a copy of block 0, rewritten on clean unmount)
```

mkfs defaults:
- inodes = `max(64, blocks / 4)`;
- journal = `min(1024, max(64, blocks / 32))` blocks.

For example, a 40 MiB partition gives 10240 blocks, 2560 inodes and a
320-block journal, with data starting at block 483.

## 3. Metadata integrity

| Block kind | Identity | Checksum |
|---|---|---|
| superblock, journal SB, descriptor, commit | magic in bytes 0–3 | `crc32c(bytes 0..4091) ^ block_nr` at 4092 |
| bitmap | `ZJBM` at 4088 (4088 bytes = 32704 bits of payload) | as above |
| indirect | owner inode number at 4088; 1022 u32 pointers | as above |
| directory | header: magic `ZJDR` @0, owner ino @4, csum @8 | crc32c over the block with the csum field zeroed, `^ block_nr` |
| inode (256 B) | inode number implied by position | `crc32c(bytes 0..251) ^ ino` at 252 |

XORing in the block or inode number detects misdirected writes and reads,
because a valid block at the wrong address fails verification. A mismatch
is reported (`metadata checksum/identity mismatch at block N`) and handled
as follows:
- on a read-write mount, the filesystem aborts (§6);
- the operation returns EUCLEAN (or EIO);
- fsck counts it as a checksum error.

## 4. Objects

- **Inode (`struct zj_dinode`):**
  - mode, links, uid, gid, flags, size, blocks;
  - atime, mtime, ctime and crtime in nanoseconds;
  - a generation number;
  - 12 direct pointers, one single-indirect and one double-indirect
    pointer (maximum ≈ 4 GiB per file). The triple-indirect field is
    reserved and must be 0;
  - `parent`, which for directories records the parent inode;
  - `orphan_next`.
- Inode 1 is the root and inode 2 is `lost+found`. Inode 0 is invalid.
- **Directory block:**
  - a 16-byte header, then records
    `{u32 ino, u16 rec_len, u8 name_len, u8 type, name…}`;
  - `rec_len` is at least 16 and 8-byte aligned, and the records tile
    the block exactly;
  - `ino == 0` marks free space. Types are 1 (regular) and 2
    (directory). `.` and `..` are implicit, from `parent`, and not stored;
  - names are at most 255 bytes and must not contain `/` or NUL.
- **Superblock state:**
  - `CLEAN` is written only by a clean unmount.
  - `DIRTY` is set at a read-write mount.
  - `ERROR` is sticky: set on abort, cleared only by `fsck --repair`.

  The superblock also records `error_count`, `last_error_code/block/time`,
  `mount_count` and the head of the orphan list.

## 5. Transactions and journaling

Every modifying operation:
1. reserves credits (a bound on the metadata blocks it will touch);
2. pre-checks free blocks and inodes, so ENOSPC is returned **before**
   anything is modified;
3. modifies buffered metadata blocks, which join the single running
   transaction.

The running transaction commits when:
- it would exceed its reserved size. The limit is
  `min(journal − 3, 192, buffer cache / 2)` blocks, which is 128 with the
  256-buffer cache. The descriptor format itself allows up to 500 targets;
- or on `fsync`, `sync` or unmount;
- or after `ZJ_COMMIT_INTERVAL` (500 ticks = 5 s), driven by the storage
  worker.

**Commit protocol (ordered mode):**
1. Write back dirty page-cache data of every inode that allocated blocks
   in this transaction.
2. Write the journal superblock (the sequence), the descriptor (target
   block numbers), the block images, and the commit record. The commit
   record carries `data_crc`, a CRC32C chained over all images.
3. **FLUSH.** The transaction is now durable.
4. Checkpoint: write the images to their home locations, then FLUSH.
5. Advance the sequence.

Blocks freed in the transaction join a pending-free set. They become
allocatable only after step 4, so ordered data writes of the next
transaction can never overwrite a block that the last durable state still
references.

A torn journal write (commit record present but images incomplete) fails
the `data_crc` check and is discarded. Because step 3 had not completed,
that transaction was never acknowledged as durable.

**Replay** happens at mount, and in fsck in either read-only or repair
mode:
- The journal SB, descriptor and commit record must all validate, with
  matching sequence and count.
- Every target must lie inside the volume and outside the journal,
  otherwise replay is refused with EUCLEAN.
- The images must match `data_crc`. If they do, they are copied home,
  followed by a FLUSH.
- Replay is idempotent: a crash during replay simply replays again.

If a transaction needs replay but the device is read-only, the mount stays
read-only and serves the **replayed view from memory**. fsck without
`--repair` reports this state as
`journal transaction N pending … checking the replayed view read-only`.

**Truncate and delete of large files** proceed in chunks of 1024 blocks,
each chunk in its own transaction. `ZJ_IF_TRUNC` and the orphan list make
this resumable after a crash.

**Orphans:** an inode whose link count drops to 0 while it is open, or
that is mid-truncate, goes on the superblock's orphan list within the same
transaction. Mount processes the list, destroying or finishing each
truncate, before the filesystem becomes available.

## 6. Error behavior

| Condition | Behavior |
|---|---|
| ENOSPC or no free inode | Detected before modification, and the operation fails cleanly. Writes return a short count once some bytes are in. Certified: fill to ENOSPC, then create, mkdir and rename all fail with no fsck damage. |
| Device I/O error or checksum mismatch after modification began | **Abort**: the running transaction is discarded, the fs becomes read-only (EROFS for modifications), and ERROR is set in the superblock on a best-effort basis. On-disk state is the last committed transaction. |
| Journal write fails (e.g. power cut) | Abort. The next mount replays up to the last complete commit. |
| Superblock invalid | Mount tries the backup superblock (last block) and logs `using backup`. If neither is valid, the mount fails with EUCLEAN and no auto-format happens. |
| ERROR flag set | Mounted read-only with `run fsck` logged. |
| Unsupported `features_incompat` | Mount refused. |
| Unsupported `features_ro_compat` | Mounted read-only. |
| Device disappears | I/O returns ENODEV, the fs aborts, and unmount reports the error. |

## 7. Check and repair (`zjfs_check`, `zjfs.py fsck`)

fsck performs the following checks:
1. Superblock (primary, then backup) and the journal (read-only replay
   into memory, or real replay with `--repair`).
2. Every inode checksum, every reachable directory, indirect and bitmap
   block checksum, and each block's identity.
3. Directory structure: record tiling, names, types, references to free
   or out-of-range inodes, and directory `parent` consistency.
4. Reachability, with per-inode link counts computed from directory
   references.
5. Block ownership: the file map must not contain duplicates and must
   stay in range.
6. Bitmaps against the computed allocation: leaked and doubly-used
   blocks and inodes.
7. Superblock free counters.

`--repair` fixes the following, and never guesses file contents:
- replays the journal;
- frees leaked blocks and inodes;
- corrects link counts;
- moves unreachable in-use inodes to `lost+found`;
- recomputes the free counters;
- clears ERROR;
- writes a CLEAN superblock and the backup.

Structural damage it cannot resolve safely is reported as **fatal**
(exit 2) and nothing further is written.

Output ends with a summary line like the following, then `RESULT clean`,
`RESULT repaired` or `RESULT fatal`:

```
inodes_used=5 blocks_used=489 checksum=0 structure=0 leaked_blocks=0 leaked_inodes=0 links=0 counters=0 repaired=0 fatal=0 error_flag=0 unclean=1
```

Exit codes: 0 clean, 1 repaired, 2 fatal, 3 cannot open.

`zjfs_check(repair=0)` in the kernel returns EROFS when a journal
transaction is pending, because a read-only check will not modify the
device. The self-tests always check after remount or with `repair=1`.

## 8. Crash testing

`test_fs.c` performs 8 seeded crash rounds on a RAM disk. Each round:
1. Runs a metadata-plus-data workload (create, write, rename, unlink,
   truncate).
2. Cuts power after N device writes, with N ∈ {1, 4, 9, 17, 30, 55, 90,
   140}. The policy either drops every write after the cut or drops and
   tears (partially writes) a seeded subset, hitting journal writes,
   checkpoints and data writeback.
3. Remounts, replaying the journal and processing orphans.
4. Verifies that every fsynced file is intact and that the namespace is
   in a pre- or post-operation state.
5. Unmounts and runs a full check, which must be clean.

CI additionally kills QEMU after each of two boots on real AHCI and NVMe
disks. The host fsck must then be clean, with or without a pending
journal, and the Ring-3 probe's fsynced file must be readable on the host.

## 9. Concurrency

- One mutex per filesystem (`fs->lock`) serializes metadata: the buffer
  cache, allocation and the journal.
- File data I/O runs outside it through the page cache. Only block
  mapping and allocation take it.
- Independent mounts commit, replay and check concurrently. Each has its
  own lock, buffers and scratch space.
- The lock order is given in STORAGE.md §6.
