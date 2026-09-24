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
    ZEROOS_SYS_MAX
};

enum zeroos_syscall_error {
    ZEROOS_EPERM = 1,
    ZEROOS_ENOENT = 2,
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
    ZEROOS_ECANCELED = 125
};

#define ZEROOS_ABI_FEATURE_PROCESS  (1ULL << 0)
#define ZEROOS_ABI_FEATURE_MEMORY   (1ULL << 1)
#define ZEROOS_ABI_FEATURE_IPC      (1ULL << 2)
#define ZEROOS_ABI_FEATURE_INIT     (1ULL << 3)

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
