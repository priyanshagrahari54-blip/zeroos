#include "ipc.h"
#include "process.h"
#include "sync.h"
#include "wait.h"
#include "task.h"
#include "timer.h"
#include "syscall.h"

struct ipc_message {
    uint16_t length;
    uint16_t reserved;
    uint64_t sequence;
    uint8_t data[ZEROOS_IPC_MAX_MESSAGE];
};

struct ipc_endpoint {
    uint32_t generation;
    uint16_t count;
    uint16_t head;
    uint16_t tail;
    uint8_t used;
    uint8_t kind;
    uint8_t event_pending;
    uint16_t reserved;
    struct ipc_endpoint *peer;
    struct wait_queue send_waiters;
    struct wait_queue receive_waiters;
    struct ipc_message messages[ZEROOS_IPC_QUEUE_DEPTH];
};

struct ipc_capability {
    uint32_t generation;
    uint8_t used;
    uint8_t rights;
    uint16_t reserved;
    struct process *owner;
    struct ipc_endpoint *endpoint;
};

static struct spinlock ipc_lock;
static struct ipc_endpoint endpoints[ZEROOS_IPC_MAX_ENDPOINTS];
static struct ipc_capability capabilities[ZEROOS_IPC_MAX_CAPABILITIES];
static uint64_t next_sequence;

static int process_exists(const struct process *process) {
    return process && process->state!=PROCESS_UNUSED;
}

static int process_can_use(const struct process *process) {
    return process_exists(process) && process->state!=PROCESS_ZOMBIE;
}

static uint64_t capability_make_handle(uint32_t slot, uint32_t generation) {
    return ((uint64_t)generation << 8) | ((uint64_t)slot + 1ULL);
}

static int capability_decode(zeroos_ipc_handle_t handle,
                             uint32_t *slot_out, uint32_t *generation_out) {
    uint32_t encoded_slot=(uint32_t)(handle & 0xffULL);
    uint64_t generation=handle >> 8;
    if (encoded_slot==0 || encoded_slot>ZEROOS_IPC_MAX_CAPABILITIES ||
        generation==0 || generation>0xffffffffULL)
        return -1;
    *slot_out=encoded_slot-1U;
    *generation_out=(uint32_t)generation;
    return 0;
}

static struct ipc_capability *capability_lookup_locked(
        struct process *owner, zeroos_ipc_handle_t handle) {
    uint32_t slot;
    uint32_t generation;
    if (!process_can_use(owner) ||
        capability_decode(handle,&slot,&generation)!=0)
        return 0;
    struct ipc_capability *capability=&capabilities[slot];
    if (!capability->used || capability->generation!=generation ||
        capability->owner!=owner || !capability->endpoint ||
        !capability->endpoint->used)
        return 0;
    return capability;
}

static void endpoint_destroy_locked(struct ipc_endpoint *endpoint) {
    struct ipc_endpoint *peer;
    if (!endpoint || !endpoint->used)
        return;
    /* Closing the last capability is also cancellation: wake both classes of
     * blocked operation before invalidating the endpoint generation. */
    (void)wait_queue_wake_all(&endpoint->send_waiters);
    (void)wait_queue_wake_all(&endpoint->receive_waiters);
    peer=endpoint->peer;
    endpoint->used=0;
    endpoint->peer=0;
    endpoint->count=0;
    endpoint->head=0;
    endpoint->tail=0;
    endpoint->kind=ZEROOS_IPC_KIND_MESSAGE;
    endpoint->event_pending=0;
    if (peer && peer->used && peer->peer==endpoint) {
        /* A peer close changes the condition observed by waiters on the
         * surviving endpoint. Wake both classes so they revalidate the link
         * and return EPIPE instead of sleeping behind a dead capability. */
        (void)wait_queue_wake_all(&peer->send_waiters);
        (void)wait_queue_wake_all(&peer->receive_waiters);
        peer->peer=0;
    }
}

