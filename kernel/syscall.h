#ifndef ZEROOS_SYSCALL_H
#define ZEROOS_SYSCALL_H

#include "types.h"

#define ZEROOS_SYSCALL_VECTOR 128U
#define ZEROOS_SYSCALL_ABI_VERSION 1U
#define ZEROOS_SYSCALL_MAX_TRANSFER 512U

enum zeroos_syscall_id {
    ZEROOS_SYS_ABI_INFO = 0,
    ZEROOS_SYS_EXIT = 1,
    ZEROOS_SYS_WRITE = 2,
    ZEROOS_SYS_GETPID = 3,
    ZEROOS_SYS_GETTID = 4,
    ZEROOS_SYS_YIELD = 5,
    ZEROOS_SYS_IPC_CREATE = 6,
    ZEROOS_SYS_IPC_GRANT = 7,
    ZEROOS_SYS_IPC_CLOSE = 8,
    ZEROOS_SYS_IPC_SEND = 9,
    ZEROOS_SYS_IPC_RECEIVE = 10,
    ZEROOS_SYS_SPAWN = 11,
    ZEROOS_SYS_WAIT = 12,
    ZEROOS_SYS_PIPE_CREATE = 13,
    ZEROOS_SYS_PIPE_WRITE = 14,
    ZEROOS_SYS_PIPE_READ = 15,
    ZEROOS_SYS_EVENT_CREATE = 16,
    ZEROOS_SYS_EVENT_SIGNAL = 17,
    ZEROOS_SYS_EVENT_WAIT = 18,
    ZEROOS_SYS_EVENT_CLOSE = 19,
    ZEROOS_SYS_SHM_CREATE = 20,
    ZEROOS_SYS_SHM_GRANT = 21,
    ZEROOS_SYS_SHM_MAP = 22,
    ZEROOS_SYS_SHM_UNMAP = 23,
    ZEROOS_SYS_SHM_CLOSE = 24,
    ZEROOS_SYS_OPEN = 25,
    ZEROOS_SYS_CLOSE = 26,
    ZEROOS_SYS_READ = 27,
    ZEROOS_SYS_FILE_WRITE = 28,
    ZEROOS_SYS_PREAD = 29,
    ZEROOS_SYS_PWRITE = 30,
    ZEROOS_SYS_SEEK = 31,
    ZEROOS_SYS_FSTAT = 32,
    ZEROOS_SYS_STAT = 33,
    ZEROOS_SYS_READDIR = 34,
    ZEROOS_SYS_MKDIR = 35,
    ZEROOS_SYS_UNLINK = 36,
    ZEROOS_SYS_RMDIR = 37,
    ZEROOS_SYS_RENAME = 38,
    ZEROOS_SYS_LINK = 39,
    ZEROOS_SYS_FSYNC = 40,
    ZEROOS_SYS_FTRUNCATE = 41,
    ZEROOS_SYS_STATFS = 42,
    ZEROOS_SYS_CHMOD = 43,
    ZEROOS_SYS_GETCRED = 44,
    ZEROOS_SYS_SETCRED = 45,
    ZEROOS_SYS_MMAP = 46,
    ZEROOS_SYS_MUNMAP = 47,
    ZEROOS_SYS_MSYNC = 48,
    ZEROOS_SYS_DUP = 49,
    ZEROOS_SYS_CHOWN = 50,
    ZEROOS_SYS_MAX
};

#define ZEROOS_WAIT_FLAG_NONBLOCK (1ULL << 0)
#define ZEROOS_WAIT_VALID_FLAGS ZEROOS_WAIT_FLAG_NONBLOCK

