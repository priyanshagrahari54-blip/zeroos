# ZEROOS Stage 3 — Virtual File System

Source: `kernel/storage/vfs.{h,c}` (core) and `kernel/storage/fsyscall.{h,c}`
(system calls and mmap). The public ABI is in
`userspace/include/zeroos/syscall.h`, with freestanding wrappers in
`userspace/include/zeroos/storage.h`. The storage stack beneath is described
in [STORAGE.md](STORAGE.md), and the filesystem in [ZJFS.md](ZJFS.md).

## 1. Objects and ownership

| Object | Bound | Owns / references | Released when |
|---|---|---|---|
| mount entry | 8 (`VFS_MAX_MOUNTS`) | 1 ref on superblock | `vfs_unmount` succeeds |
| `vfs_superblock` | one per mount | fs-private state, 1 ref on the root inode | the last mount ref drops after a successful sync |
| `vfs_inode` | 256 cached (`VFS_MAX_INODES`) | page-cache mapping, fs-private fields | last ref drops → fs `release_inode` (if links==0) or cache eviction |
| dentry | 256 (`VFS_DCACHE_SIZE`) | 1 ref on parent, 1 ref on child | LRU replacement, or invalidation by unlink/rename |
| `vfs_file` (open file description) | 256 (`VFS_MAX_FILES`) | 1 ref on inode, 1 ref on superblock, offset, flags, seen-error sequence | last descriptor / mmap ref drops |
| descriptor table | 64 tables × 32 fds | 1 ref per slot on a `vfs_file` | process exit (deferred to the storage worker) |
| mmap region | 64 system-wide, 4 per process | 1 ref on `vfs_file`, 1 ref + pin per page-cache page | `munmap` or process exit |

Reference-counting rules:
- All reference counts are changed under `vfs_lock`, a leaf spinlock that
  is never held across blocking operations.
- Lookups take a reference before dropping `vfs_lock`.
- An inode is never freed while any file, dentry, mmap region or
  in-progress operation holds it.
- An inode whose link count reaches 0 while it is still open stays usable
  through its descriptors ("unlink while open"). It is destroyed when the
  last reference drops. Until then it is on the on-disk orphan list, so a
  crash still reclaims it at the next mount.

The lock order is normative and specified in STORAGE.md §6.

## 2. Namespace

- Absolute paths only. Paths are limited to 256 bytes (`PATH_MAX`, including
  the NUL) and names to 255 bytes (`NAME_MAX`). A name may contain any byte
  except `/` and NUL.
- The walker resolves `.` and `..` using each directory's recorded
  parent. `..` at a mount root stays at that root: a walk never escapes
  its mount. `.` or `..` as the final component of create, unlink,
  rename or link → EINVAL.
- Every path component requires search (x) permission.
- There are no symbolic links (the format has no symlink type). Hard links
  are allowed only to regular files.
- Mount points: `/data` and `/diskN` for disk ZJFS partitions, and `/ram`.
  The kernel test mounts `/t` and `/c` exist only during the self-tests.
- A mount root cannot be unlinked, removed or renamed (EBUSY).
- Cross-mount `rename` and `link` return EXDEV.

## 3. Credentials and permissions

- Each process has a uid and gid, both inherited from the parent. Boot
  services run as uid 0.
- `vfs_permission()` applies the classic owner, group and other rwx bits.
  uid 0 bypasses them.
- Directory writes (create, unlink, rename) require w+x on the parent
  directory.
- New inodes take the caller's uid and gid and the requested mode, with
  bits beyond 0777 cleared.
- `chmod` requires the owner or root and returns EPERM otherwise.
- `chown` requires root and clears the setuid and setgid bits.
- `SETCRED` requires root. A non-root process can never regain uid 0.

## 4. Timestamps

- Timestamps are nanoseconds since boot, from the kernel tick at 10 ms
  granularity. There is no RTC wall-clock yet.
- `mtime` and `ctime` are set on data change. `ctime` is set on metadata
  change (chmod, chown, link count, rename). `crtime` is set at creation.
- `atime` uses relatime: it is persisted only when older than mtime/ctime,
  so reads do not cause steady metadata writes.

## 5. Operation semantics

General rules:
- Operations may block on mutexes and on I/O. They never busy-wait.
- The caller's thread performs the work. Completion interrupts wake it.
- Errors are negative errno values. Kernel `SE_*` values equal the public
  `ZEROOS_E*` values.
- **Cancellation:** a blocked filesystem call is not interruptible (there
  are no signals yet). A block request that is still queued can be
  cancelled internally. Process exit waits for the thread's in-progress
  syscall to finish, then tears down its descriptors and mappings.
- **Concurrency:**
  - Data I/O on one inode is serialized by `inode->lock`. Different inodes
    proceed in parallel, down to the block layer.
  - Namespace changes are serialized by the namespace mutex.
  - `pread` and `pwrite` never touch the shared offset. `read` and `write`
    update it under the file lock.
  - `O_APPEND` writes are atomic with respect to other appenders on the
    same inode.
