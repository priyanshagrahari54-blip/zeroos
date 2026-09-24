#ifndef ZEROOS_USERSPACE_RUNTIME_H
#define ZEROOS_USERSPACE_RUNTIME_H

#include <stdint.h>
#include <zeroos/syscall.h>

/* Small freestanding policy layer shared by init and service binaries. It
 * validates the negotiated ABI once, preserves the kernel's signed error
 * convention, and keeps bounded transfer rules out of individual programs. */
struct zeroos_runtime {
    struct zeroos_abi_info abi;
};

int zeroos_runtime_init(struct zeroos_runtime *runtime);
int64_t zeroos_runtime_write(const struct zeroos_runtime *runtime,
                             uint64_t fd, const void *buffer,
                             uint64_t length);
int64_t zeroos_runtime_ipc_send(const struct zeroos_runtime *runtime,
                                zeroos_ipc_handle_t handle, const void *data,
                                uint64_t length, uint64_t flags,
                                uint64_t timeout);
int64_t zeroos_runtime_ipc_receive(const struct zeroos_runtime *runtime,
                                   zeroos_ipc_handle_t handle, void *data,
                                   uint64_t capacity, uint64_t flags,
                                   uint64_t *length, uint64_t timeout);
int64_t zeroos_runtime_pipe_write(const struct zeroos_runtime *runtime,
                                  zeroos_ipc_handle_t handle, const void *data,
                                  uint64_t length, uint64_t flags,
                                  uint64_t timeout);
int64_t zeroos_runtime_pipe_read(const struct zeroos_runtime *runtime,
                                 zeroos_ipc_handle_t handle, void *data,
                                 uint64_t capacity, uint64_t flags,
                                 uint64_t *length, uint64_t timeout);
int64_t zeroos_runtime_spawn(const struct zeroos_runtime *runtime,
                             const void *image, uint64_t image_size,
                             const uint64_t *argv, uint64_t argc,
                             const uint64_t *envp, uint64_t envc);
int64_t zeroos_runtime_wait(const struct zeroos_runtime *runtime,
                            uint64_t pid, uint64_t *status, uint64_t flags,
                            uint64_t timeout);

#endif
