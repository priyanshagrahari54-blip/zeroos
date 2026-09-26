#ifndef ZEROOS_USERSPACE_RUNTIME_H
#define ZEROOS_USERSPACE_RUNTIME_H

#include <stdint.h>
#include <zeroos/syscall.h>

/* Small freestanding policy layer shared by init and service binaries. It
 * validates the negotiated ABI once, preserves the kernel's signed error
 * convention, and keeps bounded transfer rules out of individual programs.
 *
 * Stage 2 contract:
 * - memory: shmem create/grant/map/unmap/close with explicit rights
 * - processes/threads: spawn, wait, getpid/gettid, yield, exit
 * - IPC: message queue, byte-stream pipe, coalescing event, all with
 *   capability-checked handles, timeout, cancellation, backpressure
 * - synchronization: event signal/wait (coalescing) as the Stage 2 primitive;
 *   full mutex/rwlock/condvar is future, but event covers blocking/wakeup
 * - environment/session: argv/envp/auxv via spawn, ABI info via abi_info
 * - errors: stable ZEROOS_E* codes, signed result convention
 * - handles/descriptors: generation-tagged, process-scoped, rights-checked
 * - filesystem: not yet (Stage 3), but write() diagnostic is bounded
 *
 * Keep kernel policy out of userspace libraries: this runtime does not
 * implement scheduling, resource, or security policy, only validated wrappers.
 */
struct zeroos_runtime {
    struct zeroos_abi_info abi;
};

int zeroos_runtime_init(struct zeroos_runtime *runtime);
int64_t zeroos_runtime_write(const struct zeroos_runtime *runtime,
                             uint64_t fd, const void *buffer,
                             uint64_t length);
int64_t zeroos_runtime_getpid(const struct zeroos_runtime *runtime);
int64_t zeroos_runtime_gettid(const struct zeroos_runtime *runtime);
int64_t zeroos_runtime_yield(const struct zeroos_runtime *runtime);
int64_t zeroos_runtime_ipc_create(const struct zeroos_runtime *runtime,
                                  struct zeroos_ipc_pair *pair);
int64_t zeroos_runtime_ipc_grant(const struct zeroos_runtime *runtime,
                                 zeroos_ipc_handle_t source,
                                 uint64_t target_pid, uint8_t rights,
                                 zeroos_ipc_handle_t *target);
int64_t zeroos_runtime_ipc_close(const struct zeroos_runtime *runtime,
                                 zeroos_ipc_handle_t handle);
int64_t zeroos_runtime_ipc_send(const struct zeroos_runtime *runtime,
                                zeroos_ipc_handle_t handle, const void *data,
                                uint64_t length, uint64_t flags,
                                uint64_t timeout);
int64_t zeroos_runtime_ipc_receive(const struct zeroos_runtime *runtime,
                                   zeroos_ipc_handle_t handle, void *data,
                                   uint64_t capacity, uint64_t flags,
                                   uint64_t *length, uint64_t timeout);
int64_t zeroos_runtime_pipe_create(const struct zeroos_runtime *runtime,
                                   struct zeroos_ipc_pair *pair);
int64_t zeroos_runtime_pipe_write(const struct zeroos_runtime *runtime,
                                  zeroos_ipc_handle_t handle, const void *data,
                                  uint64_t length, uint64_t flags,
                                  uint64_t timeout);
int64_t zeroos_runtime_pipe_read(const struct zeroos_runtime *runtime,
                                 zeroos_ipc_handle_t handle, void *data,
                                 uint64_t capacity, uint64_t flags,
                                 uint64_t *length, uint64_t timeout);
int64_t zeroos_runtime_event_create(const struct zeroos_runtime *runtime,
                                    struct zeroos_ipc_pair *pair);
int64_t zeroos_runtime_event_signal(const struct zeroos_runtime *runtime,
                                    zeroos_ipc_handle_t handle,
                                    uint64_t flags);
int64_t zeroos_runtime_event_wait(const struct zeroos_runtime *runtime,
                                  zeroos_ipc_handle_t handle,
                                  uint64_t flags, uint64_t timeout);
int64_t zeroos_runtime_event_close(const struct zeroos_runtime *runtime,
                                   zeroos_ipc_handle_t handle);
int64_t zeroos_runtime_shmem_create(const struct zeroos_runtime *runtime,
                                    uint64_t size, uint64_t flags,
                                    zeroos_shmem_handle_t *handle);
int64_t zeroos_runtime_shmem_grant(const struct zeroos_runtime *runtime,
                                   zeroos_shmem_handle_t source,
                                   uint64_t target_pid, uint8_t rights,
                                   zeroos_shmem_handle_t *target);
int64_t zeroos_runtime_shmem_map(const struct zeroos_runtime *runtime,
                                 zeroos_shmem_handle_t handle,
                                 uintptr_t address, uint64_t flags);
int64_t zeroos_runtime_shmem_unmap(const struct zeroos_runtime *runtime,
                                   zeroos_shmem_handle_t handle,
                                   uintptr_t address);
int64_t zeroos_runtime_shmem_close(const struct zeroos_runtime *runtime,
                                   zeroos_shmem_handle_t handle);
int64_t zeroos_runtime_spawn(const struct zeroos_runtime *runtime,
                             const void *image, uint64_t image_size,
                             const uint64_t *argv, uint64_t argc,
                             const uint64_t *envp, uint64_t envc);
int64_t zeroos_runtime_wait(const struct zeroos_runtime *runtime,
                            uint64_t pid, uint64_t *status, uint64_t flags,
                            uint64_t timeout);

#endif
