#ifndef ZEROOS_VFS_H
#define ZEROOS_VFS_H
#include "../ksync.h"
#include "block.h"
#include "pagecache.h"

/*
 * Virtual filesystem layer. Ownership (docs/VFS.md):
 *
 *   mount table entry --1 ref--> vfs_superblock --owns--> fs private state
 *   vfs_superblock    --1 ref--> root vfs_inode
 *   dentry cache      --1 ref--> parent inode and child inode per entry
 *   vfs_file          --1 ref--> vfs_inode, --1 ref--> vfs_superblock
 *   fd table slot     --1 ref--> vfs_file
 *   mmap region       --1 ref--> vfs_file  (+ page-cache pins)
 *
 * Every reference is counted under vfs_lock (irqsave spinlock, a leaf lock
 * below all mutexes). Inodes are cached (bounded, VFS_MAX_INODES) and are
 * released through the filesystem when the last reference drops; an inode
 * with links==0 is destroyed at that point (orphan processing guarantees
 * the same after a crash). A superblock cannot be unmounted while it has
 * open files, cached busy inodes, or dirty data that fails to write back.
 *
 * Lock order (outer to inner):
 *   vfs namespace mutex (rename/unlink/create path walks)
 *   -> inode->lock (data I/O, truncate, per-inode metadata)
 *   -> fs->lock (ZJFS metadata, journal)
 *   -> page-cache pc_lock / block device locks (spinlocks)
 *   -> vfs_lock (refcounts; never held across blocking calls)
 */

#define VFS_NAME_MAX 255U
#define VFS_PATH_MAX 256U
#define VFS_MAX_MOUNTS 8U
#define VFS_MAX_INODES 256U
#define VFS_MAX_FILES 256U
#define VFS_MAX_FDS 32U
#define VFS_MAX_FDTABLES 64U
#define VFS_DCACHE_SIZE 256U
#define VFS_MAX_MMAPS 64U

#define VFS_S_IFMT  0xf000U
#define VFS_S_IFREG 0x8000U
#define VFS_S_IFDIR 0x4000U
#define VFS_S_ISREG(m) (((m) & VFS_S_IFMT) == VFS_S_IFREG)
#define VFS_S_ISDIR(m) (((m) & VFS_S_IFMT) == VFS_S_IFDIR)

/* Open flags (ABI: identical values in userspace/include/zeroos/syscall.h). */
#define VFS_O_RDONLY   0x0000U
#define VFS_O_WRONLY   0x0001U
#define VFS_O_RDWR     0x0002U
#define VFS_O_ACCMODE  0x0003U
#define VFS_O_CREAT    0x0040U
#define VFS_O_EXCL     0x0080U
#define VFS_O_TRUNC    0x0200U
#define VFS_O_APPEND   0x0400U
#define VFS_O_DIRECTORY 0x10000U
#define VFS_O_VALID (VFS_O_ACCMODE | VFS_O_CREAT | VFS_O_EXCL | VFS_O_TRUNC | \
                     VFS_O_APPEND | VFS_O_DIRECTORY)

#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

/* Access checks. */
#define VFS_MAY_READ  4U
#define VFS_MAY_WRITE 2U
#define VFS_MAY_EXEC  1U

/* Superblock flags. */
#define VFS_SB_RDONLY 0x1U
#define VFS_SB_ERROR  0x2U     /* filesystem aborted: read-only until remount */
#define VFS_SB_DYING  0x4U     /* unmount in progress */

/* Inode flags (in memory). */
#define VFS_I_VALID   0x1U
#define VFS_I_DEAD    0x2U     /* destroyed on disk; must not be looked up */

struct vfs_superblock;
struct vfs_inode;
struct vfs_file;

struct vfs_stat {
    uint64_t ino;
    uint64_t size;
    uint64_t blocks;           /* 512-byte units, POSIX st_blocks */
    uint32_t mode;
    uint32_t links;
    uint32_t uid;
    uint32_t gid;
    uint64_t atime_ns;
    uint64_t mtime_ns;
    uint64_t ctime_ns;
    uint32_t dev;
    uint32_t block_size;
};

struct vfs_dirent {
    uint64_t ino;
    uint32_t type;             /* VFS_S_IFREG / VFS_S_IFDIR >> 12 */
    uint32_t name_len;
    char name[VFS_NAME_MAX + 1U];
};

