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
int wait_queue_block(struct wait_queue *queue);
uint64_t wait_queue_wake_one(struct wait_queue *queue);
uint64_t wait_queue_wake_all(struct wait_queue *queue);
uint64_t wait_queue_count(struct wait_queue *queue);

#endif
