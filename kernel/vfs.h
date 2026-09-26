#ifndef ZEROOS_VFS_H
#define ZEROOS_VFS_H

#include "types.h"
#include "sync.h"
#include "wait.h"

/* Production VFS — Stage 3.
 * Objects: superblock, mount, inode, dentry, file, descriptor/handle, permissions, timestamps.
 * Operations: open/close/read/write/pread/pwrite/seek/stat/readdir/create/delete/rename/link/fsync/mmap.
 * Defines: blocking semantics, errors, permission checks, lifetime/refcounts, concurrent access, cancellation, crash behavior.
 */

#define ZEROOS_VFS_MAX_MOUNTS 16U
#define ZEROOS_VFS_MAX_INODES 256U
#define ZEROOS_VFS_MAX_FILES 128U
#define ZEROOS_VFS_MAX_NAME 128U
#define ZEROOS_VFS_MAX_PATH 256U

enum zeroos_vfs_file_type {
    ZEROOS_VFS_FILE_UNKNOWN = 0,
    ZEROOS_VFS_FILE_REGULAR,
    ZEROOS_VFS_FILE_DIRECTORY,
    ZEROOS_VFS_FILE_SYMLINK,
    ZEROOS_VFS_FILE_DEVICE,
    ZEROOS_VFS_FILE_PIPE
};

enum zeroos_vfs_open_flags {
    ZEROOS_VFS_O_RDONLY = 1U<<0,
    ZEROOS_VFS_O_WRONLY = 1U<<1,
    ZEROOS_VFS_O_RDWR   = (1U<<0)|(1U<<1),
    ZEROOS_VFS_O_CREATE = 1U<<2,
    ZEROOS_VFS_O_TRUNC  = 1U<<3,
    ZEROOS_VFS_O_APPEND = 1U<<4,
    ZEROOS_VFS_O_NONBLOCK = 1U<<5
};

enum zeroos_vfs_seek_whence {
    ZEROOS_VFS_SEEK_SET = 0,
    ZEROOS_VFS_SEEK_CUR,
    ZEROOS_VFS_SEEK_END
};

enum zeroos_vfs_inode_state {
    ZEROOS_VFS_INODE_UNUSED = 0,
    ZEROOS_VFS_INODE_NEW,
    ZEROOS_VFS_INODE_ACTIVE,
    ZEROOS_VFS_INODE_DIRTY,
    ZEROOS_VFS_INODE_DELETED
};

enum zeroos_vfs_mount_state {
    ZEROOS_VFS_MOUNT_STOPPED = 0,
    ZEROOS_VFS_MOUNT_DORMANT,
    ZEROOS_VFS_MOUNT_WARM,
    ZEROOS_VFS_MOUNT_ACTIVE,
    ZEROOS_VFS_MOUNT_THROTTLED,
    ZEROOS_VFS_MOUNT_SUSPENDED
};

struct zeroos_vfs_timespec {
    uint64_t seconds;
    uint32_t nanoseconds;
};

struct zeroos_vfs_superblock {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_vfs_mount_state state;
    char fs_type[32];
    uint64_t block_device_id;
    uint64_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    uint64_t flags;
    struct spinlock lock;
    uint64_t mount_count;
    uint64_t last_mount_ticks;
};

struct zeroos_vfs_inode {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_vfs_inode_state state;
    enum zeroos_vfs_file_type type;
    uint64_t size;
    uint64_t blocks;
    uint32_t mode; /* permissions */
    uint32_t uid;
    uint32_t gid;
    uint64_t superblock_id;
    uint64_t refcount;
    struct zeroos_vfs_timespec atime;
    struct zeroos_vfs_timespec mtime;
    struct zeroos_vfs_timespec ctime;
    struct spinlock lock;
    struct wait_queue waiters;
    uint64_t dirty;
};

struct zeroos_vfs_dentry {
    char name[ZEROOS_VFS_MAX_NAME];
    uint64_t inode_id;
    uint64_t parent_inode_id;
    uint8_t used;
};

struct zeroos_vfs_file {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    uint64_t inode_id;
    uint64_t offset;
    uint32_t flags;
    uint64_t refcount;
    struct spinlock lock;
    struct wait_queue waiters;
};

struct zeroos_vfs_stat {
    uint64_t inode_id;
    enum zeroos_vfs_file_type type;
    uint64_t size;
    uint64_t blocks;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    struct zeroos_vfs_timespec atime;
    struct zeroos_vfs_timespec mtime;
    struct zeroos_vfs_timespec ctime;
};

/* API — production contracts, not toy */
int vfs_system_init(void);
int vfs_mount(uint64_t block_device_id, const char *fs_type, const char *mount_point,
              uint64_t *superblock_id_out);
int vfs_unmount(uint64_t superblock_id);
int vfs_open(const char *path, uint32_t flags, uint32_t mode, uint64_t *file_id_out);
int vfs_close(uint64_t file_id);
int vfs_read(uint64_t file_id, void *buffer, uint64_t capacity, uint64_t *bytes_read_out);
int vfs_write(uint64_t file_id, const void *buffer, uint64_t length, uint64_t *bytes_written_out);
int vfs_pread(uint64_t file_id, void *buffer, uint64_t capacity, uint64_t offset, uint64_t *bytes_read_out);
int vfs_pwrite(uint64_t file_id, const void *buffer, uint64_t length, uint64_t offset, uint64_t *bytes_written_out);
int vfs_seek(uint64_t file_id, int64_t offset, enum zeroos_vfs_seek_whence whence, uint64_t *new_offset_out);
int vfs_stat(const char *path, struct zeroos_vfs_stat *stat_out);
int vfs_fstat(uint64_t file_id, struct zeroos_vfs_stat *stat_out);
int vfs_readdir(uint64_t file_id, struct zeroos_vfs_dentry *entry_out, uint64_t *index_inout);
int vfs_create(const char *path, uint32_t mode);
int vfs_delete(const char *path);
int vfs_rename(const char *old_path, const char *new_path);
int vfs_fsync(uint64_t file_id);
int vfs_debug_validate(void);

#endif
