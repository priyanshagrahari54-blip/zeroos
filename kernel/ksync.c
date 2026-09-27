#include "ksync.h"
#include "task.h"

void serial_write_public(const char *message);

static void ksync_panic(const char *message) {
    serial_write_public(message);
    for (;;)
        __asm__ volatile ("cli; hlt");
}

void kmutex_init(struct kmutex *mutex, const char *name) {
    spinlock_init(&mutex->lock);
    wait_queue_init(&mutex->waiters);
    mutex->owner=0;
    mutex->locked=0;
    mutex->contended=0;
    mutex->name=name;
}

void kmutex_lock(struct kmutex *mutex) {
    struct task *self=task_current();
    uint64_t flags=spin_lock_irqsave(&mutex->lock);

    if (mutex->locked && mutex->owner==self && self)
        ksync_panic("ZEROOS PANIC: recursive kmutex acquisition.\n");
    while (mutex->locked) {
        uint64_t inner;
        ++mutex->contended;
        if (wait_queue_prepare(&mutex->waiters,&inner)!=0) {
            /* Not blockable (early boot, idle): spin politely instead of
             * failing the lock. */
            spin_unlock_irqrestore(&mutex->lock,flags);
            __asm__ volatile ("pause");
            flags=spin_lock_irqsave(&mutex->lock);
            continue;
        }
        spin_unlock(&mutex->lock);
        (void)wait_queue_commit(flags);
        flags=spin_lock_irqsave(&mutex->lock);
    }
    mutex->locked=1;
    mutex->owner=self;
    spin_unlock_irqrestore(&mutex->lock,flags);
}

int kmutex_trylock(struct kmutex *mutex) {
    uint64_t flags=spin_lock_irqsave(&mutex->lock);
    int acquired=0;
    if (!mutex->locked) {
        mutex->locked=1;
        mutex->owner=task_current();
        acquired=1;
    }
    spin_unlock_irqrestore(&mutex->lock,flags);
    return acquired;
}

void kmutex_unlock(struct kmutex *mutex) {
    uint64_t flags=spin_lock_irqsave(&mutex->lock);
    if (!mutex->locked)
        ksync_panic("ZEROOS PANIC: kmutex unlock while not held.\n");
    mutex->locked=0;
    mutex->owner=0;
    spin_unlock_irqrestore(&mutex->lock,flags);
    (void)wait_queue_wake_one(&mutex->waiters);
}

int kmutex_held(const struct kmutex *mutex) {
    return mutex->locked && mutex->owner==task_current();
}

uint64_t kmutex_contention(const struct kmutex *mutex) {
    return mutex->contended;
}

void kcompletion_init(struct kcompletion *completion) {
    spinlock_init(&completion->lock);
    wait_queue_init(&completion->waiters);
    completion->done=0;
}

void kcompletion_reset(struct kcompletion *completion) {
    uint64_t flags=spin_lock_irqsave(&completion->lock);
    completion->done=0;
    spin_unlock_irqrestore(&completion->lock,flags);
}

void kcompletion_signal(struct kcompletion *completion) {
    uint64_t flags=spin_lock_irqsave(&completion->lock);
    completion->done=1;
    spin_unlock_irqrestore(&completion->lock,flags);
    (void)wait_queue_wake_all(&completion->waiters);
}

void kcompletion_wait(struct kcompletion *completion) {
    uint64_t flags=spin_lock_irqsave(&completion->lock);
    while (!completion->done) {
        uint64_t inner;
        if (wait_queue_prepare(&completion->waiters,&inner)!=0) {
            spin_unlock_irqrestore(&completion->lock,flags);
            __asm__ volatile ("pause");
            flags=spin_lock_irqsave(&completion->lock);
            continue;
        }
        spin_unlock(&completion->lock);
        (void)wait_queue_commit(flags);
        flags=spin_lock_irqsave(&completion->lock);
    }
    spin_unlock_irqrestore(&completion->lock,flags);
}

int kcompletion_done(struct kcompletion *completion) {
    uint64_t flags=spin_lock_irqsave(&completion->lock);
    int done=(int)completion->done;
    spin_unlock_irqrestore(&completion->lock,flags);
    return done;
}