static void capability_drop_locked(struct ipc_capability *capability) {
    struct ipc_endpoint *endpoint;
    if (!capability || !capability->used)
        return;
    endpoint=capability->endpoint;
    capability->used=0;
    capability->owner=0;
    capability->endpoint=0;
    capability->rights=0;
    if (endpoint && endpoint->used && endpoint->count==0) {
        /* `count` is the queued-message count; the capability reference
         * count is kept in the reserved field of the endpoint. */
        uint16_t references=endpoint->reserved;
        if (references)
            --references;
        endpoint->reserved=references;
        if (references==0)
            endpoint_destroy_locked(endpoint);
    } else if (endpoint && endpoint->used) {
        uint16_t references=endpoint->reserved;
        if (references)
            --references;
        endpoint->reserved=references;
        if (references==0)
            endpoint_destroy_locked(endpoint);
    }
}

static int capability_alloc_locked(struct process *owner,
                                   struct ipc_endpoint *endpoint,
                                   uint8_t rights,
                                   zeroos_ipc_handle_t *handle_out) {
    for (uint32_t i=0; i<ZEROOS_IPC_MAX_CAPABILITIES; ++i) {
        struct ipc_capability *capability=&capabilities[i];
        if (capability->used || capability->generation==0xffffffffU)
            continue;
        capability->generation+=1U;
        if (capability->generation==0)
            continue;
        capability->used=1;
        capability->rights=rights;
        capability->owner=owner;
        capability->endpoint=endpoint;
        ++endpoint->reserved;
        if (handle_out)
            *handle_out=capability_make_handle(i,capability->generation);
        return 0;
    }
    return -ZEROOS_ENOMEM;
}

int ipc_system_init(void) {
    spinlock_init(&ipc_lock);
    next_sequence=0;
    for (uint32_t i=0; i<ZEROOS_IPC_MAX_ENDPOINTS; ++i) {
        endpoints[i].generation=0;
        endpoints[i].count=0;
        endpoints[i].head=0;
        endpoints[i].tail=0;
        endpoints[i].used=0;
        endpoints[i].kind=ZEROOS_IPC_KIND_MESSAGE;
        endpoints[i].event_pending=0;
        endpoints[i].peer=0;
        endpoints[i].reserved=0;
        wait_queue_init(&endpoints[i].send_waiters);
        wait_queue_init(&endpoints[i].receive_waiters);
    }
    for (uint32_t i=0; i<ZEROOS_IPC_MAX_CAPABILITIES; ++i) {
        capabilities[i].generation=0;
        capabilities[i].used=0;
        capabilities[i].rights=0;
        capabilities[i].owner=0;
        capabilities[i].endpoint=0;
    }
    return 0;
}

static int ipc_create_kind(struct process *owner,
                           zeroos_ipc_handle_t *local_out,
                           zeroos_ipc_handle_t *peer_out,
                           uint8_t kind) {
    struct ipc_endpoint *local=0;
    struct ipc_endpoint *peer=0;
    uint64_t flags;
    int result=-ZEROOS_ENOMEM;

    if (!process_can_use(owner) || !local_out || !peer_out ||
        (kind!=ZEROOS_IPC_KIND_MESSAGE && kind!=ZEROOS_IPC_KIND_EVENT))
        return -ZEROOS_EINVAL;
    flags=spin_lock_irqsave(&ipc_lock);
    for (uint32_t i=0; i<ZEROOS_IPC_MAX_ENDPOINTS; ++i) {
        if (endpoints[i].used || endpoints[i].generation==0xffffffffU)
            continue;
        if (!local) {
            local=&endpoints[i];
            continue;
        }
        peer=&endpoints[i];
        break;
    }
    if (!local || !peer)
        goto out;

    ++local->generation;
    ++peer->generation;
    local->used=1;
    peer->used=1;
    local->kind=kind;
    peer->kind=kind;
    local->event_pending=peer->event_pending=0;
    local->count=peer->count=0;
    local->head=peer->head=0;
    local->tail=peer->tail=0;
    local->reserved=peer->reserved=0;
    wait_queue_init(&local->send_waiters);
    wait_queue_init(&local->receive_waiters);
    wait_queue_init(&peer->send_waiters);
    wait_queue_init(&peer->receive_waiters);
    local->peer=peer;
    peer->peer=local;

    result=capability_alloc_locked(owner,local,ZEROOS_IPC_ALL_RIGHTS,local_out);
    if (result!=0) {
        endpoint_destroy_locked(local);
        endpoint_destroy_locked(peer);
        goto out;
    }
    result=capability_alloc_locked(owner,peer,ZEROOS_IPC_ALL_RIGHTS,peer_out);
    if (result!=0) {
        struct ipc_capability *capability=capability_lookup_locked(owner,*local_out);
        capability_drop_locked(capability);
        endpoint_destroy_locked(peer);
        *local_out=0;
        goto out;
    }

out:
    spin_unlock_irqrestore(&ipc_lock,flags);
    return result;
}

