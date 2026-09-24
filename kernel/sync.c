#include "sync.h"

static inline uint64_t read_rflags(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags) : : "memory");
    return flags;
}

static inline void disable_interrupts(void) {
    __asm__ volatile("cli" : : : "memory");
}

static inline void restore_interrupts(uint64_t flags) {
    if (flags & (1ULL<<9))
        __asm__ volatile("sti" : : : "memory");
    else
        __asm__ volatile("cli" : : : "memory");
}

static inline void cpu_relax(void) {
    __asm__ volatile("pause" : : : "memory");
}

void spinlock_init(struct spinlock *lock) {
    if (!lock) return;
    lock->value=0;
    lock->contention=0;
}

int spin_try_lock(struct spinlock *lock) {
    uint32_t expected=0;
    if (!lock) return -1;
    if (__atomic_compare_exchange_n(&lock->value,&expected,1,0,
                                    __ATOMIC_ACQUIRE,__ATOMIC_RELAXED))
        return 0;
    __atomic_fetch_add(&lock->contention,1,__ATOMIC_RELAXED);
    return -1;
}

void spin_lock(struct spinlock *lock) {
    if (!lock) return;
    for (;;) {
        if (spin_try_lock(lock)==0)
            return;
        while (__atomic_load_n(&lock->value,__ATOMIC_RELAXED))
            cpu_relax();
    }
}

int spin_lock_bounded(struct spinlock *lock, uint64_t iterations) {
    if (!lock) return -1;
    for (uint64_t i=0; i<iterations; ++i) {
        if (spin_try_lock(lock)==0)
            return 0;
        cpu_relax();
    }
    return -1;
}

void spin_unlock(struct spinlock *lock) {
    if (!lock) return;
    __atomic_store_n(&lock->value,0,__ATOMIC_RELEASE);
}

uint64_t spinlock_contention_count(const struct spinlock *lock) {
    if (!lock) return 0;
    return __atomic_load_n(&lock->contention,__ATOMIC_RELAXED);
}

uint64_t spin_lock_irqsave(struct spinlock *lock) {
    uint64_t flags=read_rflags();
    disable_interrupts();
    spin_lock(lock);
    return flags;
}

void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags) {
    spin_unlock(lock);
    restore_interrupts(flags);
}

void rwlock_init(struct rwlock *lock) {
    if (!lock) return;
    spinlock_init(&lock->guard);
    lock->readers=0;
    lock->waiting_writers=0;
    lock->writer=0;
}

void rwlock_read_lock(struct rwlock *lock) {
    if (!lock) return;
    for (;;) {
        spin_lock(&lock->guard);
        if (!lock->writer && lock->waiting_writers==0) {
            ++lock->readers;
            spin_unlock(&lock->guard);
            return;
        }
        spin_unlock(&lock->guard);
        cpu_relax();
    }
}

void rwlock_read_unlock(struct rwlock *lock) {
    if (!lock) return;
    spin_lock(&lock->guard);
    if (lock->readers)
        --lock->readers;
    spin_unlock(&lock->guard);
}

void rwlock_write_lock(struct rwlock *lock) {
    if (!lock) return;
    spin_lock(&lock->guard);
    ++lock->waiting_writers;
    spin_unlock(&lock->guard);

    for (;;) {
        spin_lock(&lock->guard);
        if (!lock->writer && lock->readers==0) {
            lock->writer=1;
            --lock->waiting_writers;
            spin_unlock(&lock->guard);
            return;
        }
        spin_unlock(&lock->guard);
        cpu_relax();
    }
}

int rwlock_try_write(struct rwlock *lock) {
    if (!lock) return -1;
    if (spin_try_lock(&lock->guard)!=0)
        return -1;
    if (lock->writer || lock->readers!=0) {
        spin_unlock(&lock->guard);
        return -1;
    }
    lock->writer=1;
    spin_unlock(&lock->guard);
    return 0;
}

void rwlock_write_unlock(struct rwlock *lock) {
    if (!lock) return;
    spin_lock(&lock->guard);
    lock->writer=0;
    spin_unlock(&lock->guard);
}

void atomic_u64_init(struct atomic_u64 *value, uint64_t initial) {
    if (value) __atomic_store_n(&value->value,initial,__ATOMIC_RELAXED);
}

uint64_t atomic_u64_load(const struct atomic_u64 *value) {
    return value ? __atomic_load_n(&value->value,__ATOMIC_ACQUIRE) : 0;
}

void atomic_u64_store(struct atomic_u64 *value, uint64_t new_value) {
    if (value) __atomic_store_n(&value->value,new_value,__ATOMIC_RELEASE);
}

uint64_t atomic_u64_fetch_add(struct atomic_u64 *value, uint64_t amount) {
    return value ? __atomic_fetch_add(&value->value,amount,__ATOMIC_ACQ_REL) : 0;
}

uint64_t atomic_u64_fetch_sub(struct atomic_u64 *value, uint64_t amount) {
    return value ? __atomic_fetch_sub(&value->value,amount,__ATOMIC_ACQ_REL) : 0;
}
