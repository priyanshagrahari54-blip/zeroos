#ifndef ZEROOS_WAIT_H
#define ZEROOS_WAIT_H
#include "types.h"
#include "sync.h"

struct task;

struct wait_queue {
    struct spinlock lock;
    struct task *head;
    struct task *tail;
};

void wait_queue_init(struct wait_queue *queue);
/* Publish the current task as blocked while interrupts remain disabled. The
 * caller may hold an outer resource lock while preparing the waiter, then
 * releases that lock before committing the context switch. */
int wait_queue_prepare(struct wait_queue *queue, uint64_t *flags_out);
int wait_queue_commit(uint64_t flags);
/* Commit a prepared waiter with a scheduler deadline. The caller must
 * recheck the condition after return; timeout and wake are both resumptions. */
int wait_queue_commit_until(uint64_t flags, uint64_t deadline);
/* Remove the current task after a timed wait if the wake path did not already
 * dequeue it. Safe to call after either timeout or signal wake. */
int wait_queue_remove_current(struct wait_queue *queue);
int wait_queue_block(struct wait_queue *queue);
uint64_t wait_queue_wake_one(struct wait_queue *queue);
uint64_t wait_queue_wake_all(struct wait_queue *queue);
uint64_t wait_queue_count(struct wait_queue *queue);

#endif
