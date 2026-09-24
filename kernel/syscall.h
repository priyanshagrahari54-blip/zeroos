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
    ZEROOS_SYS_DISPLAY_INFO = 25,
    ZEROOS_SYS_DISPLAY_PRESENT = 26,
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
    ZEROOS_ECANCELED = 125
};

#define ZEROOS_ABI_FEATURE_PROCESS  (1ULL << 0)
#define ZEROOS_ABI_FEATURE_MEMORY   (1ULL << 1)
#define ZEROOS_ABI_FEATURE_IPC      (1ULL << 2)
#define ZEROOS_ABI_FEATURE_INIT     (1ULL << 3)
#define ZEROOS_ABI_FEATURE_PIPE     (1ULL << 4)
#define ZEROOS_ABI_FEATURE_EVENT    (1ULL << 5)
#define ZEROOS_ABI_FEATURE_SHMEM    (1ULL << 6)
#define ZEROOS_ABI_FEATURE_DISPLAY  (1ULL << 7)
#define ZEROOS_ABI_FEATURE_PRESENT  (1ULL << 8)

/* Display geometry: ABI copy of kernel/fb.h (abi_consistency.py gates the
 * struct body against the public header). */
#define ZEROOS_DISPLAY_FORMAT_INDEXED     0U
#define ZEROOS_DISPLAY_FORMAT_RGB565      1U
#define ZEROOS_DISPLAY_FORMAT_RGB888      2U
#define ZEROOS_DISPLAY_FORMAT_XRGB8888    3U
#define ZEROOS_DISPLAY_FORMAT_EGA_TEXT    4U

#define ZEROOS_DISPLAY_FLAG_PRESENT       (1U << 0)
#define ZEROOS_DISPLAY_FLAG_TEXT_FALLBACK (1U << 1)

struct zeroos_display_info {
    uint64_t physical_address;
    uint64_t byte_size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t format;
    uint32_t flags;
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

#endif
