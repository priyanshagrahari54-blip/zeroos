#include "page_cache.h"
#include "memory.h"
#include "timer.h"
#include "task.h"

static struct zeroos_page_cache cache;

int page_cache_system_init(void) {
    spinlock_init(&cache.lock);
    wait_queue_init(&cache.eviction_waiters);
    cache.state=ZEROOS_PAGE_CACHE_DORMANT;
    cache.total_pages=0;
    cache.dirty_pages=0;
    cache.clean_pages=0;
    cache.hits=0;
    cache.misses=0;
    cache.evictions=0;
    cache.writebacks=0;
    cache.memory_budget_bytes=64*1024*1024;
    for (uint32_t i=0;i<ZEROOS_PAGE_CACHE_MAX_PAGES;++i) {
        cache.entries[i].used=0;
        cache.entries[i].dirty=0;
        cache.entries[i].referenced=0;
        cache.entries[i].physical=0;
        spinlock_init(&cache.entries[i].lock);
    }
    return 0;
}

static struct zeroos_page_cache_entry *find_entry_locked(uint64_t inode_id, uint64_t offset) {
    for (uint32_t i=0;i<ZEROOS_PAGE_CACHE_MAX_PAGES;++i) {
        if (cache.entries[i].used && cache.entries[i].inode_id==inode_id && cache.entries[i].offset==offset)
            return &cache.entries[i];
    }
    return 0;
}

static struct zeroos_page_cache_entry *find_free_locked(void) {
    for (uint32_t i=0;i<ZEROOS_PAGE_CACHE_MAX_PAGES;++i) {
        if (!cache.entries[i].used) return &cache.entries[i];
    }
    return 0;
}

static struct zeroos_page_cache_entry *find_lru_clean_locked(void) {
    struct zeroos_page_cache_entry *lru=0;
    uint64_t oldest=~0ULL;
    for (uint32_t i=0;i<ZEROOS_PAGE_CACHE_MAX_PAGES;++i) {
        if (!cache.entries[i].used || cache.entries[i].dirty) continue;
        if (cache.entries[i].last_access_ticks<oldest) {
            oldest=cache.entries[i].last_access_ticks;
            lru=&cache.entries[i];
        }
    }
    return lru;
}

int page_cache_lookup(uint64_t inode_id, uint64_t offset, uint64_t *physical_out) {
    if (!physical_out) return -1;
    uint64_t flags=spin_lock_irqsave(&cache.lock);
    struct zeroos_page_cache_entry *e=find_entry_locked(inode_id,offset);
    if (!e) {
        cache.misses++;
        spin_unlock_irqrestore(&cache.lock,flags);
        return -1;
    }
    e->referenced=1;
    e->last_access_ticks=timer_ticks();
    *physical_out=e->physical;
    cache.hits++;
    spin_unlock_irqrestore(&cache.lock,flags);
    return 0;
}

int page_cache_insert(uint64_t inode_id, uint64_t offset, uint64_t physical, uint8_t dirty) {
    if (physical==0) return -1;
    uint64_t flags=spin_lock_irqsave(&cache.lock);
    if (cache.total_pages>=ZEROOS_PAGE_CACHE_MAX_PAGES) {
        /* Evict LRU clean if possible, otherwise throttled */
        struct zeroos_page_cache_entry *lru=find_lru_clean_locked();
        if (!lru) {
            cache.state=ZEROOS_PAGE_CACHE_THROTTLED;
            spin_unlock_irqrestore(&cache.lock,flags);
            return -1;
        }
        lru->used=0;
        lru->physical=0;
        cache.total_pages--;
        cache.clean_pages--;
        cache.evictions++;
    }
    struct zeroos_page_cache_entry *e=find_entry_locked(inode_id,offset);
    if (e) {
        /* Replace */
        uint64_t eflags=spin_lock_irqsave(&e->lock);
        e->physical=physical;
        if (e->dirty && !dirty) cache.dirty_pages--;
        if (!e->dirty && dirty) cache.dirty_pages++;
        e->dirty=dirty;
        e->last_access_ticks=timer_ticks();
        spin_unlock_irqrestore(&e->lock,eflags);
        spin_unlock_irqrestore(&cache.lock,flags);
        return 0;
    }
    e=find_free_locked();
    if (!e) { spin_unlock_irqrestore(&cache.lock,flags); return -1; }
    uint64_t eflags=spin_lock_irqsave(&e->lock);
    e->used=1;
    e->inode_id=inode_id;
    e->offset=offset;
    e->physical=physical;
    e->dirty=dirty;
    e->referenced=1;
    e->last_access_ticks=timer_ticks();
    spin_unlock_irqrestore(&e->lock,eflags);
    cache.total_pages++;
    if (dirty) cache.dirty_pages++;
    else cache.clean_pages++;
    cache.state=ZEROOS_PAGE_CACHE_ACTIVE;
    spin_unlock_irqrestore(&cache.lock,flags);
    return 0;
}