int ipc_create(struct process *owner, zeroos_ipc_handle_t *local_out,
              zeroos_ipc_handle_t *peer_out) {
    return ipc_create_kind(owner,local_out,peer_out,ZEROOS_IPC_KIND_MESSAGE);
}

int ipc_create_event(struct process *owner, zeroos_ipc_handle_t *signal_out,
                     zeroos_ipc_handle_t *wait_out) {
    return ipc_create_kind(owner,signal_out,wait_out,ZEROOS_IPC_KIND_EVENT);
}

int ipc_grant(struct process *owner, zeroos_ipc_handle_t source,
              uint64_t target_pid, zeroos_ipc_handle_t *target_out) {
    uint8_t rights=ZEROOS_IPC_ALL_RIGHTS;
    return ipc_grant_rights(owner,source,target_pid,rights,target_out);
}

int ipc_grant_rights(struct process *owner, zeroos_ipc_handle_t source,
                     uint64_t target_pid, uint8_t rights,
                     zeroos_ipc_handle_t *target_out) {
    struct process *target;
    uint64_t flags;
    int result;

    if (!process_can_use(owner) || !target_out || target_pid==0 ||
        rights==0 || (rights&~ZEROOS_IPC_ALL_RIGHTS)!=0)
        return -ZEROOS_EINVAL;
    target=process_lookup(target_pid);
    if (!process_can_use(target))
        return -ZEROOS_ENOENT;

    flags=spin_lock_irqsave(&ipc_lock);
    /* The lookup is repeated under the IPC lock and the generation-tagged PID
     * is checked again. A target slot may have gone zombie and been reused
     * between process_lookup() and capability publication; never attach a
     * grant to that replacement process. */
    if (!target || target->pid!=target_pid || !process_can_use(target))
        result=-ZEROOS_ENOENT;
    else {
        struct ipc_capability *source_cap=
            capability_lookup_locked(owner,source);
        if (!source_cap || !(source_cap->rights&ZEROOS_IPC_RIGHT_GRANT))
            result=-ZEROOS_EBADF;
        else if ((rights&source_cap->rights)!=rights)
            result=-ZEROOS_EPERM;
        else
            result=capability_alloc_locked(target,source_cap->endpoint,
                                           rights,target_out);
    }
    spin_unlock_irqrestore(&ipc_lock,flags);
    return result;
}

int ipc_close(struct process *owner, zeroos_ipc_handle_t handle) {
    uint64_t flags;
    if (!process_can_use(owner))
        return -ZEROOS_EPERM;
    flags=spin_lock_irqsave(&ipc_lock);
    struct ipc_capability *capability=capability_lookup_locked(owner,handle);
    if (!capability || !(capability->rights&ZEROOS_IPC_RIGHT_CLOSE)) {
        spin_unlock_irqrestore(&ipc_lock,flags);
        return -ZEROOS_EBADF;
    }
    capability_drop_locked(capability);
    spin_unlock_irqrestore(&ipc_lock,flags);
    return 0;
}

