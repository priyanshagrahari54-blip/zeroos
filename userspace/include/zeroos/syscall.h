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
#define ZEROOS_IPC_PIPE_CAPACITY (ZEROOS_IPC_MAX_MESSAGE * 8U)

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
    ZEROOS_SYS_DISPLAY_INFO = 51,
    ZEROOS_SYS_DISPLAY_PRESENT = 52,
    ZEROOS_SYS_INPUT_POLL = 53,
    ZEROOS_SYS_INPUT_WAIT = 54,
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
#define ZEROOS_ABI_FEATURE_DISPLAY (1ULL << 8)
#define ZEROOS_ABI_FEATURE_PRESENT (1ULL << 9)
#define ZEROOS_ABI_FEATURE_INPUT (1ULL << 10)

/* Display geometry (ABI): must match kernel/fb.h and kernel/syscall.h. */
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

/* Input event kinds; values mirror kernel/input_core.h enum input_kind
 * (gated by abi_consistency.py). */
enum zeroos_input_kind {
    ZEROOS_INPUT_KIND_KEY = 1,
    ZEROOS_INPUT_KIND_POINTER = 2,
    ZEROOS_INPUT_KIND_TOUCH = 3,
    ZEROOS_INPUT_KIND_DEVICE_GONE = 4
};

/* Key codes: printable keys carry their ASCII value; non-printable keys
 * live above 0xFF. Values are gate-checked between headers. */
enum zeroos_keycode {
    ZEROOS_KEY_NONE = 0,
    ZEROOS_KEY_ESCAPE = 256,
    ZEROOS_KEY_ENTER = 257,
    ZEROOS_KEY_BACKSPACE = 258,
    ZEROOS_KEY_TAB = 259,
    ZEROOS_KEY_UP = 272,
    ZEROOS_KEY_DOWN = 273,
    ZEROOS_KEY_LEFT = 274,
    ZEROOS_KEY_RIGHT = 275,
    ZEROOS_KEY_HOME = 276,
    ZEROOS_KEY_END = 277,
    ZEROOS_KEY_PAGE_UP = 278,
    ZEROOS_KEY_PAGE_DOWN = 279,
    ZEROOS_KEY_INSERT = 280,
    ZEROOS_KEY_DELETE = 281,
    ZEROOS_KEY_F1 = 288,
    ZEROOS_KEY_F2 = 289,
    ZEROOS_KEY_F3 = 290,
    ZEROOS_KEY_F4 = 291,
    ZEROOS_KEY_F5 = 292,
    ZEROOS_KEY_F6 = 293,
    ZEROOS_KEY_F7 = 294,
    ZEROOS_KEY_F8 = 295,
    ZEROOS_KEY_F9 = 296,
    ZEROOS_KEY_F10 = 297,
    ZEROOS_KEY_F11 = 298,
    ZEROOS_KEY_F12 = 299,
    ZEROOS_KEY_SHIFT_L = 304,
    ZEROOS_KEY_SHIFT_R = 305,
    ZEROOS_KEY_CTRL_L = 306,
    ZEROOS_KEY_CTRL_R = 307,
    ZEROOS_KEY_ALT_L = 308,
    ZEROOS_KEY_ALT_R = 309,
    ZEROOS_KEY_SUPER_L = 310,
    ZEROOS_KEY_SUPER_R = 311,
    ZEROOS_KEY_CAPS_LOCK = 312
};

/* Pointer codes for KIND_POINTER events. Motion uses code 0 with x/y as
 * relative deltas (device orientation, y up) and value = button mask;
 * button transitions use codes 1..5 with value = pressed (1/0).
 * Values mirror kernel/mouse_core.h enum pointer_code (drift-gated). */
enum zeroos_pointer_code {
    ZEROOS_POINTER_MOTION = 0,
    ZEROOS_POINTER_BTN_LEFT = 1,
    ZEROOS_POINTER_BTN_RIGHT = 2,
    ZEROOS_POINTER_BTN_MIDDLE = 3,
    ZEROOS_POINTER_BTN_SIDE = 4,
    ZEROOS_POINTER_BTN_EXTRA = 5
};

#define ZEROOS_INPUT_FLAG_DOWN   (1U << 0)
#define ZEROOS_INPUT_FLAG_REPEAT (1U << 1)

/* Input event (ABI copy of kernel input_core.h struct input_event;
 * abi_consistency.py gates the struct bodies against each other). */
struct zeroos_input_event {
    uint64_t timestamp;
    uint32_t device_id;
    int32_t x, y, value;
    uint16_t code;
    uint8_t kind, flags;
};

typedef uint64_t zeroos_handle_t;
typedef zeroos_handle_t zeroos_ipc_handle_t;
typedef zeroos_handle_t zeroos_shmem_handle_t;

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

/* Display geometry; flags tell whether a linear framebuffer is live. */
static inline int64_t zeroos_display_info(struct zeroos_display_info *info) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_DISPLAY_INFO,(uint64_t)(uintptr_t)info,sizeof(*info),
        0,0,0,0));
}

/* Pixel-mapping present: copy a source rectangle of the scanout format
 * into the live framebuffer. stride is the source row pitch in bytes;
 * pixels must reference a readable buffer of (height-1)*stride +
 * width*bpp/8 bytes. Fails ENOENT when no linear framebuffer is live. */
static inline int64_t zeroos_display_present(uint32_t x, uint32_t y,
                                             uint32_t width, uint32_t height,
                                             uint32_t stride,
                                             const void *pixels) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_DISPLAY_PRESENT,(uint64_t)x,(uint64_t)y,(uint64_t)width,
        (uint64_t)height,(uint64_t)stride,(uint64_t)(uintptr_t)pixels));
}

/* Input: nonblocking drain (0 with event, -EAGAIN when empty) and
 * blocking wait. timeout is in scheduler ticks; 0 waits forever.
 * flags accepts ZEROOS_WAIT_FLAG_NONBLOCK only. */
static inline int64_t zeroos_input_poll(struct zeroos_input_event *event) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_INPUT_POLL,(uint64_t)(uintptr_t)event,0,0,0,0,0));
}
static inline int64_t zeroos_input_wait(struct zeroos_input_event *event,
                                        uint64_t flags,
                                        uint64_t timeout_ticks) {
    return zeroos_syscall_result(zeroos_syscall6(
        ZEROOS_SYS_INPUT_WAIT,(uint64_t)(uintptr_t)event,flags,
        timeout_ticks,0,0,0));
}

#endif