enum zeroos_syscall_error {
    ZEROOS_EPERM = 1,
    ZEROOS_ENOENT = 2,
    ZEROOS_E2BIG = 7,
    ZEROOS_ECHILD = 10,
    ZEROOS_EINTR = 4,
    ZEROOS_EBADF = 9,
    ZEROOS_EAGAIN = 11,
    ZEROOS_ENOMEM = 12,
    ZEROOS_EFAULT = 14,
    ZEROOS_EBUSY = 16,
    ZEROOS_EINVAL = 22,
    ZEROOS_ENOSYS = 38,
    ZEROOS_EOVERFLOW = 75,
    ZEROOS_EPIPE = 32,
    ZEROOS_ETIMEDOUT = 110,
    ZEROOS_EIO = 5,
    ZEROOS_ENXIO = 6,
    ZEROOS_EACCES = 13,
    ZEROOS_EEXIST = 17,
    ZEROOS_EXDEV = 18,
    ZEROOS_ENODEV = 19,
    ZEROOS_ENOTDIR = 20,
    ZEROOS_EISDIR = 21,
    ZEROOS_ENFILE = 23,
    ZEROOS_EMFILE = 24,
    ZEROOS_EFBIG = 27,
    ZEROOS_ENOSPC = 28,
    ZEROOS_ESPIPE = 29,
    ZEROOS_EROFS = 30,
    ZEROOS_EMLINK = 31,
    ZEROOS_ENAMETOOLONG = 36,
    ZEROOS_ENOTEMPTY = 39,
    ZEROOS_ENOTSUP = 95,
    ZEROOS_ESTALE = 116,
    ZEROOS_EUCLEAN = 117,
    ZEROOS_ECANCELED = 125
};

#define ZEROOS_ABI_FEATURE_PROCESS  (1ULL << 0)
#define ZEROOS_ABI_FEATURE_MEMORY   (1ULL << 1)
#define ZEROOS_ABI_FEATURE_IPC      (1ULL << 2)
#define ZEROOS_ABI_FEATURE_INIT     (1ULL << 3)
#define ZEROOS_ABI_FEATURE_PIPE     (1ULL << 4)
#define ZEROOS_ABI_FEATURE_EVENT    (1ULL << 5)
#define ZEROOS_ABI_FEATURE_SHMEM    (1ULL << 6)

/* File/VFS ABI (feature ZEROOS_ABI_FEATURE_FILES, additive to ABI v1).
 * Layouts are fixed; see docs/VFS.md for semantics. */
#define ZEROOS_ABI_FEATURE_FILES   (1ULL << 7)
#define ZEROOS_FILE_MAX_TRANSFER (1024U * 1024U)
#define ZEROOS_PATH_MAX 256U
#define ZEROOS_NAME_MAX 255U
#define ZEROOS_MAX_FDS 32U
#define ZEROOS_MMAP_MAX_PAGES 64U
#define ZEROOS_MMAP_MAX_REGIONS 4U
#define ZEROOS_O_RDONLY 0x0000U
#define ZEROOS_O_WRONLY 0x0001U
#define ZEROOS_O_RDWR 0x0002U
#define ZEROOS_O_CREAT 0x0040U
#define ZEROOS_O_EXCL 0x0080U
#define ZEROOS_O_TRUNC 0x0200U
#define ZEROOS_O_APPEND 0x0400U
#define ZEROOS_O_DIRECTORY 0x10000U
#define ZEROOS_SEEK_SET 0U
#define ZEROOS_SEEK_CUR 1U
#define ZEROOS_SEEK_END 2U
#define ZEROOS_FSYNC_DATAONLY 1U
#define ZEROOS_MMAP_PROT_WRITE 1U
#define ZEROOS_S_IFMT 0xf000U
#define ZEROOS_S_IFREG 0x8000U
#define ZEROOS_S_IFDIR 0x4000U

struct zeroos_stat {
    uint64_t ino;
    uint64_t size;
    uint64_t blocks;
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

struct zeroos_statfs {
    uint64_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    uint32_t flags;
    uint32_t name_max;
};

struct zeroos_dirent {
    uint64_t ino;
    uint32_t type;
    uint32_t name_len;
    char name[256];
};

struct zeroos_syscall_abi_info {
    uint32_t version;
    uint32_t size;
    uint64_t features;
    uint64_t max_transfer;
};

struct interrupt_frame;

/* Dispatches one validated Ring-3 software interrupt. SYS_EXIT does not
 * return: it transfers ownership to the scheduler's normal exit path. */
void syscall_dispatch(struct interrupt_frame *frame);
int syscall_debug_validate(void);
/* Validated user copies for syscall units outside syscall.c. */
int syscall_copy_from_user(void *destination, uint64_t source, uint64_t length);
int syscall_copy_to_user(uint64_t destination, const void *source, uint64_t length);

#endif