int page_cache_mark_dirty(uint64_t inode_id, uint64_t offset) {
    uint64_t flags=spin_lock_irqsave(&cache.lock);
    struct zeroos_page_cache_entry *e=find_entry_locked(inode_id,offset);
    if (!e) { spin_unlock_irqrestore(&cache.lock,flags); return -1; }
    uint64_t eflags=spin_lock_irqsave(&e->lock);
    if (!e->dirty) {
        e->dirty=1;
        cache.dirty_pages++;
        if (cache.clean_pages) cache.clean_pages--;
    }
    spin_unlock_irqrestore(&e->lock,eflags);
    spin_unlock_irqrestore(&cache.lock,flags);
    return 0;
}

int page_cache_writeback(uint64_t inode_id) {
    uint64_t flags=spin_lock_irqsave(&cache.lock);
    uint64_t written=0;
    for (uint32_t i=0;i<ZEROOS_PAGE_CACHE_MAX_PAGES;++i) {
        if (!cache.entries[i].used || cache.entries[i].inode_id!=inode_id || !cache.entries[i].dirty) continue;
        uint64_t eflags=spin_lock_irqsave(&cache.entries[i].lock);
        /* Simulate writeback: clear dirty, would call block layer in real */
        cache.entries[i].dirty=0;
        cache.dirty_pages--;
        cache.clean_pages++;
        cache.writebacks++;
        written++;
        spin_unlock_irqrestore(&cache.entries[i].lock,eflags);
        if (written>=ZEROOS_PAGE_CACHE_MAX_DIRTY) break; /* bounded writeback */
    }
    spin_unlock_irqrestore(&cache.lock,flags);
    return (int)written;
}

int page_cache_evict(uint64_t inode_id, uint64_t offset) {
    uint64_t flags=spin_lock_irqsave(&cache.lock);
    struct zeroos_page_cache_entry *e=find_entry_locked(inode_id,offset);
    if (!e) { spin_unlock_irqrestore(&cache.lock,flags); return -1; }
    if (e->dirty) { spin_unlock_irqrestore(&cache.lock,flags); return -1; /* must writeback first */ }
    uint64_t eflags=spin_lock_irqsave(&e->lock);
    e->used=0;
    e->physical=0;
    spin_unlock_irqrestore(&e->lock,eflags);
    cache.total_pages--;
    cache.clean_pages--;
    cache.evictions++;
    spin_unlock_irqrestore(&cache.lock,flags);
    return 0;
}

int page_cache_evict_pressure(uint64_t needed_pages) {
    uint64_t flags=spin_lock_irqsave(&cache.lock);
    uint64_t evicted=0;
    while (evicted<needed_pages) {
        struct zeroos_page_cache_entry *lru=find_lru_clean_locked();
        if (!lru) break;
        uint64_t eflags=spin_lock_irqsave(&lru->lock);
        lru->used=0;
        lru->physical=0;
        spin_unlock_irqrestore(&lru->lock,eflags);
        cache.total_pages--;
        cache.clean_pages--;
        cache.evictions++;
        evicted++;
    }
    if (cache.total_pages==0) cache.state=ZEROOS_PAGE_CACHE_DORMANT;
    spin_unlock_irqrestore(&cache.lock,flags);
    return (int)evicted;
}

int page_cache_fsync(uint64_t inode_id) {
    return page_cache_writeback(inode_id);
}

int page_cache_flush_all(void) {
    uint64_t flags=spin_lock_irqsave(&cache.lock);
    uint64_t flushed=0;
    for (uint32_t i=0;i<ZEROOS_PAGE_CACHE_MAX_PAGES;++i) {
        if (!cache.entries[i].used || !cache.entries[i].dirty) continue;
        uint64_t eflags=spin_lock_irqsave(&cache.entries[i].lock);
        cache.entries[i].dirty=0;
        cache.dirty_pages--;
        cache.clean_pages++;
        cache.writebacks++;
        flushed++;
        spin_unlock_irqrestore(&cache.entries[i].lock,eflags);
    }
    cache.state=cache.total_pages ? ZEROOS_PAGE_CACHE_WARM : ZEROOS_PAGE_CACHE_DORMANT;
    spin_unlock_irqrestore(&cache.lock,flags);
    return (int)flushed;
}

int page_cache_stats(uint64_t *total, uint64_t *dirty, uint64_t *hits, uint64_t *misses) {
    if (!total || !dirty || !hits || !misses) return -1;
    uint64_t flags=spin_lock_irqsave(&cache.lock);
    *total=cache.total_pages;
    *dirty=cache.dirty_pages;
    *hits=cache.hits;
    *misses=cache.misses;
    spin_unlock_irqrestore(&cache.lock,flags);
    return 0;
}

int page_cache_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&cache.lock);
    uint64_t total=0, dirty=0, clean=0;
    for (uint32_t i=0;i<ZEROOS_PAGE_CACHE_MAX_PAGES;++i) {
        if (!cache.entries[i].used) continue;
        total++;
        if (cache.entries[i].dirty) dirty++;
        else clean++;
        if (cache.entries[i].physical==0) { spin_unlock_irqrestore(&cache.lock,flags); return -1; }
    }
    if (total!=cache.total_pages || dirty!=cache.dirty_pages || clean!=cache.clean_pages) {
        spin_unlock_irqrestore(&cache.lock,flags);
        return -1;
    }
    spin_unlock_irqrestore(&cache.lock,flags);
    return 0;
}
