#include <zeroos/syscall.h>

static int abi_compile_probe(void) {
    struct zeroos_abi_info info={0};
    struct zeroos_ipc_pair pair={0};
    zeroos_shmem_handle_t shared=0;
    uint64_t length=0;
    char buffer[8]={0};

    (void)zeroos_abi_info(&info);
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
    return (int)info.version;
}

int main(void) {
    return abi_compile_probe();
}
