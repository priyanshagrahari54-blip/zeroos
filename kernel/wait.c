#include "wait.h"
#include "task.h"

static void wait_queue_push_locked(struct wait_queue *queue, struct task *task) {
    task->wait_next=0;
    task->wait_queue=queue;

    if (queue->tail)
        queue->tail->wait_next=task;
    else
        queue->head=task;
    queue->tail=task;
}

static struct task *wait_queue_pop_locked(struct wait_queue *queue) {
    struct task *task=queue->head;
    if (!task) return 0;

    queue->head=task->wait_next;
    if (!queue->head) queue->tail=0;

    task->wait_next=0;
    task->wait_queue=0;
    return task;
}

void wait_queue_init(struct wait_queue *queue) {
    if (!queue) return;
    spinlock_init(&queue->lock);
    queue->head=0;
    queue->tail=0;
}

int wait_queue_prepare(struct wait_queue *queue, uint64_t *flags_out) {
    struct task *task;
    uint64_t flags;

    if (!queue || !flags_out)
        return -1;
    task=task_current();
    if (!task)
        return -1;

    flags=spin_lock_irqsave(&queue->lock);
    if (task->state!=TASK_RUNNING || task->wait_queue ||
        task->sleep_armed || task_prepare_block()!=0) {
        spin_unlock_irqrestore(&queue->lock,flags);
        return -1;
    }

    /* Leave interrupts disabled for the caller. This closes the lost-wakeup
     * window between publishing the waiter and releasing the condition lock. */
    wait_queue_push_locked(queue,task);
    spin_unlock(&queue->lock);
    *flags_out=flags;
    return 0;
}

int wait_queue_commit(uint64_t flags) {
    return task_block_irqsave(flags);
}

int wait_queue_block(struct wait_queue *queue) {
    uint64_t flags;
    if (wait_queue_prepare(queue,&flags)!=0)
        return -1;
    return wait_queue_commit(flags);
}

uint64_t wait_queue_wake_one(struct wait_queue *queue) {
    struct task *task;
    uint64_t flags;

    if (!queue) return 0;

    flags=spin_lock_irqsave(&queue->lock);
    task=wait_queue_pop_locked(queue);
    if (task && task_wake(task)!=0)
        task=0;
    spin_unlock_irqrestore(&queue->lock,flags);
    return task ? 1 : 0;
}

uint64_t wait_queue_wake_all(struct wait_queue *queue) {
    uint64_t count=0;
    uint64_t flags;

    if (!queue) return 0;

    flags=spin_lock_irqsave(&queue->lock);
    while (queue->head) {
        struct task *task=wait_queue_pop_locked(queue);
        if (task && task_wake(task)==0)
            ++count;
    }
    spin_unlock_irqrestore(&queue->lock,flags);
    return count;
}

uint64_t wait_queue_count(struct wait_queue *queue) {
    uint64_t count=0;
    struct task *task;
    uint64_t flags;

    if (!queue) return 0;

    flags=spin_lock_irqsave(&queue->lock);
    for (task=queue->head;task;task=task->wait_next)
        ++count;
    spin_unlock_irqrestore(&queue->lock,flags);
    return count;
}