struct vfs_statfs {
    uint64_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    uint32_t flags;
    uint32_t name_max;
};

struct vfs_fs_ops {
    int (*lookup)(struct vfs_inode *dir, const char *name, uint32_t length,
                  uint64_t *ino_out);
    int (*read_inode)(struct vfs_superblock *sb, uint64_t ino,
                      struct vfs_inode *inode);
    int (*create)(struct vfs_inode *dir, const char *name, uint32_t length,
                  uint32_t mode, uint32_t uid, uint32_t gid, uint64_t *ino_out);
    int (*unlink)(struct vfs_inode *dir, const char *name, uint32_t length,
                  struct vfs_inode *victim, int directory);
    int (*link)(struct vfs_inode *dir, const char *name, uint32_t length,
                struct vfs_inode *inode);
    int (*rename)(struct vfs_inode *old_dir, const char *old_name, uint32_t old_len,
                  struct vfs_inode *new_dir, const char *new_name, uint32_t new_len,
                  struct vfs_inode *moved, struct vfs_inode *replaced);
    int (*readdir)(struct vfs_inode *dir, uint64_t *cookie, struct vfs_dirent *out);
    /* Map file page `index`; create=1 allocates (zeroed, dirty in the page
     * cache before the allocation can commit). *new_out=1 if allocated. */
    int (*map_page)(struct vfs_inode *inode, uint64_t index, int create,
                    uint64_t *block_out, int *new_out);
    int (*set_size)(struct vfs_inode *inode, uint64_t size);
    int (*update_inode)(struct vfs_inode *inode);   /* times/mode/owner */
    void (*release_inode)(struct vfs_inode *inode); /* last ref, links==0 */
    void (*evict_inode)(struct vfs_inode *inode);   /* cache eviction */
    int (*fsync)(struct vfs_inode *inode, int data_only);
    int (*sync)(struct vfs_superblock *sb);
    int (*statfs)(struct vfs_superblock *sb, struct vfs_statfs *out);
    int (*unmount)(struct vfs_superblock *sb);
};

struct vfs_superblock {
    const struct vfs_fs_ops *ops;
    void *fs;
    struct block_device *device;
    struct vfs_inode *root;
    uint32_t refcount;
    uint32_t flags;
    uint32_t dev_id;
    uint32_t in_use;
    char fs_name[8];
};

struct vfs_inode {
    struct vfs_superblock *sb;
    uint64_t ino;
    uint32_t refcount;
    uint32_t flags;
    uint32_t mode;
    uint32_t links;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint64_t blocks;           /* filesystem blocks allocated */
    uint64_t atime_ns;
    uint64_t mtime_ns;
    uint64_t ctime_ns;
    uint64_t crtime_ns;
    uint32_t mmap_count;
    uint32_t generation;
    struct kmutex lock;
    struct pc_mapping mapping;
    struct vfs_inode *hash_next;
    uint64_t lru_tick;
    /* Filesystem private state (ZJFS: block map, parent, orphan link). */
    uint32_t fs_direct[12];
    uint32_t fs_indirect;
    uint32_t fs_dindirect;
    uint32_t fs_parent;
    uint32_t fs_flags;
    uint32_t fs_orphan_next;
    uint32_t fs_in_txn;
};

struct vfs_file {
    struct vfs_inode *inode;
    uint32_t refcount;
    uint32_t flags;            /* VFS_O_* */
    uint64_t offset;
    uint32_t wb_error_seen;    /* mapping error sequence sampled */
    uint32_t in_use;
    struct kmutex pos_lock;    /* offset updates (read/write/seek) */
    uint64_t dir_cookie;
};

struct vfs_metrics {
    uint64_t opens;
    uint64_t closes;
    uint64_t reads;
    uint64_t writes;
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t fsyncs;
    uint64_t fsync_errors;
    uint64_t lookups;
    uint64_t dcache_hits;
    uint64_t dcache_misses;
    uint64_t inode_cache_hits;
    uint64_t inode_cache_misses;
    uint64_t inodes_cached;
    uint64_t files_open;
    uint64_t permission_denials;
    uint64_t mmaps;
};