int ipc_send(struct process *owner, zeroos_ipc_handle_t handle,
             const void *data, uint64_t length, uint64_t flags) {
    return ipc_send_timeout(owner,handle,data,length,flags,
                            ZEROOS_IPC_TIMEOUT_FOREVER);
}

int ipc_send_timeout(struct process *owner, zeroos_ipc_handle_t handle,
                     const void *data, uint64_t length, uint64_t flags,
                     uint64_t timeout_ticks) {
    uint64_t deadline=0;

    if (!process_can_use(owner) || !data || length==0 ||
        length>ZEROOS_IPC_MAX_MESSAGE || (flags&~ZEROOS_IPC_VALID_FLAGS))
        return -ZEROOS_EINVAL;
    if (timeout_ticks!=ZEROOS_IPC_TIMEOUT_FOREVER) {
        deadline=timer_ticks()+timeout_ticks;
        if (deadline<timer_ticks())
            deadline=~0ULL;
    }

    for (;;) {
        uint64_t irq_flags;
        struct ipc_capability *capability;
        struct ipc_endpoint *peer;
        struct ipc_message *message;

        irq_flags=spin_lock_irqsave(&ipc_lock);
        capability=capability_lookup_locked(owner,handle);
        if (!capability || !(capability->rights&ZEROOS_IPC_RIGHT_SEND)) {
            spin_unlock_irqrestore(&ipc_lock,irq_flags);
            return -ZEROOS_EBADF;
        }
        if (capability->endpoint->kind!=ZEROOS_IPC_KIND_MESSAGE) {
            spin_unlock_irqrestore(&ipc_lock,irq_flags);
            return -ZEROOS_EINVAL;
        }
        peer=capability->endpoint->peer;
        if (!peer || !peer->used) {
            spin_unlock_irqrestore(&ipc_lock,irq_flags);
            return -ZEROOS_EPIPE;
        }
        if (peer->count>=ZEROOS_IPC_QUEUE_DEPTH) {
            uint64_t block_flags;
            if (flags&ZEROOS_IPC_FLAG_NONBLOCK) {
                spin_unlock_irqrestore(&ipc_lock,irq_flags);
                return -ZEROOS_EAGAIN;
            }
            if (timeout_ticks!=ZEROOS_IPC_TIMEOUT_FOREVER) {
                int sleep_result;
                if ((long long)(deadline-timer_ticks())<=0) {
                    spin_unlock_irqrestore(&ipc_lock,irq_flags);
                    return -ZEROOS_ETIMEDOUT;
                }
                spin_unlock_irqrestore(&ipc_lock,irq_flags);
                sleep_result=task_sleep_ticks(1);
                if (sleep_result!=0)
                    return -ZEROOS_EINTR;
                continue;
            }
            /* The condition (full) and waiter publication are serialized by
             * ipc_lock, so a concurrent receive cannot lose this wakeup. */
            if (wait_queue_prepare(&peer->send_waiters,&block_flags)!=0) {
                spin_unlock_irqrestore(&ipc_lock,irq_flags);
                return -ZEROOS_EBUSY;
            }
            spin_unlock(&ipc_lock);
            if (wait_queue_commit(block_flags)!=0)
                return -ZEROOS_EINTR;
            continue;
        }
        message=&peer->messages[peer->tail];
        for (uint64_t i=0; i<length; ++i)
            message->data[i]=((const uint8_t *)data)[i];
        message->length=(uint16_t)length;
        message->sequence=++next_sequence;
        peer->tail=(uint16_t)((peer->tail+1U)%ZEROOS_IPC_QUEUE_DEPTH);
        ++peer->count;
        (void)wait_queue_wake_one(&peer->receive_waiters);
        spin_unlock_irqrestore(&ipc_lock,irq_flags);
        return (int)length;
    }
}

int ipc_receive(struct process *owner, zeroos_ipc_handle_t handle,
                void *data, uint64_t capacity, uint64_t flags,
                uint64_t *length_out) {
    return ipc_receive_timeout(owner,handle,data,capacity,flags,length_out,
                               ZEROOS_IPC_TIMEOUT_FOREVER);
}

