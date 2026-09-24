#ifndef ZEROOS_IPC_H
#define ZEROOS_IPC_H

#include "types.h"

struct process;

#define ZEROOS_IPC_MAX_ENDPOINTS 64U
#define ZEROOS_IPC_MAX_CAPABILITIES 128U
#define ZEROOS_IPC_QUEUE_DEPTH 8U
#define ZEROOS_IPC_MAX_MESSAGE 256U
#define ZEROOS_IPC_PIPE_CAPACITY (ZEROOS_IPC_MAX_MESSAGE * 8U)

#define ZEROOS_IPC_FLAG_NONBLOCK (1ULL << 0)
#define ZEROOS_IPC_FLAG_PEEK     (1ULL << 1)
#define ZEROOS_IPC_VALID_FLAGS  (ZEROOS_IPC_FLAG_NONBLOCK | ZEROOS_IPC_FLAG_PEEK)
#define ZEROOS_IPC_TIMEOUT_FOREVER (~0ULL)
#define ZEROOS_IPC_KIND_MESSAGE 0U
#define ZEROOS_IPC_KIND_EVENT   1U
#define ZEROOS_IPC_KIND_PIPE    2U

#define ZEROOS_IPC_RIGHT_SEND  (1U << 0)
#define ZEROOS_IPC_RIGHT_RECV  (1U << 1)
#define ZEROOS_IPC_RIGHT_GRANT (1U << 2)
#define ZEROOS_IPC_RIGHT_CLOSE (1U << 3)
#define ZEROOS_IPC_ALL_RIGHTS (ZEROOS_IPC_RIGHT_SEND | ZEROOS_IPC_RIGHT_RECV | \
                               ZEROOS_IPC_RIGHT_GRANT | ZEROOS_IPC_RIGHT_CLOSE)

typedef uint64_t zeroos_ipc_handle_t;

struct zeroos_ipc_pair {
    zeroos_ipc_handle_t local;
    zeroos_ipc_handle_t peer;
};

/* Capability operations are process-scoped: a handle is invalid for every
 * process other than its generation-checked owner. */
int ipc_system_init(void);
int ipc_create(struct process *owner, zeroos_ipc_handle_t *local_out,
              zeroos_ipc_handle_t *peer_out);
int ipc_create_pipe(struct process *owner, zeroos_ipc_handle_t *local_out,
                    zeroos_ipc_handle_t *peer_out);
int ipc_create_event(struct process *owner, zeroos_ipc_handle_t *signal_out,
                     zeroos_ipc_handle_t *wait_out);
int ipc_event_signal(struct process *owner, zeroos_ipc_handle_t handle,
                     uint64_t flags);
int ipc_event_wait_timeout(struct process *owner, zeroos_ipc_handle_t handle,
                           uint64_t flags, uint64_t timeout_ticks);
int ipc_grant(struct process *owner, zeroos_ipc_handle_t source,
              uint64_t target_pid, zeroos_ipc_handle_t *target_out);
int ipc_grant_rights(struct process *owner, zeroos_ipc_handle_t source,
                     uint64_t target_pid, uint8_t rights,
                     zeroos_ipc_handle_t *target_out);
int ipc_close(struct process *owner, zeroos_ipc_handle_t handle);
int ipc_send(struct process *owner, zeroos_ipc_handle_t handle,
             const void *data, uint64_t length, uint64_t flags);
int ipc_send_timeout(struct process *owner, zeroos_ipc_handle_t handle,
                     const void *data, uint64_t length, uint64_t flags,
                     uint64_t timeout_ticks);
int ipc_receive(struct process *owner, zeroos_ipc_handle_t handle,
                void *data, uint64_t capacity, uint64_t flags,
                uint64_t *length_out);
int ipc_receive_timeout(struct process *owner, zeroos_ipc_handle_t handle,
                        void *data, uint64_t capacity, uint64_t flags,
                        uint64_t *length_out, uint64_t timeout_ticks);
int ipc_pipe_write_timeout(struct process *owner, zeroos_ipc_handle_t handle,
                           const void *data, uint64_t length, uint64_t flags,
                           uint64_t timeout_ticks);
int ipc_pipe_read_timeout(struct process *owner, zeroos_ipc_handle_t handle,
                          void *data, uint64_t capacity, uint64_t flags,
                          uint64_t *length_out, uint64_t timeout_ticks);
int ipc_process_revoke(struct process *owner);
int ipc_debug_validate(void);

#endif
