#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zeroos/runtime.h>

/* Replace only the calls exercised by this host policy test. Keeping the
 * public syscall header intact lets the real runtime source compile while
 * preventing a host int 0x80 instruction from being executed. */
static int64_t host_abi_info(struct zeroos_abi_info *info);
static int64_t host_event_create(struct zeroos_ipc_pair *pair);
static int64_t host_event_signal(zeroos_handle_t handle, uint64_t flags);
static int64_t host_event_wait(zeroos_handle_t handle, uint64_t flags,
                               uint64_t timeout);
static int64_t host_event_close(zeroos_handle_t handle);
static int64_t host_shmem_create(uint64_t size, uint64_t flags,
                                 zeroos_shmem_handle_t *handle);
static int64_t host_shmem_grant(zeroos_shmem_handle_t source,
                                uint64_t target_pid, uint8_t rights,
                                zeroos_shmem_handle_t *target);
static int64_t host_shmem_map(zeroos_shmem_handle_t handle,
                              uintptr_t address, uint64_t flags);
static int64_t host_shmem_unmap(zeroos_shmem_handle_t handle,
                                uintptr_t address);
static int64_t host_shmem_close(zeroos_shmem_handle_t handle);

#define zeroos_abi_info host_abi_info
#define zeroos_event_create host_event_create
#define zeroos_event_signal host_event_signal
#define zeroos_event_wait host_event_wait
#define zeroos_event_close host_event_close
#define zeroos_shmem_create host_shmem_create
#define zeroos_shmem_grant host_shmem_grant
#define zeroos_shmem_map host_shmem_map
#define zeroos_shmem_unmap host_shmem_unmap
#define zeroos_shmem_close host_shmem_close
#include "../runtime.c"
#undef zeroos_abi_info
#undef zeroos_event_create
#undef zeroos_event_signal
#undef zeroos_event_wait
#undef zeroos_event_close
#undef zeroos_shmem_create
#undef zeroos_shmem_grant
#undef zeroos_shmem_map
#undef zeroos_shmem_unmap
#undef zeroos_shmem_close

static struct zeroos_abi_info mock_abi;
static int64_t mock_abi_result;
static int64_t mock_event_create_result;
static int64_t mock_event_signal_result;
static int64_t mock_event_wait_result;
static int64_t mock_event_close_result;
static int64_t mock_shmem_create_result;
static int64_t mock_shmem_grant_result;
static int64_t mock_shmem_map_result;
static int64_t mock_shmem_unmap_result;
static int64_t mock_shmem_close_result;
static uint32_t abi_calls;
static uint32_t event_create_calls;
static uint32_t event_signal_calls;
static uint32_t event_wait_calls;
static uint32_t event_close_calls;
static uint32_t shmem_create_calls;
static uint32_t shmem_grant_calls;
static uint32_t shmem_map_calls;
static uint32_t shmem_unmap_calls;
static uint32_t shmem_close_calls;
static struct zeroos_ipc_pair *seen_event_pair;
static zeroos_handle_t seen_event_handle;
static uint64_t seen_event_flags;
static uint64_t seen_event_timeout;
static uint64_t seen_shmem_size;
static uint64_t seen_shmem_flags;
static zeroos_shmem_handle_t seen_shmem_handle;
static uint64_t seen_target_pid;
static uint8_t seen_rights;
static zeroos_shmem_handle_t seen_target_handle;
static uintptr_t seen_address;

