#include <stdio.h>
#include <string.h>
#include "wait.h"
#include "task.h"

static unsigned checks;
static unsigned failures;

#define CHECK(condition, name) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr,"FAIL: %s\n",name); \
    } \
} while (0)

/* wait_queue_count only needs lock stubs in this host boundary test. */
void spinlock_init(struct spinlock *lock) { memset(lock,0,sizeof(*lock)); }
uint64_t spin_lock_irqsave(struct spinlock *lock) { (void)lock; return 0; }
void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags) {
    (void)lock; (void)flags;
}
void spin_unlock(struct spinlock *lock) { (void)lock; }
struct task *task_current(void) { return 0; }
int task_prepare_block(void) { return -1; }
int task_block_irqsave(uint64_t flags) { (void)flags; return -1; }
int task_wake(struct task *task) { (void)task; return -1; }
void serial_write_public(const char *text) { (void)text; }

int main(void) {
    struct wait_queue queue;
    struct task tasks[ZEROOS_MAX_TASKS];
    wait_queue_init(&queue);

    CHECK(wait_queue_count(&queue)==0,"empty queue count");

    memset(tasks,0,sizeof(tasks));
    queue.head=&tasks[0];
    tasks[0].wait_next=&tasks[1];
    tasks[1].wait_next=&tasks[2];
    CHECK(wait_queue_count(&queue)==3,"acyclic queue count");

    for (uint32_t i=0;i<ZEROOS_MAX_TASKS;++i)
        tasks[i].wait_next=(i+1U<ZEROOS_MAX_TASKS) ? &tasks[i+1U] : 0;
    queue.head=&tasks[0];
    CHECK(wait_queue_count(&queue)==ZEROOS_MAX_TASKS,
          "maximum-capacity queue count");

    tasks[ZEROOS_MAX_TASKS-1U].wait_next=&tasks[0];
    CHECK(wait_queue_count(&queue)==ZEROOS_MAX_TASKS+1U,
          "cyclic queue is bounded and signaled as invalid");

    printf("wait_queue_test: checks=%u failures=%u\n",checks,failures);
    return failures ? 1 : 0;
}
