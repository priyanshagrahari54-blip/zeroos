#ifndef ZEROOS_PAGECACHE_H
#define ZEROOS_PAGECACHE_H
#include "block.h"

/*
 * Bounded file-data page cache.
 *
 * - Pool: at most pc_limit pages (min(PC_MAX_PAGES, free/4) at init,
 *   adjustable for pressure tests). Pages are 4 KiB = one filesystem block.
 * - Lookup: hash on (mapping, index); per-mapping page list for writeback,
 *   fsync and invalidation; one global LRU for eviction.
 * - Locking: pc_lock (irqsave spinlock) covers all metadata; waiting for
 *   fill/writeback uses a condition loop on pc_waitq prepared under pc_lock.
 *   Page *contents* are serialized by the owning inode's mutex (VFS).
 * - Block mapping is cached per page (page->block) when the filesystem
 *   maps or allocates it, so writeback never re-enters the filesystem
 *   (no fs-lock recursion from commit/eviction paths).
 * - Dirty control: global dirty limit (25% of pool) throttles writers into
 *   synchronous writeback of their own mapping; the flusher writes back
 *   pages older than PC_DIRTY_EXPIRE ticks or above the 10% background
 *   threshold, at most PC_FLUSH_BATCH pages per pass, BACKGROUND priority,
 *   and sleeps on an event when nothing is dirty.
 * - Errors: a failed writeback marks the page ERROR and bumps the mapping's
 *   error sequence (reported once per file description by fsync). The page
 *   stays dirty for PC_WB_RETRIES attempts; after that the data is dropped
 *   from the cache but the mapping error remains sticky (never silent).
 * - mmap: pinned pages are never evicted or invalidated.
 */
#define PC_MAX_PAGES 4096U
#define PC_HASH_BUCKETS 1024U
#define PC_DIRTY_EXPIRE 300U
#define PC_FLUSH_BATCH 64U
#define PC_WB_RETRIES 3U

#define PC_VALID     (1U << 0)
#define PC_DIRTY     (1U << 1)
#define PC_WRITEBACK (1U << 2)
#define PC_ERROR     (1U << 3)
#define PC_LOCKED    (1U << 4)
#define PC_REFERENCED (1U << 5)

struct pc_mapping;

struct pc_page {
    struct pc_mapping *mapping;
    uint64_t index;
    uint64_t physical;
    uint64_t block;                 /* filesystem block, 0 = hole/unmapped */
    uint32_t refcount;
    uint32_t pins;
    uint32_t flags;
    uint32_t wb_retries;
    int32_t error;
    uint32_t reserved;
    uint64_t dirty_tick;
    struct pc_page *hash_next;
    struct pc_page *lru_prev, *lru_next;
    struct pc_page *map_prev, *map_next;
};

struct pc_mapping {
    struct block_device *device;    /* partition device */
    uint32_t sectors_per_block;
    uint32_t in_use;
    struct pc_page *pages;
    uint64_t nrpages;
    uint64_t ndirty;
    uint64_t nwriteback;
    uint32_t error_seq;             /* bumped on each writeback failure */
    int32_t error;                  /* last writeback error */
    void *owner;
};

struct pc_stats {
    uint64_t limit;
    uint64_t pages;
    uint64_t dirty;
    uint64_t writeback;
    uint64_t hits;
    uint64_t misses;
    uint64_t reads;
    uint64_t evictions;
    uint64_t writebacks;
    uint64_t writeback_errors;
    uint64_t dropped_after_errors;
    uint64_t throttled;
    uint64_t flusher_passes;
    uint64_t flusher_wakeups;
    uint64_t alloc_failures;
    uint64_t pinned;
};

int pc_init(void);
void pc_mapping_init(struct pc_mapping *mapping, struct block_device *device,
                     void *owner);
/* Find or create page `index`. `block` (0 = hole) is the caller's mapping;
 * `fill`: 1 read from disk if not valid, 0 caller overwrites the whole
 * page (zero-filled). Returns a referenced, VALID page or -SE_*. */
int pc_get(struct pc_mapping *mapping, uint64_t index, uint64_t block, int fill,
           uint8_t priority, struct pc_page **out);
/* Lookup only (no I/O, no allocation). */
struct pc_page *pc_find(struct pc_mapping *mapping, uint64_t index);
void pc_put(struct pc_page *page);
void pc_mark_dirty(struct pc_page *page, uint64_t block);
void pc_pin(struct pc_page *page);
void pc_unpin(struct pc_page *page);
static inline uint8_t *pc_data(struct pc_page *page) {
    return (uint8_t *)page->physical;
}
/* Write back dirty pages (all if max==0). wait: block until this pass's
 * writeback finished. Returns first error of this pass (or 0). */
int pc_writeback_mapping(struct pc_mapping *mapping, int wait, uint8_t priority);
/* Remove pages with index >= from. Dirty data is discarded (caller has
 * made it irrelevant, e.g. truncate/unlink). -SE_BUSY if pinned. */
int pc_invalidate(struct pc_mapping *mapping, uint64_t from);
/* Global: write back everything (sync/unmount). */
int pc_sync_all(uint8_t priority);
/* Throttle writers above the dirty limit. */
void pc_balance_dirty(struct pc_mapping *mapping);
void pc_set_limit(uint64_t pages);
uint64_t pc_shrink(uint64_t target);
void pc_stats_snapshot(struct pc_stats *out);
void pc_flusher_start(void);
int pc_mapping_has_pins(struct pc_mapping *mapping);

#endif