static void reset_mocks(void) {
    memset(&mock_abi,0,sizeof(mock_abi));
    mock_abi.version=ZEROOS_SYSCALL_ABI_VERSION;
    mock_abi.size=sizeof(mock_abi);
    mock_abi.features=ZEROOS_ABI_FEATURE_PROCESS |
                      ZEROOS_ABI_FEATURE_MEMORY |
                      ZEROOS_ABI_FEATURE_IPC |
                      ZEROOS_ABI_FEATURE_INIT |
                      ZEROOS_ABI_FEATURE_PIPE |
                      ZEROOS_ABI_FEATURE_EVENT |
                      ZEROOS_ABI_FEATURE_SHMEM;
    mock_abi.max_transfer=ZEROOS_SYSCALL_MAX_TRANSFER;
    mock_abi_result=0;
    mock_event_create_result=0;
    mock_event_signal_result=0;
    mock_event_wait_result=0;
    mock_event_close_result=0;
    mock_shmem_create_result=0;
    mock_shmem_grant_result=0;
    mock_shmem_map_result=0;
    mock_shmem_unmap_result=0;
    mock_shmem_close_result=0;
    abi_calls=0;
    event_create_calls=0;
    event_signal_calls=0;
    event_wait_calls=0;
    event_close_calls=0;
    shmem_create_calls=0;
    shmem_grant_calls=0;
    shmem_map_calls=0;
    shmem_unmap_calls=0;
    shmem_close_calls=0;
    seen_event_pair=0;
    seen_event_handle=0;
    seen_event_flags=0;
    seen_event_timeout=0;
    seen_shmem_size=0;
    seen_shmem_flags=0;
    seen_shmem_handle=0;
    seen_target_pid=0;
    seen_rights=0;
    seen_target_handle=0;
    seen_address=0;
}

static int64_t host_abi_info(struct zeroos_abi_info *info) {
    ++abi_calls;
    if (mock_abi_result<0)
        return mock_abi_result;
    *info=mock_abi;
    return mock_abi_result;
}

static int64_t host_event_create(struct zeroos_ipc_pair *pair) {
    ++event_create_calls;
    seen_event_pair=pair;
    if (mock_event_create_result==0) {
        pair->local=0x101ULL;
        pair->peer=0x202ULL;
    }
    return mock_event_create_result;
}

static int64_t host_event_signal(zeroos_handle_t handle, uint64_t flags) {
    ++event_signal_calls;
    seen_event_handle=handle;
    seen_event_flags=flags;
    return mock_event_signal_result;
}

static int64_t host_event_wait(zeroos_handle_t handle, uint64_t flags,
                               uint64_t timeout) {
    ++event_wait_calls;
    seen_event_handle=handle;
    seen_event_flags=flags;
    seen_event_timeout=timeout;
    return mock_event_wait_result;
}

static int64_t host_event_close(zeroos_handle_t handle) {
    ++event_close_calls;
    seen_event_handle=handle;
    return mock_event_close_result;
}

static int64_t host_shmem_create(uint64_t size, uint64_t flags,
                                 zeroos_shmem_handle_t *handle) {
    ++shmem_create_calls;
    seen_shmem_size=size;
    seen_shmem_flags=flags;
    if (mock_shmem_create_result==0)
        *handle=0x303ULL;
    return mock_shmem_create_result;
}

static int64_t host_shmem_grant(zeroos_shmem_handle_t source,
                                uint64_t target_pid, uint8_t rights,
                                zeroos_shmem_handle_t *target) {
    ++shmem_grant_calls;
    seen_shmem_handle=source;
    seen_target_pid=target_pid;
    seen_rights=rights;
    if (mock_shmem_grant_result==0)
        *target=0x404ULL;
    return mock_shmem_grant_result;
}

static int64_t host_shmem_map(zeroos_shmem_handle_t handle,
                              uintptr_t address, uint64_t flags) {
    ++shmem_map_calls;
    seen_shmem_handle=handle;
    seen_address=address;
    seen_shmem_flags=flags;
    return mock_shmem_map_result;
}

static int64_t host_shmem_unmap(zeroos_shmem_handle_t handle,
                                uintptr_t address) {
    ++shmem_unmap_calls;
    seen_shmem_handle=handle;
    seen_address=address;
    return mock_shmem_unmap_result;
}

static int64_t host_shmem_close(zeroos_shmem_handle_t handle) {
    ++shmem_close_calls;
    seen_shmem_handle=handle;
    return mock_shmem_close_result;
}