- **Crash semantics:**
  - Each namespace operation is atomic: after a crash it is either fully
    present or fully absent.
  - A file's data is durable only after `fsync` (or `msync`) returns 0.
  - Metadata of other operations is durable once any later journal commit
    completes: periodic (5 s), on `fsync`, on `sync`, or on unmount.

| Operation | Semantics and notable errors |
|---|---|
| open | Flags are RDONLY/WRONLY/RDWR plus CREAT, EXCL, TRUNC, APPEND and DIRECTORY; unknown flags → EINVAL. CREAT\|EXCL on an existing name → EEXIST. Missing parent → ENOENT. A non-directory component → ENOTDIR. Opening a directory for writing → EISDIR. TRUNC requires write access. Limits: EMFILE (process fds), ENFILE (system files). |
| close | Drops the descriptor. Never flushes: data remains in the page cache and is written back later. Writeback errors that no descriptor has observed yet remain reportable through other descriptions of the same inode. |
| read / pread | Returns a short count at EOF and 0 at or past EOF. Holes read as zeros. At most 1 MiB per call. |
| write / pwrite | Allocates blocks and extends the size. On a full filesystem it returns a short count if some bytes were written, otherwise ENOSPC. Beyond the maximum file size → EFBIG. On a read-only or aborted filesystem → EROFS. |
| seek | SET, CUR or END. A negative result → EINVAL. Overflow → EOVERFLOW. Seeking past EOF is allowed; writing there creates a hole. |
| fstat / stat | `struct zeroos_stat`: inode, size, 512-byte blocks, mode, links, uid, gid, times, device, block size. |
| readdir | Returns one entry per call (1), or 0 at the end. The cookie is kept in the file offset. Entries added or removed during iteration may or may not appear, but no entry is returned twice and none is skipped unless it was removed. |
| mkdir / rmdir | rmdir of a non-empty directory → ENOTEMPTY. rmdir of a non-directory → ENOTDIR. rmdir of a mount root → EBUSY. |
| unlink | On a directory → EISDIR. Open files stay usable (orphan list). |
| rename | Atomic. Replaces an existing target of the same kind: a file over a directory → EISDIR, a directory over a file → ENOTDIR. A non-empty target directory → ENOTEMPTY. Moving a directory into its own subtree → EINVAL. Across mounts → EXDEV. Renaming a file onto itself is a no-op. |
| link | Regular files only (directories → EPERM). An existing name → EEXIST. Across mounts → EXDEV. |
| fsync | Flags: 0, or FSYNC_DATAONLY; others → EINVAL. Writes back the inode's dirty pages, commits the journal and issues a device FLUSH. Returns a writeback error once per file description (EIO). |
| ftruncate | Needs write access. Growing creates a hole. Shrinking frees blocks in bounded journal chunks and is crash-safe (the TRUNC flag resumes the operation at mount). Shrinking below a currently mmapped page → EBUSY. |
| statfs | Block size, total and free blocks, total and free inodes, flags (RDONLY, ERROR), name_max. |
| chmod / chown | See §3. EROFS on read-only mounts. |
| dup | Lowest free descriptor that shares the same file description. |
| mmap / munmap / msync | See §6. |

**Error states:**
- A device I/O error or metadata checksum failure in the middle of an
  operation **aborts the filesystem**. The running transaction is
  discarded, and every later modification returns EROFS. Reads keep
  working from committed state.
- The superblock is marked ERROR. It is mounted read-only until
  `fsck --repair` clears the flag.

## 6. mmap

- The mapping is shared, backed by the file, and read-only or read/write.
  `PROT_WRITE` requires the descriptor to be open RDWR, otherwise EACCES.
- Arguments:
  - `offset` must be page-aligned and `length` nonzero, at most 64 pages
    → otherwise EINVAL.
  - The range must lie within the page-rounded file size → otherwise
    ENXIO.
  - Directories and other non-regular files → ENODEV.
- A process may hold at most 4 regions (then ENOMEM), out of 64
  system-wide. Regions are placed in fixed slots at
  `USER_BASE + 0x40000000 + slot × 1 MiB`.
- **Eager and pinned:** every page is read into the page cache when the
  mapping is created and pinned, and the user PTEs point at the cache
  frames. `read()`, `write()` and the mapping therefore see the same
  bytes: *coherence is exact in both directions*. Pinned pages are never
  evicted.
- **Dirtying:**
  - Stores through the mapping are marked dirty at `msync`, `munmap` or
    process exit. There is no hardware dirty-bit scan (**PARTIAL**).
  - `msync` also performs `fsync(data)` and returns its error.
  - Data written through a mapping and never msynced reaches the disk at
    munmap/exit writeback, not at an earlier periodic flush.
- `munmap(addr)` takes the exact start address of a region; the whole
  region is unmapped (EINVAL otherwise).

## 7. System call interface

These calls are additive to syscall ABI v1 and advertised by
`ZEROOS_ABI_FEATURE_FILES` (bit 7) in `ABI_INFO`.
- Convention: `int 0x80`, `rax` = number, arguments in
  `rdi, rsi, rdx, r10, r8, r9`, negative return = `-errno`.
