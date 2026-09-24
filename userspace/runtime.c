#include <zeroos/runtime.h>

static int runtime_ready(const struct zeroos_runtime *runtime) {
    uint64_t required=ZEROOS_ABI_FEATURE_PROCESS |
                      ZEROOS_ABI_FEATURE_MEMORY |
                      ZEROOS_ABI_FEATURE_IPC |
                      ZEROOS_ABI_FEATURE_INIT;
    return runtime && runtime->abi.version==ZEROOS_SYSCALL_ABI_VERSION &&
           runtime->abi.size>=sizeof(runtime->abi) &&
           (runtime->abi.features&required)==required &&
           runtime->abi.max_transfer!=0;
}

int zeroos_runtime_init(struct zeroos_runtime *runtime) {
    int64_t result;
    if (!runtime)
        return -ZEROOS_EINVAL;
    result=zeroos_abi_info(&runtime->abi);
    if (result<0)
        return (int)result;
    if (!runtime_ready(runtime))
        return -ZEROOS_ENOSYS;
    return 0;
}

int64_t zeroos_runtime_write(const struct zeroos_runtime *runtime,
                             uint64_t fd, const void *buffer,
                             uint64_t length) {
    const uint8_t *cursor=(const uint8_t *)buffer;
    uint64_t total=0;
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (length && !buffer)
        return -ZEROOS_EFAULT;
    while (total<length) {
        uint64_t remaining=length-total;
        uint64_t chunk=remaining<runtime->abi.max_transfer ?
                       remaining : runtime->abi.max_transfer;
        int64_t result=zeroos_write(fd,cursor+total,chunk);
        if (result<0)
            return result;
        if ((uint64_t)result!=chunk)
            return -ZEROOS_EINTR;
        total+=chunk;
    }
    return (int64_t)total;
}

int64_t zeroos_runtime_getpid(const struct zeroos_runtime *runtime) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    return zeroos_getpid();
}

int64_t zeroos_runtime_gettid(const struct zeroos_runtime *runtime) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    return zeroos_gettid();
}

int64_t zeroos_runtime_yield(const struct zeroos_runtime *runtime) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    return zeroos_yield();
}

int64_t zeroos_runtime_ipc_create(const struct zeroos_runtime *runtime,
                                  struct zeroos_ipc_pair *pair) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!pair)
        return -ZEROOS_EFAULT;
    return zeroos_ipc_create(pair);
}

int64_t zeroos_runtime_ipc_grant(const struct zeroos_runtime *runtime,
                                 zeroos_ipc_handle_t source,
                                 uint64_t target_pid, uint8_t rights,
                                 zeroos_ipc_handle_t *target) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!target || target_pid==0 || rights==0 ||
        (rights & ~ZEROOS_IPC_ALL_RIGHTS))
        return -ZEROOS_EINVAL;
    return zeroos_ipc_grant(source,target_pid,rights,target);
}

int64_t zeroos_runtime_ipc_close(const struct zeroos_runtime *runtime,
                                 zeroos_ipc_handle_t handle) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    return zeroos_ipc_close(handle);
}

int64_t zeroos_runtime_ipc_send(const struct zeroos_runtime *runtime,
                                zeroos_ipc_handle_t handle, const void *data,
                                uint64_t length, uint64_t flags,
                                uint64_t timeout) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!data || !length || length>ZEROOS_IPC_MAX_MESSAGE)
        return length>ZEROOS_IPC_MAX_MESSAGE ? -ZEROOS_EOVERFLOW :
                                               -ZEROOS_EINVAL;
    return zeroos_ipc_send(handle,data,length,flags,timeout);
}

int64_t zeroos_runtime_ipc_receive(const struct zeroos_runtime *runtime,
                                   zeroos_ipc_handle_t handle, void *data,
                                   uint64_t capacity, uint64_t flags,
                                   uint64_t *length, uint64_t timeout) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!data || !length || !capacity)
        return -ZEROOS_EINVAL;
    if (capacity>ZEROOS_IPC_MAX_MESSAGE)
        return -ZEROOS_EOVERFLOW;
    return zeroos_ipc_receive(handle,data,capacity,flags,length,timeout);
}

int64_t zeroos_runtime_pipe_create(const struct zeroos_runtime *runtime,
                                   struct zeroos_ipc_pair *pair) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!(runtime->abi.features&ZEROOS_ABI_FEATURE_PIPE))
        return -ZEROOS_ENOSYS;
    if (!pair)
        return -ZEROOS_EFAULT;
    return zeroos_pipe_create(pair);
}

int64_t zeroos_runtime_pipe_write(const struct zeroos_runtime *runtime,
                                  zeroos_ipc_handle_t handle, const void *data,
                                  uint64_t length, uint64_t flags,
                                  uint64_t timeout) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!(runtime->abi.features&ZEROOS_ABI_FEATURE_PIPE))
        return -ZEROOS_ENOSYS;
    if (!data || !length)
        return -ZEROOS_EINVAL;
    if (length>runtime->abi.max_transfer)
        return -ZEROOS_EOVERFLOW;
    return zeroos_pipe_write(handle,data,length,flags,timeout);
}

