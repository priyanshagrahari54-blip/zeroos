#ifndef ZEROOS_PAGE_CACHE_H
#define ZEROOS_PAGE_CACHE_H

#include "types.h"
#include "sync.h"
#include "wait.h"

#define ZEROOS_PAGE_CACHE_MAX_PAGES 1024U
#define ZEROOS_PAGE_CACHE_MAX_DIRTY 256U

enum zeroos_page_cache_state {
    ZEROOS_PAGE_CACHE_STOPPED = 0,
    ZEROOS_PAGE_CACHE_DORMANT,
    ZEROOS_PAGE_CACHE_WARM,
    ZEROOS_PAGE_CACHE_ACTIVE,
    ZEROOS_PAGE_CACHE_THROTTLED,
    ZEROOS_PAGE_CACHE_SUSPENDED
};

struct zeroos_page_cache_entry {
    uint64_t inode_id;
    uint64_t offset; /* file offset / page size */
    uint64_t physical;
    uint8_t used;
    uint8_t dirty;
    uint8_t referenced;
    uint64_t last_access_ticks;
    struct spinlock lock;
};

struct zeroos_page_cache {
    enum zeroos_page_cache_state state;
    uint64_t total_pages;
    uint64_t dirty_pages;
    uint64_t clean_pages;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    uint64_t writebacks;
    uint64_t memory_budget_bytes;
    struct spinlock lock;
    struct zeroos_page_cache_entry entries[ZEROOS_PAGE_CACHE_MAX_PAGES];
    struct wait_queue eviction_waiters;
};

int page_cache_system_init(void);
int page_cache_lookup(uint64_t inode_id, uint64_t offset, uint64_t *physical_out);
int page_cache_insert(uint64_t inode_id, uint64_t offset, uint64_t physical, uint8_t dirty);
int page_cache_mark_dirty(uint64_t inode_id, uint64_t offset);
int page_cache_writeback(uint64_t inode_id);
int page_cache_evict(uint64_t inode_id, uint64_t offset);
int page_cache_evict_pressure(uint64_t needed_pages);
int page_cache_fsync(uint64_t inode_id);
int page_cache_flush_all(void);
int page_cache_stats(uint64_t *total, uint64_t *dirty, uint64_t *hits, uint64_t *misses);
int page_cache_debug_validate(void);

#endif