- Every pointer is validated by `syscall_copy_{from,to}_user`. A bad
  pointer → EFAULT, with no partial side effects for path arguments.

| Nr | Call | Arguments → result |
|---:|---|---|
| 25 | `OPEN` | path, flags, mode → fd |
| 26 | `CLOSE` | fd → 0 |
| 27 | `READ` | fd, buf, len → bytes |
| 28 | `FILE_WRITE` | fd, buf, len → bytes (distinct from the v1 diagnostic `WRITE`) |
| 29 | `PREAD` | fd, buf, len, offset (r10) → bytes |
| 30 | `PWRITE` | fd, buf, len, offset (r10) → bytes |
| 31 | `SEEK` | fd, offset, whence → new offset |
| 32 | `FSTAT` | fd, `struct zeroos_stat *` → 0 |
| 33 | `STAT` | path, `struct zeroos_stat *` → 0 |
| 34 | `READDIR` | fd, `struct zeroos_dirent *` → 1 entry / 0 end |
| 35 | `MKDIR` | path, mode → 0 |
| 36 | `UNLINK` | path → 0 |
| 37 | `RMDIR` | path → 0 |
| 38 | `RENAME` | old, new → 0 |
| 39 | `LINK` | existing, new → 0 |
| 40 | `FSYNC` | fd, flags (`FSYNC_DATAONLY`=1) → 0 |
| 41 | `FTRUNCATE` | fd, size → 0 |
| 42 | `STATFS` | path, `struct zeroos_statfs *` → 0 |
| 43 | `CHMOD` | path, mode → 0 |
| 44 | `GETCRED` | → `uid \| (gid << 32)` |
| 45 | `SETCRED` | uid, gid → 0 (root only) |
| 46 | `MMAP` | fd, offset, length, prot → address |
| 47 | `MUNMAP` | address → 0 |
| 48 | `MSYNC` | address → 0 |
| 49 | `DUP` | fd → new fd |
| 50 | `CHOWN` | path, uid, gid → 0 (root only) |

- The file calls end at `CHOWN` = 50; `DISPLAY_INFO` (Stage 5) is 51 and
  `ZEROOS_SYS_MAX` is 52. Descriptors are per process and are not
  inherited by `SPAWN`.
- Limits: `ZEROOS_FILE_MAX_TRANSFER` = 1 MiB per call (larger requests
  return a short count), `ZEROOS_MAX_FDS` = 32, `ZEROOS_MMAP_MAX_PAGES` =
  64, `ZEROOS_MMAP_MAX_REGIONS` = 4.
- **New errnos:** EIO, EACCES, EEXIST, EFBIG, EISDIR, EMFILE, EMLINK,
  ENAMETOOLONG, ENFILE, ENODEV, ENOSPC, ENOTDIR, ENOTEMPTY, ENOTSUP, ENXIO,
  EROFS, ESPIPE, ESTALE, EUCLEAN, EXDEV. The v1 values (EBADF, ENOENT,
  EPERM, EBUSY, …) are reused unchanged. Values are in `syscall.h` and
  identical to the kernel `SE_*` values.
- **Structures** (`zeroos_stat`, `zeroos_statfs`, `zeroos_dirent` with
  `name[256]`) have fixed layouts. They are `_Static_assert`ed against the
  kernel structures and checked by `userspace/tests/abi_consistency.py`.

### ABI migration notes (R11)
- **Additive:** no existing v1 call, number, structure or errno changed,
  and `ZEROOS_SYSCALL_ABI_VERSION` stays 1.
- **Detection:** before using any call in 25–50, a program must check
  `ZEROOS_ABI_FEATURE_FILES` in `ABI_INFO.features`. Kernels without the
  bit return ENOSYS for those numbers.
- `SYS_MAX` moved from 25 to 52 (file calls 25–50, `DISPLAY_INFO` 51 with
  feature bit 8). Code that used `ZEROOS_SYS_MAX` as an
  array bound must be rebuilt against the new header.
- Future changes to these structures will add sized, versioned
  successors (new syscall numbers), never change existing layouts.

### Certification: Ring-3 storage probe
`userspace/storage/probe.c` is linked into the kernel image as a static
ELF. The storage manager launches it as uid 0 after certification. It
exits deliberately with one descriptor and one mapping still open, so
that process-exit cleanup is tested too. It covers:
- create, write, read, pread, pwrite, seek, fstat and fsync of
  `/data/user-probe.txt`, or `/ram/…` on diskless boots;
- ENOENT, EEXIST, EBADF, EFAULT and ENOTDIR;
- rename, link (link count 2), unlink and readdir;
- mmap coherence in both directions, msync, truncate-while-mapped EBUSY,
  munmap, and mmap EINVAL/ENXIO;
- statfs;
- EACCES, EPERM on chmod, creation in a 0777 directory as uid 1000 with
  correct owner, and the inability to regain root.

The kernel then requires:
- exit status 0;
- after reaping, no fd table left for the PID and zero pinned
  page-cache pages.

The host re-reads the file from the disk image after QEMU is killed.
