#ifndef ZEROOS_USERSPACE_SYSCALL_H
#define ZEROOS_USERSPACE_SYSCALL_H

/* Public Ring-3 syscall surface. The header intentionally uses fixed-width
 * C types so it can be consumed by a freestanding runtime without importing
 * kernel-private headers. */
#include <stdint.h>

#define ZEROOS_SYSCALL_VECTOR 128U
#define ZEROOS_SYSCALL_ABI_VERSION 1U
#define ZEROOS_SYSCALL_MAX_TRANSFER 512U
#define ZEROOS_IPC_MAX_MESSAGE 256U

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
    ZEROOS_SYS_MAX
};

#define ZEROOS_WAIT_VALID_FLAGS ZEROOS_WAIT_FLAG_NONBLOCK
#define ZEROOS_IPC_VALID_FLAGS (ZEROOS_IPC_FLAG_NONBLOCK | ZEROOS_IPC_FLAG_PEEK)
#define ZEROOS_PAGE_SIZE 4096U
#define ZEROOS_EXEC_MAX_IMAGE (64U * 1024U)
#define ZEROOS_EXEC_MAX_ARGUMENTS 16U
#define ZEROOS_EXEC_MAX_STRING 128U

enum zeroos_error {
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

#define ZEROOS_WAIT_FLAG_NONBLOCK (1ULL << 0)
#define ZEROOS_IPC_FLAG_NONBLOCK (1ULL << 0)
#define ZEROOS_IPC_FLAG_PEEK     (1ULL << 1)
#define ZEROOS_IPC_RIGHT_SEND    (1U << 0)
#define ZEROOS_IPC_RIGHT_RECV    (1U << 1)
#define ZEROOS_IPC_RIGHT_GRANT   (1U << 2)
#define ZEROOS_IPC_RIGHT_CLOSE   (1U << 3)
#define ZEROOS_IPC_ALL_RIGHTS    (ZEROOS_IPC_RIGHT_SEND | \
                                  ZEROOS_IPC_RIGHT_RECV | \
                                  ZEROOS_IPC_RIGHT_GRANT | \
                                  ZEROOS_IPC_RIGHT_CLOSE)

#define ZEROOS_SHMEM_RIGHT_MAP   (1U << 0)
#define ZEROOS_SHMEM_RIGHT_WRITE (1U << 1)
#define ZEROOS_SHMEM_RIGHT_GRANT (1U << 2)
#define ZEROOS_SHMEM_RIGHT_CLOSE (1U << 3)
#define ZEROOS_SHMEM_ALL_RIGHTS  (ZEROOS_SHMEM_RIGHT_MAP | \
                                  ZEROOS_SHMEM_RIGHT_WRITE | \
                                  ZEROOS_SHMEM_RIGHT_GRANT | \
                                  ZEROOS_SHMEM_RIGHT_CLOSE)
#define ZEROOS_SHMEM_MAP_WRITE   (1ULL << 0)

#define ZEROOS_ABI_FEATURE_PROCESS (1ULL << 0)
#define ZEROOS_ABI_FEATURE_MEMORY  (1ULL << 1)
#define ZEROOS_ABI_FEATURE_IPC     (1ULL << 2)
#define ZEROOS_ABI_FEATURE_INIT    (1ULL << 3)
#define ZEROOS_ABI_FEATURE_PIPE    (1ULL << 4)
#define ZEROOS_ABI_FEATURE_EVENT   (1ULL << 5)
#define ZEROOS_ABI_FEATURE_SHMEM   (1ULL << 6)

typedef uint64_t zeroos_handle_t;
typedef zeroos_handle_t zeroos_ipc_handle_t;
typedef zeroos_handle_t zeroos_shmem_handle_t;

struct zeroos_abi_info {
    uint32_t version;
    uint32_t size;
    uint64_t features;
    uint64_t max_transfer;
};

struct zeroos_ipc_pair {
    zeroos_ipc_handle_t local;
    zeroos_ipc_handle_t peer;
};

static inline uint64_t zeroos_syscall6(uint64_t number,
                                      uint64_t arg0, uint64_t arg1,
                                      uint64_t arg2, uint64_t arg3,
                                      uint64_t arg4, uint64_t arg5) {
    register uint64_t r10 __asm__("r10")=arg3;
    register uint64_t r8 __asm__("r8")=arg4;
    register uint64_t r9 __asm__("r9")=arg5;
    __asm__ volatile ("int $0x80"
                      : "+a"(number)
                      : "D"(arg0), "S"(arg1), "d"(arg2),
                        "r"(r10), "r"(r8), "r"(r9)
                      : "rcx", "r11", "memory");
    return number;
}

static inline int64_t zeroos_syscall_result(uint64_t result) {
    return (int64_t)result;
}

static inline int64_t zeroos_abi_info(struct zeroos_abi_info *info) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_ABI_INFO,(uint64_t)(uintptr_t)info,sizeof(*info),0,0,0,0));
}

static inline int64_t zeroos_exit(uint64_t status) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_EXIT,status,0,0,0,0,0));
}

