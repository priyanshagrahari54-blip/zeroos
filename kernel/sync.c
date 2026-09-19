#include "sync.h"

static inline uint64_t read_rflags(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags) : : "memory");
    return flags;
}

static inline void disable_interrupts(void) {
    __asm__ volatile ("cli" : : : "memory");
}

static inline void restore_interrupts(uint64_t flags) {
    if (flags & (1ULL << 9))
        __asm__ volatile ("sti" : : : "memory");
    else
        __asm__ volatile ("cli" : : : "memory");
}

static inline void cpu_relax(void) {
    __asm__ volatile ("pause" : : : "memory");
}

void spinlock_init(struct spinlock *lock) {
    lock->value = 0;
}

void spin_lock(struct spinlock *lock) {
    for (;;) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(&lock->value, &expected, 1, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return;
        while (__atomic_load_n(&lock->value, __ATOMIC_RELAXED))
            cpu_relax();
    }
}

void spin_unlock(struct spinlock *lock) {
    __atomic_store_n(&lock->value, 0, __ATOMIC_RELEASE);
}

uint64_t spin_lock_irqsave(struct spinlock *lock) {
    uint64_t flags = read_rflags();
    disable_interrupts();
    spin_lock(lock);
    return flags;
}

void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags) {
    spin_unlock(lock);
    restore_interrupts(flags);
}

void atomic_u64_init(struct atomic_u64 *value, uint64_t initial) {
    __atomic_store_n(&value->value, initial, __ATOMIC_RELAXED);
}

uint64_t atomic_u64_load(const struct atomic_u64 *value) {
    return __atomic_load_n(&value->value, __ATOMIC_ACQUIRE);
}

void atomic_u64_store(struct atomic_u64 *value, uint64_t new_value) {
    __atomic_store_n(&value->value, new_value, __ATOMIC_RELEASE);
}

uint64_t atomic_u64_fetch_add(struct atomic_u64 *value, uint64_t amount) {
    return __atomic_fetch_add(&value->value, amount, __ATOMIC_ACQ_REL);
}

uint64_t atomic_u64_fetch_sub(struct atomic_u64 *value, uint64_t amount) {
    return __atomic_fetch_sub(&value->value, amount, __ATOMIC_ACQ_REL);
}
