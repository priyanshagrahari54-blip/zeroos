#ifndef ZEROOS_IPC_H
#define ZEROOS_IPC_H

#include "types.h"

struct process;

/* Production IPC bounds — all data paths are bounded, no dynamic allocation.
 * - endpoints: 64 total, each with fixed ring buffer for pipe (2048) or
 *   queue (8*256) or event bit
 * - capabilities: 128 total, generation-tagged, process-scoped
 * - pipe capacity: 2048 bytes, byte-stream, ring-buffer
 * - message queue depth: 8, each max 256 bytes
 */
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

/* Pipe byte-stream contract 10x advanced (see ipc.c for full semantics):
 * - fixed capacity 2048, ring-buffer byte semantics, 75%/25% high/low watermarks
 * - partial read: returns min(available, requested) >=1 when data present
 * - partial write: returns min(free, requested) >=1 when space present; free==0 blocks or EAGAIN/ETIMEDOUT
 * - full-buffer backpressure, empty-buffer blocking, throttling via high_watermark
 * - correct wakeups, no lost wakeups (condition + waiter publication under ipc_lock)
 * - timeout-aware: infinite uses wait_queue_prepare/commit event-driven, timed uses 1-tick polling fallback due to scheduler invariant
 * - cancellation via endpoint destruction waking all waiters
 * - peer-close: writer EPIPE when reader closed; reader drains then EPIPE
 * - lifetime: generation + refcount, process revocation on exit wakes blocked peers
 * - concurrent readers/writers: serialized by ipc_lock, each transfer atomic
 * - close while blocked, exit while blocked: wakes with EPIPE
 * - PEEK: inspect without consuming; repeated PEEK same bytes; interaction timeout still applies, close returns EPIPE when empty, partial returns min(count,capacity), concurrent PEEKs serialized by ipc_lock never torn, contention_count tracks races
 * - user-copy validation in syscall layer via process_address_space_is_user_range
 * - bounded memory/resource accounting: fixed buffers 2048, 64 endpoints, 128 caps, max transfer 512, no alloc in data path
 * - 10x stats: bytes_written_total, bytes_read_total, peak_byte_count, contention_count, transfer_count, latency_sum_ticks, last_transfer_ticks, priority, throttled flag, high/low watermarks
 * - 10x flow control: throttled set when byte_count>=high_watermark, cleared when <=low_watermark
 * - 10x priority: 0=low,16=default,31=high foreground protected
 * Lifecycle states:
 * STOPPED: not allocated
 * DORMANT: allocated no waiters no data watermarks reset
 * WARM: has buffered data no active blocked
 * ACTIVE: active reader/writer or transfer in progress
 * THROTTLED: full or >=high_watermark writer blocked contention_count++
 * SUSPENDED: peer closed draining before EPIPE low_watermark clears throttled
 */

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