static inline int64_t zeroos_write(uint64_t fd, const void *buffer,
                                   uint64_t length) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_WRITE,fd,(uint64_t)(uintptr_t)buffer,length,0,0,0));
}

static inline int64_t zeroos_getpid(void) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_GETPID,0,0,0,0,0,0));
}

static inline int64_t zeroos_gettid(void) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_GETTID,0,0,0,0,0,0));
}

static inline int64_t zeroos_yield(void) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_YIELD,0,0,0,0,0,0));
}

static inline int64_t zeroos_ipc_create(struct zeroos_ipc_pair *pair) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_IPC_CREATE,(uint64_t)(uintptr_t)pair,sizeof(*pair),0,0,0,0));
}

static inline int64_t zeroos_ipc_grant(zeroos_ipc_handle_t source,
                                       uint64_t target_pid,
                                       uint8_t rights,
                                       zeroos_ipc_handle_t *target) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_IPC_GRANT,source,target_pid,(uint64_t)(uintptr_t)target,
        rights,0,0));
}

static inline int64_t zeroos_ipc_close(zeroos_ipc_handle_t handle) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_IPC_CLOSE,handle,0,0,0,0,0));
}

static inline int64_t zeroos_ipc_send(zeroos_ipc_handle_t handle,
                                      const void *data, uint64_t length,
                                      uint64_t flags, uint64_t timeout) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_IPC_SEND,handle,(uint64_t)(uintptr_t)data,length,flags,0,
        timeout));
}

static inline int64_t zeroos_ipc_receive(zeroos_ipc_handle_t handle,
                                         void *data, uint64_t capacity,
                                         uint64_t flags, uint64_t *length,
                                         uint64_t timeout) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_IPC_RECEIVE,handle,(uint64_t)(uintptr_t)data,capacity,
        flags,(uint64_t)(uintptr_t)length,timeout));
}

static inline int64_t zeroos_spawn(const void *image, uint64_t image_size,
                                   const uint64_t *argv, uint64_t argc,
                                   const uint64_t *envp, uint64_t envc) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_SPAWN,(uint64_t)(uintptr_t)image,image_size,
        (uint64_t)(uintptr_t)argv,argc,(uint64_t)(uintptr_t)envp,envc));
}

static inline int64_t zeroos_wait(uint64_t pid, uint64_t *status,
                                  uint64_t flags, uint64_t timeout) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_WAIT,pid,(uint64_t)(uintptr_t)status,flags,timeout,0,0));
}

static inline int64_t zeroos_pipe_create(struct zeroos_ipc_pair *pair) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_PIPE_CREATE,(uint64_t)(uintptr_t)pair,sizeof(*pair),0,0,0,0));
}

static inline int64_t zeroos_pipe_write(zeroos_ipc_handle_t handle,
                                        const void *data, uint64_t length,
                                        uint64_t flags, uint64_t timeout) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_PIPE_WRITE,handle,(uint64_t)(uintptr_t)data,length,flags,0,
        timeout));
}

static inline int64_t zeroos_pipe_read(zeroos_ipc_handle_t handle,
                                       void *data, uint64_t capacity,
                                       uint64_t flags, uint64_t *length,
                                       uint64_t timeout) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_PIPE_READ,handle,(uint64_t)(uintptr_t)data,capacity,flags,
        (uint64_t)(uintptr_t)length,timeout));
}

static inline int64_t zeroos_event_create(struct zeroos_ipc_pair *pair) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_EVENT_CREATE,(uint64_t)(uintptr_t)pair,sizeof(*pair),0,0,0,0));
}

static inline int64_t zeroos_event_signal(zeroos_handle_t handle,
                                          uint64_t flags) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_EVENT_SIGNAL,handle,0,0,flags,0,0));
}

static inline int64_t zeroos_event_wait(zeroos_handle_t handle,
                                        uint64_t flags, uint64_t timeout) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_EVENT_WAIT,handle,0,0,flags,0,timeout));
}

static inline int64_t zeroos_event_close(zeroos_handle_t handle) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_EVENT_CLOSE,handle,0,0,0,0,0));
}

static inline int64_t zeroos_shmem_create(uint64_t size, uint64_t flags,
                                          zeroos_shmem_handle_t *handle) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_SHM_CREATE,size,flags,(uint64_t)(uintptr_t)handle,0,0,0));
}

static inline int64_t zeroos_shmem_grant(zeroos_shmem_handle_t source,
                                         uint64_t target_pid, uint8_t rights,
                                         zeroos_shmem_handle_t *target) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_SHM_GRANT,source,target_pid,(uint64_t)(uintptr_t)target,
        rights,0,0));
}

static inline int64_t zeroos_shmem_map(zeroos_shmem_handle_t handle,
                                       uintptr_t address, uint64_t flags) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_SHM_MAP,handle,address,flags,0,0,0));
}

static inline int64_t zeroos_shmem_unmap(zeroos_shmem_handle_t handle,
                                         uintptr_t address) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_SHM_UNMAP,handle,address,0,0,0,0));
}

static inline int64_t zeroos_shmem_close(zeroos_shmem_handle_t handle) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_SHM_CLOSE,handle,0,0,0,0,0));
}

#endif