/* Credentials used for permission checks. */
struct vfs_cred {
    uint32_t uid;
    uint32_t gid;
};

int vfs_init(void);
/* Mount `device` (a partition or disk) at "/<name>" or "/" (name == 0).
 * Only filesystems whose signature validates are mounted; nothing is ever
 * formatted implicitly. */
int vfs_mount(struct block_device *device, const char *path, uint32_t flags);
int vfs_unmount(const char *path, int force);
int vfs_unmount_all(void);
int vfs_sync_all(void);

/* Path-based operations (absolute paths). */
int vfs_open(const struct vfs_cred *cred, const char *path, uint32_t flags,
             uint32_t mode, struct vfs_file **out);
int vfs_mkdir(const struct vfs_cred *cred, const char *path, uint32_t mode);
int vfs_unlink(const struct vfs_cred *cred, const char *path);
int vfs_rmdir(const struct vfs_cred *cred, const char *path);
int vfs_rename(const struct vfs_cred *cred, const char *old_path,
               const char *new_path);
int vfs_link(const struct vfs_cred *cred, const char *old_path,
             const char *new_path);
int vfs_stat(const struct vfs_cred *cred, const char *path, struct vfs_stat *out);
int vfs_statfs(const char *path, struct vfs_statfs *out);
int vfs_chmod(const struct vfs_cred *cred, const char *path, uint32_t mode);
int vfs_chown(const struct vfs_cred *cred, const char *path, uint32_t uid,
              uint32_t gid);

/* File-description operations. Buffers are kernel addresses; the syscall
 * layer copies to/from user memory in bounded chunks. */
int64_t vfs_read(struct vfs_file *file, void *buffer, uint64_t length);
int64_t vfs_write(struct vfs_file *file, const void *buffer, uint64_t length);
int64_t vfs_pread(struct vfs_file *file, void *buffer, uint64_t length,
                  uint64_t offset);
int64_t vfs_pwrite(struct vfs_file *file, const void *buffer, uint64_t length,
                   uint64_t offset);
int64_t vfs_seek(struct vfs_file *file, int64_t offset, int whence);
int vfs_fstat(struct vfs_file *file, struct vfs_stat *out);
int vfs_fsync(struct vfs_file *file, int data_only);
int vfs_ftruncate(struct vfs_file *file, uint64_t size);
int vfs_readdir(struct vfs_file *file, struct vfs_dirent *out);
void vfs_file_get(struct vfs_file *file);
void vfs_file_put(struct vfs_file *file);

/* Inode helpers for filesystems / mmap. */
void vfs_inode_get(struct vfs_inode *inode);
void vfs_inode_put(struct vfs_inode *inode);
/* Cached inode for (sb, ino) or 0; +1 reference when found. Never loads. */
struct vfs_inode *vfs_inode_cached(struct vfs_superblock *sb, uint64_t ino);
int vfs_permission(const struct vfs_cred *cred, struct vfs_inode *inode,
                   uint32_t mask);
uint64_t vfs_now_ns(void);

void vfs_metrics_snapshot(struct vfs_metrics *out);

/* Per-process descriptor tables (keyed by pid + generation). */
struct vfs_fdtable;
struct vfs_fdtable *vfs_fdtable_lookup(uint64_t pid, uint32_t generation, int create);
int vfs_fd_install(struct vfs_fdtable *table, struct vfs_file *file);
struct vfs_file *vfs_fd_get(struct vfs_fdtable *table, int fd);   /* +1 ref */
int vfs_fd_close(struct vfs_fdtable *table, int fd);
int vfs_fd_dup(struct vfs_fdtable *table, int fd);
/* Called from process teardown (never blocks: defers closing to the
 * storage worker). */
void vfs_process_exit(uint64_t pid, uint32_t generation);
void vfs_deferred_work(void);
uint32_t vfs_fdtable_count(void);
void vfs_set_worker_event(struct kcompletion *event);
/* Periodic filesystem work (journal commit interval). Returns 1 while
 * any mounted filesystem has an open transaction. */
int vfs_periodic(void);
int vfs_sb_check_writable(struct vfs_superblock *sb);
void vfs_kick_worker(void);
void vfs_inode_mmap_get(struct vfs_inode *inode);
void vfs_inode_mmap_put(struct vfs_inode *inode);

#endif