static void init_runtime(struct zeroos_runtime *runtime) {
    reset_mocks();
    assert(zeroos_runtime_init(runtime)==0);
    assert(abi_calls==1);
}

static void test_feature_negotiation(void) {
    struct zeroos_runtime runtime={0};
    struct zeroos_ipc_pair pair={0};

    reset_mocks();
    assert(zeroos_runtime_init(0)==-ZEROOS_EINVAL);
    assert(abi_calls==0);
    mock_abi_result=-ZEROOS_EIO;
    assert(zeroos_runtime_init(&runtime)==-ZEROOS_EIO);
    assert(abi_calls==1);

    reset_mocks();
    mock_abi.version=ZEROOS_SYSCALL_ABI_VERSION+1U;
    assert(zeroos_runtime_init(&runtime)==-ZEROOS_ENOSYS);

    reset_mocks();
    mock_abi.features&=~ZEROOS_ABI_FEATURE_EVENT;
    assert(zeroos_runtime_init(&runtime)==0);
    assert(zeroos_runtime_event_create(&runtime,&pair)==-ZEROOS_ENOSYS);
    assert(event_create_calls==0);
    assert(zeroos_runtime_shmem_close(&runtime,0x55)==0);
    assert(shmem_close_calls==1);
    runtime.abi.features&=~ZEROOS_ABI_FEATURE_SHMEM;
    assert(zeroos_runtime_shmem_close(&runtime,0x55)==-ZEROOS_ENOSYS);
    assert(shmem_close_calls==1);
}

static void test_event_runtime(void) {
    struct zeroos_runtime runtime={0};
    struct zeroos_ipc_pair pair={0};

    init_runtime(&runtime);
    assert(zeroos_runtime_event_create(&runtime,0)==-ZEROOS_EINVAL);
    assert(event_create_calls==0);
    assert(zeroos_runtime_event_create(&runtime,&pair)==0);
    assert(event_create_calls==1 && seen_event_pair==&pair);
    assert(pair.local==0x101ULL && pair.peer==0x202ULL);

    assert(zeroos_runtime_event_signal(&runtime,pair.local,
                                       ZEROOS_IPC_FLAG_PEEK)==-ZEROOS_EINVAL);
    assert(event_signal_calls==0);
    mock_event_signal_result=0;       /* already pending: coalesced */
    assert(zeroos_runtime_event_signal(&runtime,pair.local,
                                       ZEROOS_IPC_FLAG_NONBLOCK)==0);
    assert(seen_event_handle==pair.local &&
           seen_event_flags==ZEROOS_IPC_FLAG_NONBLOCK);
    mock_event_signal_result=1;       /* empty -> pending transition */
    assert(zeroos_runtime_event_signal(&runtime,pair.local,0)==1);

    assert(zeroos_runtime_event_wait(&runtime,pair.peer,1ULL<<8,0)==
           -ZEROOS_EINVAL);
    assert(event_wait_calls==0);
    mock_event_wait_result=1;
    assert(zeroos_runtime_event_wait(&runtime,pair.peer,
             ZEROOS_IPC_FLAG_PEEK|ZEROOS_IPC_FLAG_NONBLOCK,73)==1);
    assert(seen_event_handle==pair.peer &&
           seen_event_flags==(ZEROOS_IPC_FLAG_PEEK|ZEROOS_IPC_FLAG_NONBLOCK) &&
           seen_event_timeout==73);
    mock_event_wait_result=-ZEROOS_EPIPE;
    assert(zeroos_runtime_event_wait(&runtime,pair.peer,0,0)==-ZEROOS_EPIPE);
    assert(seen_event_timeout==0);    /* zero retains wait-forever semantics */

    mock_event_close_result=0;
    assert(zeroos_runtime_event_close(&runtime,pair.local)==0);
    assert(event_close_calls==1 && seen_event_handle==pair.local);
}

