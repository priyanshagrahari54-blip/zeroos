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
    ZEROOS_SYS_DISPLAY_INFO = 51,
    ZEROOS_SYS_DISPLAY_PRESENT = 52,
    ZEROOS_SYS_INPUT_POLL = 53,
    ZEROOS_SYS_INPUT_WAIT = 54,
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
#define ZEROOS_ABI_FEATURE_DISPLAY  (1ULL << 8)
#define ZEROOS_ABI_FEATURE_PRESENT  (1ULL << 9)
#define ZEROOS_ABI_FEATURE_INPUT    (1ULL << 10)

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
 * Values mirror kernel/mouse_core.h enum pointer_code (gated by
 * abi_consistency.py). */
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
