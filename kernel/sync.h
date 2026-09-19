#ifndef ZEROOS_SYNC_H
#define ZEROOS_SYNC_H
#include "types.h"

struct spinlock { volatile uint32_t value; };
struct atomic_u64 { volatile uint64_t value; };

void spinlock_init(struct spinlock *lock);
void spin_lock(struct spinlock *lock);
void spin_unlock(struct spinlock *lock);
uint64_t spin_lock_irqsave(struct spinlock *lock);
void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags);

void atomic_u64_init(struct atomic_u64 *value, uint64_t initial);
uint64_t atomic_u64_load(const struct atomic_u64 *value);
void atomic_u64_store(struct atomic_u64 *value, uint64_t new_value);
uint64_t atomic_u64_fetch_add(struct atomic_u64 *value, uint64_t amount);
uint64_t atomic_u64_fetch_sub(struct atomic_u64 *value, uint64_t amount);

#endif