int64_t zeroos_runtime_pipe_read(const struct zeroos_runtime *runtime,
                                 zeroos_ipc_handle_t handle, void *data,
                                 uint64_t capacity, uint64_t flags,
                                 uint64_t *length, uint64_t timeout) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!(runtime->abi.features&ZEROOS_ABI_FEATURE_PIPE))
        return -ZEROOS_ENOSYS;
    if (!data || !length || !capacity)
        return -ZEROOS_EINVAL;
    if (capacity>runtime->abi.max_transfer)
        return -ZEROOS_EOVERFLOW;
    return zeroos_pipe_read(handle,data,capacity,flags,length,timeout);
}

int64_t zeroos_runtime_event_create(const struct zeroos_runtime *runtime,
                                    struct zeroos_ipc_pair *pair) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!(runtime->abi.features&ZEROOS_ABI_FEATURE_EVENT))
        return -ZEROOS_ENOSYS;
    if (!pair)
        return -ZEROOS_EFAULT;
    return zeroos_event_create(pair);
}

int64_t zeroos_runtime_event_signal(const struct zeroos_runtime *runtime,
                                    zeroos_ipc_handle_t handle,
                                    uint64_t flags) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!(runtime->abi.features&ZEROOS_ABI_FEATURE_EVENT))
        return -ZEROOS_ENOSYS;
    return zeroos_event_signal(handle,flags);
}

int64_t zeroos_runtime_event_wait(const struct zeroos_runtime *runtime,
                                  zeroos_ipc_handle_t handle,
                                  uint64_t flags, uint64_t timeout) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!(runtime->abi.features&ZEROOS_ABI_FEATURE_EVENT))
        return -ZEROOS_ENOSYS;
    return zeroos_event_wait(handle,flags,timeout);
}

int64_t zeroos_runtime_event_close(const struct zeroos_runtime *runtime,
                                   zeroos_ipc_handle_t handle) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!(runtime->abi.features&ZEROOS_ABI_FEATURE_EVENT))
        return -ZEROOS_ENOSYS;
    return zeroos_event_close(handle);
}

int64_t zeroos_runtime_shmem_create(const struct zeroos_runtime *runtime,
                                    uint64_t size, uint64_t flags,
                                    zeroos_shmem_handle_t *handle) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!(runtime->abi.features&ZEROOS_ABI_FEATURE_SHMEM))
        return -ZEROOS_ENOSYS;
    if (!handle || size==0)
        return size==0 ? -ZEROOS_EINVAL : -ZEROOS_EFAULT;
    return zeroos_shmem_create(size,flags,handle);
}

int64_t zeroos_runtime_shmem_grant(const struct zeroos_runtime *runtime,
                                   zeroos_shmem_handle_t source,
                                   uint64_t target_pid, uint8_t rights,
                                   zeroos_shmem_handle_t *target) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!(runtime->abi.features&ZEROOS_ABI_FEATURE_SHMEM))
        return -ZEROOS_ENOSYS;
    if (!target || target_pid==0 || rights==0 ||
        (rights & ~ZEROOS_SHMEM_ALL_RIGHTS))
        return -ZEROOS_EINVAL;
    return zeroos_shmem_grant(source,target_pid,rights,target);
}

int64_t zeroos_runtime_shmem_map(const struct zeroos_runtime *runtime,
                                 zeroos_shmem_handle_t handle,
                                 uintptr_t address, uint64_t flags) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!(runtime->abi.features&ZEROOS_ABI_FEATURE_SHMEM))
        return -ZEROOS_ENOSYS;
    return zeroos_shmem_map(handle,address,flags);
}

int64_t zeroos_runtime_shmem_unmap(const struct zeroos_runtime *runtime,
                                   zeroos_shmem_handle_t handle,
                                   uintptr_t address) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!(runtime->abi.features&ZEROOS_ABI_FEATURE_SHMEM))
        return -ZEROOS_ENOSYS;
    return zeroos_shmem_unmap(handle,address);
}

int64_t zeroos_runtime_shmem_close(const struct zeroos_runtime *runtime,
                                   zeroos_shmem_handle_t handle) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!(runtime->abi.features&ZEROOS_ABI_FEATURE_SHMEM))
        return -ZEROOS_ENOSYS;
    return zeroos_shmem_close(handle);
}

int64_t zeroos_runtime_spawn(const struct zeroos_runtime *runtime,
                             const void *image, uint64_t image_size,
                             const uint64_t *argv, uint64_t argc,
                             const uint64_t *envp, uint64_t envc) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!image || !image_size || image_size>ZEROOS_EXEC_MAX_IMAGE ||
        argc>ZEROOS_EXEC_MAX_ARGUMENTS || envc>ZEROOS_EXEC_MAX_ARGUMENTS ||
        (argc && !argv) || (envc && !envp))
        return image_size>ZEROOS_EXEC_MAX_IMAGE ? -ZEROOS_E2BIG :
               -ZEROOS_EINVAL;
    return zeroos_spawn(image,image_size,argv,argc,envp,envc);
}

int64_t zeroos_runtime_wait(const struct zeroos_runtime *runtime,
                            uint64_t pid, uint64_t *status, uint64_t flags,
                            uint64_t timeout) {
    if (!runtime_ready(runtime))
        return -ZEROOS_EPERM;
    if (!pid || (flags&~ZEROOS_WAIT_FLAG_NONBLOCK))
        return -ZEROOS_EINVAL;
    return zeroos_wait(pid,status,flags,timeout);
}
