#ifndef ZEROOS_USERSPACE_STORAGE_H
#define ZEROOS_USERSPACE_STORAGE_H

/* Freestanding wrappers for the file/VFS syscalls (feature
 * ZEROOS_ABI_FEATURE_FILES). Return values: >= 0 success, negative
 * -ZEROOS_E* on failure. Semantics: docs/VFS.md. */
#include "syscall.h"

#define ZS_(n, a, b, c, d) zeroos_syscall_result(zeroos_syscall6((n), (uint64_t)(a), \
    (uint64_t)(b), (uint64_t)(c), (uint64_t)(d), 0, 0))

static inline int64_t zeroos_open(const char *path, uint32_t flags, uint32_t mode) {
    return ZS_(ZEROOS_SYS_OPEN, path, flags, mode, 0);
}
static inline int64_t zeroos_close(int64_t fd) { return ZS_(ZEROOS_SYS_CLOSE, fd, 0, 0, 0); }
static inline int64_t zeroos_dup(int64_t fd) { return ZS_(ZEROOS_SYS_DUP, fd, 0, 0, 0); }
static inline int64_t zeroos_read(int64_t fd, void *buffer, uint64_t length) {
    return ZS_(ZEROOS_SYS_READ, fd, buffer, length, 0);
}
static inline int64_t zeroos_file_write(int64_t fd, const void *buffer, uint64_t length) {
    return ZS_(ZEROOS_SYS_FILE_WRITE, fd, buffer, length, 0);
}
static inline int64_t zeroos_pread(int64_t fd, void *buffer, uint64_t length, uint64_t offset) {
    return ZS_(ZEROOS_SYS_PREAD, fd, buffer, length, offset);
}
static inline int64_t zeroos_pwrite(int64_t fd, const void *buffer, uint64_t length,
                                    uint64_t offset) {
    return ZS_(ZEROOS_SYS_PWRITE, fd, buffer, length, offset);
}
static inline int64_t zeroos_seek(int64_t fd, int64_t offset, uint32_t whence) {
    return ZS_(ZEROOS_SYS_SEEK, fd, offset, whence, 0);
}
static inline int64_t zeroos_fstat(int64_t fd, struct zeroos_stat *st) {
    return ZS_(ZEROOS_SYS_FSTAT, fd, st, 0, 0);
}
static inline int64_t zeroos_stat(const char *path, struct zeroos_stat *st) {
    return ZS_(ZEROOS_SYS_STAT, path, st, 0, 0);
}
/* 1 = entry returned, 0 = end of directory. */
static inline int64_t zeroos_readdir(int64_t fd, struct zeroos_dirent *entry) {
    return ZS_(ZEROOS_SYS_READDIR, fd, entry, 0, 0);
}
static inline int64_t zeroos_mkdir(const char *path, uint32_t mode) {
    return ZS_(ZEROOS_SYS_MKDIR, path, mode, 0, 0);
}
static inline int64_t zeroos_unlink(const char *path) { return ZS_(ZEROOS_SYS_UNLINK, path, 0, 0, 0); }
static inline int64_t zeroos_rmdir(const char *path) { return ZS_(ZEROOS_SYS_RMDIR, path, 0, 0, 0); }
static inline int64_t zeroos_rename(const char *from, const char *to) {
    return ZS_(ZEROOS_SYS_RENAME, from, to, 0, 0);
}
static inline int64_t zeroos_link(const char *from, const char *to) {
    return ZS_(ZEROOS_SYS_LINK, from, to, 0, 0);
}
static inline int64_t zeroos_fsync(int64_t fd, uint32_t flags) {
    return ZS_(ZEROOS_SYS_FSYNC, fd, flags, 0, 0);
}
static inline int64_t zeroos_ftruncate(int64_t fd, uint64_t size) {
    return ZS_(ZEROOS_SYS_FTRUNCATE, fd, size, 0, 0);
}
static inline int64_t zeroos_statfs(const char *path, struct zeroos_statfs *out) {
    return ZS_(ZEROOS_SYS_STATFS, path, out, 0, 0);
}
static inline int64_t zeroos_chmod(const char *path, uint32_t mode) {
    return ZS_(ZEROOS_SYS_CHMOD, path, mode, 0, 0);
}
static inline int64_t zeroos_chown(const char *path, uint32_t uid, uint32_t gid) {
    return ZS_(ZEROOS_SYS_CHOWN, path, uid, gid, 0);
}
/* Returns uid | (gid << 32). */
static inline int64_t zeroos_getcred(void) { return ZS_(ZEROOS_SYS_GETCRED, 0, 0, 0, 0); }
static inline int64_t zeroos_setcred(uint32_t uid, uint32_t gid) {
    return ZS_(ZEROOS_SYS_SETCRED, uid, gid, 0, 0);
}
/* Returns the mapped user address (kernel-chosen) or -ZEROOS_E*. */
static inline int64_t zeroos_mmap(int64_t fd, uint64_t offset, uint64_t length, uint32_t prot) {
    return ZS_(ZEROOS_SYS_MMAP, fd, offset, length, prot);
}
static inline int64_t zeroos_munmap(void *address) { return ZS_(ZEROOS_SYS_MUNMAP, address, 0, 0, 0); }
static inline int64_t zeroos_msync(void *address) { return ZS_(ZEROOS_SYS_MSYNC, address, 0, 0, 0); }

#undef ZS_
#endif
