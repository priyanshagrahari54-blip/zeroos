#include <zeroos/syscall.h>
#include <zeroos/runtime.h>

static int abi_compile_probe(void) {
    struct zeroos_abi_info info={0};
    struct zeroos_ipc_pair pair={0};
    zeroos_shmem_handle_t shared=0;
    uint64_t length=0;
    char buffer[8]={0};
    struct zeroos_runtime runtime={0};

    (void)zeroos_abi_info(&info);
    (void)zeroos_runtime_init(&runtime);
    (void)zeroos_getpid();
    (void)zeroos_gettid();
    (void)zeroos_yield();
    (void)zeroos_ipc_create(&pair);
    (void)zeroos_ipc_grant(pair.local,1,ZEROOS_IPC_RIGHT_SEND,&pair.peer);
    (void)zeroos_ipc_send(pair.local,buffer,sizeof(buffer),
                          ZEROOS_IPC_FLAG_NONBLOCK,0);
    (void)zeroos_ipc_receive(pair.peer,buffer,sizeof(buffer),
                             ZEROOS_IPC_FLAG_NONBLOCK,&length,0);
    (void)zeroos_pipe_create(&pair);
    (void)zeroos_pipe_write(pair.local,buffer,sizeof(buffer),
                            ZEROOS_IPC_FLAG_NONBLOCK,0);
    (void)zeroos_pipe_read(pair.peer,buffer,sizeof(buffer),
                           ZEROOS_IPC_FLAG_NONBLOCK,&length,0);
    (void)zeroos_event_create(&pair);
    (void)zeroos_event_signal(pair.local,0);
    (void)zeroos_event_wait(pair.peer,ZEROOS_IPC_FLAG_NONBLOCK,0);
    (void)zeroos_shmem_create(4096,0,&shared);
    (void)zeroos_shmem_map(shared,0x7f0000010000ULL,ZEROOS_SHMEM_MAP_WRITE);
    (void)zeroos_shmem_unmap(shared,0x7f0000010000ULL);
    (void)zeroos_shmem_close(shared);
    (void)zeroos_runtime_write(&runtime,1,buffer,sizeof(buffer));
    (void)zeroos_runtime_getpid(&runtime);
    (void)zeroos_runtime_gettid(&runtime);
    (void)zeroos_runtime_yield(&runtime);
    (void)zeroos_runtime_ipc_create(&runtime,&pair);
    (void)zeroos_runtime_ipc_grant(&runtime,pair.local,1,
                                   ZEROOS_IPC_RIGHT_SEND,&pair.peer);
    (void)zeroos_runtime_ipc_close(&runtime,pair.local);
    (void)zeroos_runtime_ipc_send(&runtime,pair.local,buffer,sizeof(buffer),
                                   ZEROOS_IPC_FLAG_NONBLOCK,0);
    (void)zeroos_runtime_ipc_receive(&runtime,pair.peer,buffer,sizeof(buffer),
                                      ZEROOS_IPC_FLAG_NONBLOCK,&length,0);
    (void)zeroos_runtime_pipe_create(&runtime,&pair);
    (void)zeroos_runtime_pipe_write(&runtime,pair.local,buffer,sizeof(buffer),
                                     ZEROOS_IPC_FLAG_NONBLOCK,0);
    (void)zeroos_runtime_pipe_read(&runtime,pair.peer,buffer,sizeof(buffer),
                                    ZEROOS_IPC_FLAG_NONBLOCK,&length,0);
    (void)zeroos_runtime_event_create(&runtime,&pair);
    (void)zeroos_runtime_event_signal(&runtime,pair.local,0);
    (void)zeroos_runtime_event_wait(&runtime,pair.peer,
                                    ZEROOS_IPC_FLAG_NONBLOCK,0);
    (void)zeroos_runtime_event_close(&runtime,pair.local);
    (void)zeroos_runtime_shmem_create(&runtime,4096,0,&shared);
    (void)zeroos_runtime_shmem_grant(&runtime,shared,1,
                                     ZEROOS_SHMEM_RIGHT_MAP,&shared);
    (void)zeroos_runtime_shmem_map(&runtime,shared,0x7f0000010000ULL,
                                   ZEROOS_SHMEM_MAP_WRITE);
    (void)zeroos_runtime_shmem_unmap(&runtime,shared,0x7f0000010000ULL);
    (void)zeroos_runtime_shmem_close(&runtime,shared);
    (void)zeroos_runtime_spawn(&runtime,buffer,sizeof(buffer),0,0,0,0);
    (void)zeroos_runtime_wait(&runtime,1,&length,ZEROOS_WAIT_FLAG_NONBLOCK,0);
    return (int)info.version;
}

int main(void) {
    return abi_compile_probe();
}
