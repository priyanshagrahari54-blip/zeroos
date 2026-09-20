#ifndef ZEROOS_HEAP_H
#define ZEROOS_HEAP_H
#include "types.h"

/*
 * ZEROOS kernel heap.
 *
 * A first-fit block allocator over one contiguous region of physical RAM.
 * The region is identity-mapped, so allocation returns addresses that are
 * directly usable by kernel code.
 *
 * Invariants (checked by heap_validate()):
 *   - every block is 16-byte aligned from the region start;
 *   - block sizes are 16-byte multiples and cover the region exactly;
 *   - in-use blocks carry a valid header magic and an intact canary;
 *   - free blocks carry the free magic.
 *
 * Known limitation (Stage 1): coalescing is forward-only; freeing a block
 * only merges with the block that physically follows it. This is documented
 * and exercised by the self-test; a full buddy/region allocator is deferred
 * until a driver or storage layer needs it.
 */

#define ZEROOS_HEAP_ALIGN 16ULL
#define ZEROOS_HEAP_MIN_ALLOC 32ULL

int heap_init(void);
void *kmalloc(uint64_t size);
void *kcalloc(uint64_t count, uint64_t size);
int kfree(void *pointer);
uint64_t heap_used_bytes(void);
uint64_t heap_capacity_bytes(void);
int heap_validate(void);

#endif