int ipc_receive_timeout(struct process *owner, zeroos_ipc_handle_t handle,
                        void *data, uint64_t capacity, uint64_t flags,
                        uint64_t *length_out, uint64_t timeout_ticks) {
    uint64_t deadline=0;

    if (!process_can_use(owner) || !data || capacity==0 || !length_out ||
        (flags&~ZEROOS_IPC_VALID_FLAGS))
        return -ZEROOS_EINVAL;
    if (timeout_ticks!=ZEROOS_IPC_TIMEOUT_FOREVER) {
        deadline=timer_ticks()+timeout_ticks;
        if (deadline<timer_ticks())
            deadline=~0ULL;
    }

    for (;;) {
        uint64_t irq_flags;
        struct ipc_capability *capability;
        struct ipc_endpoint *endpoint;
        struct ipc_message *message;
        uint64_t length;

        irq_flags=spin_lock_irqsave(&ipc_lock);
        capability=capability_lookup_locked(owner,handle);
        if (!capability || !(capability->rights&ZEROOS_IPC_RIGHT_RECV)) {
            spin_unlock_irqrestore(&ipc_lock,irq_flags);
            return -ZEROOS_EBADF;
        }
        endpoint=capability->endpoint;
        if (endpoint->kind!=ZEROOS_IPC_KIND_MESSAGE) {
            spin_unlock_irqrestore(&ipc_lock,irq_flags);
            return -ZEROOS_EINVAL;
        }
        if (endpoint->count==0) {
            uint64_t block_flags;
            if (!endpoint->peer) {
                spin_unlock_irqrestore(&ipc_lock,irq_flags);
                return -ZEROOS_EPIPE;
            }
            if (flags&ZEROOS_IPC_FLAG_NONBLOCK) {
                spin_unlock_irqrestore(&ipc_lock,irq_flags);
                return -ZEROOS_EAGAIN;
            }
            if (timeout_ticks!=ZEROOS_IPC_TIMEOUT_FOREVER) {
                int sleep_result;
                if ((long long)(deadline-timer_ticks())<=0) {
                    spin_unlock_irqrestore(&ipc_lock,irq_flags);
                    return -ZEROOS_ETIMEDOUT;
                }
                spin_unlock_irqrestore(&ipc_lock,irq_flags);
                sleep_result=task_sleep_ticks(1);
                if (sleep_result!=0)
                    return -ZEROOS_EINTR;
                continue;
            }
            if (wait_queue_prepare(&endpoint->receive_waiters,&block_flags)!=0) {
                spin_unlock_irqrestore(&ipc_lock,irq_flags);
                return -ZEROOS_EBUSY;
            }
            spin_unlock(&ipc_lock);
            if (wait_queue_commit(block_flags)!=0)
                return -ZEROOS_EINTR;
            continue;
        }
        message=&endpoint->messages[endpoint->head];
        length=message->length;
        if (capacity<length) {
            spin_unlock_irqrestore(&ipc_lock,irq_flags);
            return -ZEROOS_EOVERFLOW;
        }
        for (uint64_t i=0; i<length; ++i)
            ((uint8_t *)data)[i]=message->data[i];
        *length_out=length;
        if (!(flags&ZEROOS_IPC_FLAG_PEEK)) {
            endpoint->head=(uint16_t)((endpoint->head+1U)%ZEROOS_IPC_QUEUE_DEPTH);
            --endpoint->count;
            (void)wait_queue_wake_one(&endpoint->send_waiters);
        }
        spin_unlock_irqrestore(&ipc_lock,irq_flags);
        return (int)length;
    }
}


