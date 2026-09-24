#ifndef ZEROOS_KSYNC_H
#define ZEROOS_KSYNC_H
#include "types.h"
#include "sync.h"
#include "wait.h"

struct task;

/*
 * Sleeping synchronization for task context (never IRQ context).
 *
 * kmutex: non-recursive, owner-tracked, FIFO wait queue. Blocking uses the
 * wait_queue prepare/commit protocol under the mutex's internal spinlock, so
 * an unlock racing with a new waiter cannot lose the wakeup. Holders may
 * block (I/O, other kmutexes) but must follow the documented storage lock
 * order (docs/STORAGE.md "Lock order").
 *
 * kcompletion: one-shot or re-armable event with a monotonically increasing
 * completion count. Signalling is IRQ-safe (block-request completion runs in
 * device interrupt context); waiting is task-context only.
 */
struct kmutex {
    struct spinlock lock;
    struct wait_queue waiters;
    struct task *owner;
    uint32_t locked;
    uint64_t contended;
    const char *name;
};

struct kcompletion {
    struct spinlock lock;
    struct wait_queue waiters;
    uint32_t done;
};

void kmutex_init(struct kmutex *mutex, const char *name);
void kmutex_lock(struct kmutex *mutex);
int kmutex_trylock(struct kmutex *mutex);
void kmutex_unlock(struct kmutex *mutex);
int kmutex_held(const struct kmutex *mutex);
uint64_t kmutex_contention(const struct kmutex *mutex);

void kcompletion_init(struct kcompletion *completion);
void kcompletion_reset(struct kcompletion *completion);
void kcompletion_signal(struct kcompletion *completion);
void kcompletion_wait(struct kcompletion *completion);
int kcompletion_done(struct kcompletion *completion);

#endif