static void test_shmem_runtime(void) {
    struct zeroos_runtime runtime={0};
    zeroos_shmem_handle_t handle=0;
    zeroos_shmem_handle_t granted=0;
    const uintptr_t map_address=(uintptr_t)0x7f0000010000ULL;

    init_runtime(&runtime);
    assert(zeroos_runtime_shmem_create(&runtime,0,0,&handle)==-ZEROOS_EINVAL);
    assert(zeroos_runtime_shmem_create(&runtime,ZEROOS_SHMEM_MAX_SIZE+1ULL,
                                        0,&handle)==-ZEROOS_EINVAL);
    assert(zeroos_runtime_shmem_create(&runtime,1,1,&handle)==-ZEROOS_EINVAL);
    assert(zeroos_runtime_shmem_create(&runtime,1,0,0)==-ZEROOS_EINVAL);
    assert(shmem_create_calls==0);
    assert(zeroos_runtime_shmem_create(&runtime,ZEROOS_SHMEM_MAX_SIZE,0,
                                        &handle)==0);
    assert(shmem_create_calls==1 && seen_shmem_size==ZEROOS_SHMEM_MAX_SIZE &&
           seen_shmem_flags==0 && handle==0x303ULL);

    assert(zeroos_runtime_shmem_grant(&runtime,handle,0,
              ZEROOS_SHMEM_RIGHT_MAP,&granted)==-ZEROOS_EINVAL);
    assert(zeroos_runtime_shmem_grant(&runtime,handle,42,0,&granted)==
           -ZEROOS_EINVAL);
    assert(zeroos_runtime_shmem_grant(&runtime,handle,42,0x80U,&granted)==
           -ZEROOS_EINVAL);
    assert(zeroos_runtime_shmem_grant(&runtime,handle,42,
              ZEROOS_SHMEM_RIGHT_MAP,0)==-ZEROOS_EINVAL);
    assert(shmem_grant_calls==0);
    assert(zeroos_runtime_shmem_grant(&runtime,handle,42,
              ZEROOS_SHMEM_RIGHT_MAP,&granted)==0);
    assert(shmem_grant_calls==1 && seen_shmem_handle==handle &&
           seen_target_pid==42 && seen_rights==ZEROOS_SHMEM_RIGHT_MAP &&
           granted==0x404ULL);

    assert(zeroos_runtime_shmem_map(&runtime,handle,0,
              ZEROOS_SHMEM_MAP_WRITE)==-ZEROOS_EINVAL);
    assert(zeroos_runtime_shmem_map(&runtime,handle,map_address+1U,0)==
           -ZEROOS_EINVAL);
    assert(zeroos_runtime_shmem_map(&runtime,handle,map_address,2)==
           -ZEROOS_EINVAL);
    assert(shmem_map_calls==0);
    mock_shmem_map_result=(int64_t)map_address;
    assert(zeroos_runtime_shmem_map(&runtime,handle,map_address,
                                    ZEROOS_SHMEM_MAP_WRITE)==
           (int64_t)map_address);
    assert(shmem_map_calls==1 && seen_shmem_handle==handle &&
           seen_address==map_address &&
           seen_shmem_flags==ZEROOS_SHMEM_MAP_WRITE);

    assert(zeroos_runtime_shmem_unmap(&runtime,handle,map_address+1U)==
           -ZEROOS_EINVAL);
    assert(shmem_unmap_calls==0);
    assert(zeroos_runtime_shmem_unmap(&runtime,handle,map_address)==0);
    assert(shmem_unmap_calls==1 && seen_shmem_handle==handle &&
           seen_address==map_address);
    mock_shmem_close_result=-ZEROOS_EBUSY;
    assert(zeroos_runtime_shmem_close(&runtime,handle)==-ZEROOS_EBUSY);
    assert(shmem_close_calls==1 && seen_shmem_handle==handle);
}

int main(void) {
    test_feature_negotiation();
    test_event_runtime();
    test_shmem_runtime();
    puts("userspace runtime policy tests: PASS (feature gates, events, shared memory)");
    return 0;
}