int ipc_event_signal(struct process *owner, zeroos_ipc_handle_t handle,
                     uint64_t flags) {
    uint64_t irq_flags;
    struct ipc_capability *capability;
    struct ipc_endpoint *peer;

    if (!process_can_use(owner) || (flags&~ZEROOS_IPC_FLAG_NONBLOCK)!=0)
        return -ZEROOS_EINVAL;
    irq_flags=spin_lock_irqsave(&ipc_lock);
    capability=capability_lookup_locked(owner,handle);
    if (!capability || !(capability->rights&ZEROOS_IPC_RIGHT_SEND) ||
        capability->endpoint->kind!=ZEROOS_IPC_KIND_EVENT) {
        spin_unlock_irqrestore(&ipc_lock,irq_flags);
        return -ZEROOS_EBADF;
    }
    peer=capability->endpoint->peer;
    if (!peer || !peer->used || peer->kind!=ZEROOS_IPC_KIND_EVENT) {
        spin_unlock_irqrestore(&ipc_lock,irq_flags);
        return -ZEROOS_EPIPE;
    }
    /* Event notifications are coalesced: one pending bit represents any
     * number of signals until the waiter consumes it. This avoids an
     * unbounded event queue while preserving a wakeup for every empty->ready
     * transition. */
    if (peer->event_pending) {
        spin_unlock_irqrestore(&ipc_lock,irq_flags);
        return 0;
    }
    peer->event_pending=1;
    (void)wait_queue_wake_one(&peer->receive_waiters);
    spin_unlock_irqrestore(&ipc_lock,irq_flags);
    return 1;
}

int ipc_event_wait_timeout(struct process *owner, zeroos_ipc_handle_t handle,
                           uint64_t flags, uint64_t timeout_ticks) {
    uint64_t deadline=0;

    if (!process_can_use(owner) || (flags&~ZEROOS_IPC_VALID_FLAGS)!=0)
        return -ZEROOS_EINVAL;
    if (timeout_ticks!=ZEROOS_IPC_TIMEOUT_FOREVER) {
        deadline=timer_ticks()+timeout_ticks;
        if (deadline<timer_ticks())
            deadline=~0ULL;
    }

    for (;;) {
        uint64_t irq_flags;
        struct ipc_capability *capability;
        struct ipc_endpoint *endpoint;

        irq_flags=spin_lock_irqsave(&ipc_lock);
        capability=capability_lookup_locked(owner,handle);
        if (!capability || !(capability->rights&ZEROOS_IPC_RIGHT_RECV) ||
            capability->endpoint->kind!=ZEROOS_IPC_KIND_EVENT) {
            spin_unlock_irqrestore(&ipc_lock,irq_flags);
            return -ZEROOS_EBADF;
        }
        endpoint=capability->endpoint;
        if (endpoint->event_pending) {
            if (!(flags&ZEROOS_IPC_FLAG_PEEK))
                endpoint->event_pending=0;
            spin_unlock_irqrestore(&ipc_lock,irq_flags);
            return 1;
        }
        if (!endpoint->peer) {
            spin_unlock_irqrestore(&ipc_lock,irq_flags);
            return -ZEROOS_EPIPE;
        }
        if (flags&ZEROOS_IPC_FLAG_NONBLOCK) {
            spin_unlock_irqrestore(&ipc_lock,irq_flags);
            return -ZEROOS_EAGAIN;
        }
        if (timeout_ticks!=ZEROOS_IPC_TIMEOUT_FOREVER) {
            int sleep_result;
            if ((long long)(deadline-timer_ticks())<=0) {
                spin_unlock_irqrestore(&ipc_lock,irq_flags);
                return -ZEROOS_ETIMEDOUT;
            }
            spin_unlock_irqrestore(&ipc_lock,irq_flags);
            sleep_result=task_sleep_ticks(1);
            if (sleep_result!=0)
                return -ZEROOS_EINTR;
            continue;
        }
        {
            uint64_t block_flags;
            if (wait_queue_prepare(&endpoint->receive_waiters,&block_flags)!=0) {
                spin_unlock_irqrestore(&ipc_lock,irq_flags);
                return -ZEROOS_EBUSY;
            }
            spin_unlock(&ipc_lock);
            if (wait_queue_commit(block_flags)!=0)
                return -ZEROOS_EINTR;
        }
    }
}

int ipc_process_revoke(struct process *owner) {
    uint64_t flags;
    uint64_t revoked=0;
    if (!owner)
        return -ZEROOS_EINVAL;
    flags=spin_lock_irqsave(&ipc_lock);
    for (uint32_t i=0; i<ZEROOS_IPC_MAX_CAPABILITIES; ++i) {
        if (capabilities[i].used && capabilities[i].owner==owner) {
            capability_drop_locked(&capabilities[i]);
            ++revoked;
        }
    }
    spin_unlock_irqrestore(&ipc_lock,flags);
    return (int)revoked;
}

int ipc_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&ipc_lock);
    for (uint32_t i=0; i<ZEROOS_IPC_MAX_ENDPOINTS; ++i) {
        struct ipc_endpoint *endpoint=&endpoints[i];
        uint32_t refs=0;
        if (!endpoint->used) {
            if (endpoint->count || endpoint->head || endpoint->tail ||
                endpoint->peer || endpoint->reserved ||
                endpoint->kind!=ZEROOS_IPC_KIND_MESSAGE ||
                endpoint->event_pending) {
                spin_unlock_irqrestore(&ipc_lock,flags);
                return -1;
            }
            continue;
        }
        if (endpoint->kind!=ZEROOS_IPC_KIND_MESSAGE &&
            endpoint->kind!=ZEROOS_IPC_KIND_EVENT) {
            spin_unlock_irqrestore(&ipc_lock,flags);
            return -1;
        }
        if (endpoint->kind==ZEROOS_IPC_KIND_MESSAGE && endpoint->event_pending) {
            spin_unlock_irqrestore(&ipc_lock,flags);
            return -1;
        }
        if (endpoint->kind==ZEROOS_IPC_KIND_EVENT && endpoint->count!=0) {
            spin_unlock_irqrestore(&ipc_lock,flags);
            return -1;
        }
        if (endpoint->count>ZEROOS_IPC_QUEUE_DEPTH ||
            endpoint->head>=ZEROOS_IPC_QUEUE_DEPTH ||
            endpoint->tail>=ZEROOS_IPC_QUEUE_DEPTH ||
            wait_queue_count(&endpoint->send_waiters)>ZEROOS_MAX_TASKS ||
            wait_queue_count(&endpoint->receive_waiters)>ZEROOS_MAX_TASKS ||
            (endpoint->peer && (!endpoint->peer->used ||
                                 endpoint->peer->peer!=endpoint ||
                                 endpoint->peer->kind!=endpoint->kind))) {
            spin_unlock_irqrestore(&ipc_lock,flags);
            return -1;
        }
        for (uint32_t j=0; j<ZEROOS_IPC_MAX_CAPABILITIES; ++j)
            if (capabilities[j].used && capabilities[j].endpoint==endpoint)
                ++refs;
        if (refs!=endpoint->reserved) {
            spin_unlock_irqrestore(&ipc_lock,flags);
            return -1;
        }
        for (uint32_t j=0; j<ZEROOS_IPC_QUEUE_DEPTH; ++j)
            if (endpoint->messages[j].length>ZEROOS_IPC_MAX_MESSAGE) {
                spin_unlock_irqrestore(&ipc_lock,flags);
                return -1;
            }
    }
    for (uint32_t i=0; i<ZEROOS_IPC_MAX_CAPABILITIES; ++i) {
        struct ipc_capability *capability=&capabilities[i];
        if (!capability->used)
            continue;
        if (!process_exists(capability->owner) ||
            !capability->endpoint || !capability->endpoint->used ||
            capability->rights==0 ||
            (capability->rights&~ZEROOS_IPC_ALL_RIGHTS)!=0) {
            spin_unlock_irqrestore(&ipc_lock,flags);
            return -1;
        }
    }
    spin_unlock_irqrestore(&ipc_lock,flags);
    return 0;
}
