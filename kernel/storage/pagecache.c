#include "pagecache.h"
#include "../kstring.h"
#include "../memory.h"
#include "../task.h"
#include "../timer.h"
#include "../wait.h"

static struct pc_page pc_pages[PC_MAX_PAGES];
static struct pc_page *pc_hash[PC_HASH_BUCKETS];
static struct pc_page *pc_free_structs;
static struct pc_page *lru_head, *lru_tail;       /* head = most recent */
static struct spinlock pc_lock;
static struct wait_queue pc_waitq;
static struct kcompletion pc_flusher_event;
static uint64_t pc_limit;
static uint64_t pc_page_count;
static uint64_t pc_dirty_count;
static uint64_t pc_writeback_count;
static struct pc_stats pc_counters;
static uint64_t pc_flusher_task;
static int pc_ready;

static uint32_t pc_hash_of(struct pc_mapping *mapping, uint64_t index) {
    uint64_t key=((uint64_t)mapping>>4)^(index*0x9e3779b97f4a7c15ULL);
    return (uint32_t)((key^(key>>29))%PC_HASH_BUCKETS);
}

int pc_init(void) {
    if (pc_ready)
        return 0;
    spinlock_init(&pc_lock);
    wait_queue_init(&pc_waitq);
    kcompletion_init(&pc_flusher_event);
    for (uint32_t i=PC_MAX_PAGES; i>0; --i) {
        pc_pages[i-1U].hash_next=pc_free_structs;
        pc_free_structs=&pc_pages[i-1U];
    }
    uint64_t budget=memory_free_pages()/4ULL;
    pc_limit=budget<PC_MAX_PAGES ? budget : PC_MAX_PAGES;
    if (pc_limit<64ULL)
        pc_limit=64ULL;
    pc_ready=1;
    klog("ZEROOS: page cache initialized: limit=%llu pages (%llu KiB), dirty_limit=%llu, bg_threshold=%llu.",
         pc_limit,pc_limit*4ULL,pc_limit/4ULL,pc_limit/10ULL);
    return 0;
}

void pc_mapping_init(struct pc_mapping *mapping, struct block_device *device,
                     void *owner) {
    memset(mapping,0,sizeof(*mapping));
    mapping->device=device;
    mapping->sectors_per_block=4096U/block_root(device)->sector_size;
    mapping->owner=owner;
    mapping->in_use=1;
}

/* ------------------------------------------------------ list helpers */

static void lru_remove(struct pc_page *p) {
    if (p->lru_prev) p->lru_prev->lru_next=p->lru_next; else lru_head=p->lru_next;
    if (p->lru_next) p->lru_next->lru_prev=p->lru_prev; else lru_tail=p->lru_prev;
    p->lru_prev=p->lru_next=0;
}

static void lru_push_front(struct pc_page *p) {
    p->lru_prev=0;
    p->lru_next=lru_head;
    if (lru_head) lru_head->lru_prev=p; else lru_tail=p;
    lru_head=p;
}

static void hash_insert(struct pc_page *p) {
    uint32_t bucket=pc_hash_of(p->mapping,p->index);
    p->hash_next=pc_hash[bucket];
    pc_hash[bucket]=p;
}

static void hash_remove(struct pc_page *p) {
    struct pc_page **cursor=&pc_hash[pc_hash_of(p->mapping,p->index)];
    while (*cursor && *cursor!=p)
        cursor=&(*cursor)->hash_next;
    if (*cursor)
        *cursor=p->hash_next;
    p->hash_next=0;
}

static void map_insert(struct pc_page *p) {
    struct pc_mapping *m=p->mapping;
    p->map_prev=0;
    p->map_next=m->pages;
    if (m->pages) m->pages->map_prev=p;
    m->pages=p;
    ++m->nrpages;
}

static void map_remove(struct pc_page *p) {
    struct pc_mapping *m=p->mapping;
    if (p->map_prev) p->map_prev->map_next=p->map_next; else m->pages=p->map_next;
    if (p->map_next) p->map_next->map_prev=p->map_prev;
    p->map_prev=p->map_next=0;
    --m->nrpages;
}

static struct pc_page *lookup_locked(struct pc_mapping *mapping, uint64_t index) {
    for (struct pc_page *p=pc_hash[pc_hash_of(mapping,index)]; p; p=p->hash_next)
        if (p->mapping==mapping && p->index==index)
            return p;
    return 0;
}

/* Detach a page from all indexes and return its struct to the free list;
 * the physical frame is returned to `*frame` for reuse or release. */
static void page_forget_locked(struct pc_page *p, uint64_t *frame) {
    if (p->flags&PC_DIRTY) {
        --pc_dirty_count;
        --p->mapping->ndirty;
    }
    hash_remove(p);
    lru_remove(p);
    map_remove(p);
    *frame=p->physical;
    memset(p,0,sizeof(*p));
    p->hash_next=pc_free_structs;
    pc_free_structs=p;
    --pc_page_count;
}

/* Evict the least recently used clean, unreferenced, unpinned page. */
static int evict_one_locked(uint64_t *frame) {
    for (struct pc_page *p=lru_tail; p; p=p->lru_prev) {
        if (p->refcount || p->pins ||
            (p->flags&(PC_DIRTY|PC_WRITEBACK|PC_LOCKED)))
            continue;
        if (p->flags&PC_REFERENCED) {
            /* Second chance. */
            p->flags&=~PC_REFERENCED;
            continue;
        }
        page_forget_locked(p,frame);
        ++pc_counters.evictions;
        return 1;
    }
    /* Everything had its referenced bit: second pass without it. */
    for (struct pc_page *p=lru_tail; p; p=p->lru_prev) {
        if (p->refcount || p->pins ||
            (p->flags&(PC_DIRTY|PC_WRITEBACK|PC_LOCKED)))
            continue;
        page_forget_locked(p,frame);
        ++pc_counters.evictions;
        return 1;
    }
    return 0;
}

static void pc_wait_locked(uint64_t *flags) {
    uint64_t inner;
    if (wait_queue_prepare(&pc_waitq,&inner)!=0) {
        spin_unlock_irqrestore(&pc_lock,*flags);
        task_yield();
        *flags=spin_lock_irqsave(&pc_lock);
        return;
    }
    spin_unlock(&pc_lock);
    (void)wait_queue_commit(*flags);
    *flags=spin_lock_irqsave(&pc_lock);
}

/* ------------------------------------------------------------ writeback */

static void pc_writeback_done(struct block_request *request) {
    struct pc_page *p=(struct pc_page *)request->private_data;
    int status=request->status;
    uint64_t flags=spin_lock_irqsave(&pc_lock);
    struct pc_mapping *m=p->mapping;
    p->flags&=~PC_WRITEBACK;
    --pc_writeback_count;
    --m->nwriteback;
    if (status) {
        p->flags|=PC_ERROR;
        p->error=status;
        ++p->wb_retries;
        m->error=status;
        ++m->error_seq;
        ++pc_counters.writeback_errors;
        if (p->wb_retries<PC_WB_RETRIES) {
            if (!(p->flags&PC_DIRTY)) {
                p->flags|=PC_DIRTY;
                ++pc_dirty_count;
                ++m->ndirty;
            }
        } else {
            ++pc_counters.dropped_after_errors;
        }
    } else {
        p->flags&=~PC_ERROR;
        p->wb_retries=0;
    }
    --p->refcount;
    spin_unlock_irqrestore(&pc_lock,flags);
    block_request_free(request);
    (void)wait_queue_wake_all(&pc_waitq);
}

/* Collect up to `max` dirty pages of `m` (all mappings when m==0) that are
 * eligible now, mark them WRITEBACK and submit. Returns number submitted. */
static uint32_t pc_start_writeback(struct pc_mapping *m, uint32_t max,
                                   uint64_t older_than, uint8_t priority) {
    struct pc_page *batch[32];
    uint32_t total=0;
    for (;;) {
        uint32_t count=0;
        uint64_t flags=spin_lock_irqsave(&pc_lock);
        struct pc_page *start=m ? m->pages : lru_tail;
        for (struct pc_page *p=start; p && count<32U && total+count<max;
             p=m ? p->map_next : p->lru_prev) {
            if ((p->flags&(PC_DIRTY|PC_WRITEBACK))!=PC_DIRTY)
                continue;
            if (p->block==0)
                continue;       /* unmapped dirty page: filesystem bug guard */
            if (older_than && p->dirty_tick>older_than)
                continue;
            p->flags&=~PC_DIRTY;
            p->flags|=PC_WRITEBACK;
            --pc_dirty_count;
            --p->mapping->ndirty;
            ++pc_writeback_count;
            ++p->mapping->nwriteback;
            ++p->refcount;
            ++pc_counters.writebacks;
            batch[count++]=p;
        }
        spin_unlock_irqrestore(&pc_lock,flags);
        for (uint32_t i=0; i<count; ++i) {
            struct pc_mapping *pm=batch[i]->mapping;
            struct block_request *request=block_request_alloc(pm->device,priority,1);
            if (!request) {
                /* Cannot happen with wait=1; keep the page dirty. */
                flags=spin_lock_irqsave(&pc_lock);
                batch[i]->flags&=~PC_WRITEBACK;
                batch[i]->flags|=PC_DIRTY;
                --pc_writeback_count;
                --pm->nwriteback;
                ++pc_dirty_count;
                ++pm->ndirty;
                --batch[i]->refcount;
                spin_unlock_irqrestore(&pc_lock,flags);
                continue;
            }
            request->op=BLOCK_OP_WRITE;
            request->lba=batch[i]->block*pm->sectors_per_block;
            request->done=pc_writeback_done;
            request->private_data=batch[i];
            (void)block_request_add_buffer(request,batch[i]->physical,4096);
            block_submit(pm->device,request);
        }
        total+=count;
        if (count<32U || total>=max)
            return total;
    }
}

int pc_writeback_mapping(struct pc_mapping *mapping, int wait, uint8_t priority) {
    uint32_t seq_before;
    uint64_t flags=spin_lock_irqsave(&pc_lock);
    seq_before=mapping->error_seq;
    spin_unlock_irqrestore(&pc_lock,flags);
    (void)pc_start_writeback(mapping,~0U,0,priority);
    if (!wait)
        return 0;
    flags=spin_lock_irqsave(&pc_lock);
    while (mapping->nwriteback)
        pc_wait_locked(&flags);
    int rc=mapping->error_seq!=seq_before ? mapping->error : 0;
    spin_unlock_irqrestore(&pc_lock,flags);
    return rc;
}

int pc_sync_all(uint8_t priority) {
    uint64_t errors_before=pc_counters.writeback_errors;
    (void)pc_start_writeback(0,~0U,0,priority);
    uint64_t flags=spin_lock_irqsave(&pc_lock);
    while (pc_writeback_count)
        pc_wait_locked(&flags);
    int rc=pc_counters.writeback_errors!=errors_before ? -SE_IO : 0;
    spin_unlock_irqrestore(&pc_lock,flags);
    return rc;
}

void pc_balance_dirty(struct pc_mapping *mapping) {
    uint64_t limit=pc_limit/4ULL;
    if (pc_dirty_count+pc_writeback_count<=limit) {
        if (pc_dirty_count>pc_limit/10ULL)
            kcompletion_signal(&pc_flusher_event);
        return;
    }
    __atomic_fetch_add(&pc_counters.throttled,1ULL,__ATOMIC_RELAXED);
    (void)pc_writeback_mapping(mapping,1,BLOCK_PRIO_NORMAL);
    uint64_t flags=spin_lock_irqsave(&pc_lock);
    while (pc_dirty_count+pc_writeback_count>limit && pc_writeback_count)
        pc_wait_locked(&flags);
    spin_unlock_irqrestore(&pc_lock,flags);
}

/* -------------------------------------------------------------- lookup */

struct pc_page *pc_find(struct pc_mapping *mapping, uint64_t index) {
    uint64_t flags=spin_lock_irqsave(&pc_lock);
    struct pc_page *p=lookup_locked(mapping,index);
    if (p)
        ++p->refcount;
    spin_unlock_irqrestore(&pc_lock,flags);
    return p;
}

static int pc_alloc_frame(uint64_t *frame, uint64_t *flags) {
    for (int attempt=0; attempt<4; ++attempt) {
        if (pc_page_count<pc_limit) {
            void *page=page_alloc();
            if (page) {
                *frame=(uint64_t)page;
                return 0;
            }
        }
        if (evict_one_locked(frame))
            return 0;
        /* Only dirty/in-flight pages left: write some back and wait. */
        if (pc_dirty_count==0 && pc_writeback_count==0)
            break;
        spin_unlock_irqrestore(&pc_lock,*flags);
        (void)pc_start_writeback(0,16,0,BLOCK_PRIO_NORMAL);
        *flags=spin_lock_irqsave(&pc_lock);
        while (pc_writeback_count)
            pc_wait_locked(flags);
    }
    ++pc_counters.alloc_failures;
    return -SE_NOMEM;
}

int pc_get(struct pc_mapping *mapping, uint64_t index, uint64_t block, int fill,
           uint8_t priority, struct pc_page **out) {
    uint64_t flags=spin_lock_irqsave(&pc_lock);
    for (;;) {
        struct pc_page *p=lookup_locked(mapping,index);
        if (p) {
            ++p->refcount;
            if (p->flags&PC_LOCKED) {
                /* Being filled by another task. */
                while (p->flags&PC_LOCKED)
                    pc_wait_locked(&flags);
            }
            if (!(p->flags&PC_VALID)) {
                /* Fill failed earlier: retry as a miss. */
                --p->refcount;
                if (p->refcount==0 && !p->pins) {
                    uint64_t frame;
                    page_forget_locked(p,&frame);
                    page_free((void *)frame);
                }
                continue;
            }
            if (block && !p->block)
                p->block=block;
            p->flags|=PC_REFERENCED;
            lru_remove(p);
            lru_push_front(p);
            ++pc_counters.hits;
            spin_unlock_irqrestore(&pc_lock,flags);
            *out=p;
            return 0;
        }
        uint64_t frame;
        if (pc_alloc_frame(&frame,&flags)!=0) {
            spin_unlock_irqrestore(&pc_lock,flags);
            return -SE_NOMEM;
        }
        /* Allocation may have dropped the lock: re-check for a racer. */
        if (lookup_locked(mapping,index)) {
            page_free((void *)frame);
            continue;
        }
        p=pc_free_structs;
        pc_free_structs=p->hash_next;
        memset(p,0,sizeof(*p));
        p->mapping=mapping;
        p->index=index;
        p->physical=frame;
        p->block=block;
        p->refcount=1;
        p->flags=PC_LOCKED;
        hash_insert(p);
        map_insert(p);
        lru_push_front(p);
        ++pc_page_count;
        ++pc_counters.misses;
        spin_unlock_irqrestore(&pc_lock,flags);

        int rc=0;
        if (fill && block) {
            ++pc_counters.reads;
            rc=block_rw(mapping->device,BLOCK_OP_READ,block*mapping->sectors_per_block,
                        (void *)frame,4096,priority);
        } else {
            memset((void *)frame,0,4096);
        }
        flags=spin_lock_irqsave(&pc_lock);
        p->flags&=~PC_LOCKED;
        if (rc==0)
            p->flags|=PC_VALID;
        else {
            p->flags|=PC_ERROR;
            p->error=rc;
        }
        spin_unlock_irqrestore(&pc_lock,flags);
        (void)wait_queue_wake_all(&pc_waitq);
        if (rc) {
            pc_put(p);
            return rc;
        }
        *out=p;
        return 0;
    }
}

void pc_put(struct pc_page *page) {
    uint64_t flags=spin_lock_irqsave(&pc_lock);
    if (page->refcount)
        --page->refcount;
    /* Failed fills are dropped immediately so a retry re-reads. */
    if (page->refcount==0 && !page->pins && !(page->flags&PC_VALID) &&
        !(page->flags&PC_LOCKED)) {
        uint64_t frame;
        page_forget_locked(page,&frame);
        spin_unlock_irqrestore(&pc_lock,flags);
        page_free((void *)frame);
        return;
    }
    spin_unlock_irqrestore(&pc_lock,flags);
}

void pc_mark_dirty(struct pc_page *page, uint64_t block) {
    uint64_t flags=spin_lock_irqsave(&pc_lock);
    if (block)
        page->block=block;
    if (!(page->flags&PC_DIRTY)) {
        page->flags|=PC_DIRTY;
        page->dirty_tick=timer_ticks();
        ++pc_dirty_count;
        ++page->mapping->ndirty;
    }
    int first=pc_dirty_count==1;
    page->wb_retries=0;
    spin_unlock_irqrestore(&pc_lock,flags);
    if (first)
        kcompletion_signal(&pc_flusher_event);
}

void pc_pin(struct pc_page *page) {
    uint64_t flags=spin_lock_irqsave(&pc_lock);
    ++page->pins;
    ++pc_counters.pinned;
    spin_unlock_irqrestore(&pc_lock,flags);
}

void pc_unpin(struct pc_page *page) {
    uint64_t flags=spin_lock_irqsave(&pc_lock);
    if (page->pins) {
        --page->pins;
        --pc_counters.pinned;
    }
    spin_unlock_irqrestore(&pc_lock,flags);
}

int pc_mapping_has_pins(struct pc_mapping *mapping) {
    int pinned=0;
    uint64_t flags=spin_lock_irqsave(&pc_lock);
    for (struct pc_page *p=mapping->pages; p; p=p->map_next)
        if (p->pins)
            pinned=1;
    spin_unlock_irqrestore(&pc_lock,flags);
    return pinned;
}

int pc_invalidate(struct pc_mapping *mapping, uint64_t from) {
    uint64_t flags=spin_lock_irqsave(&pc_lock);
    for (struct pc_page *p=mapping->pages; p; p=p->map_next)
        if (p->index>=from && p->pins) {
            spin_unlock_irqrestore(&pc_lock,flags);
            return -SE_BUSY;
        }
    for (;;) {
        struct pc_page *victim=0;
        int busy=0;
        for (struct pc_page *p=mapping->pages; p; p=p->map_next) {
            if (p->index<from)
                continue;
            if (p->refcount || (p->flags&(PC_WRITEBACK|PC_LOCKED))) {
                busy=1;
                continue;
            }
            victim=p;
            break;
        }
        if (victim) {
            uint64_t frame;
            page_forget_locked(victim,&frame);
            spin_unlock_irqrestore(&pc_lock,flags);
            page_free((void *)frame);
            flags=spin_lock_irqsave(&pc_lock);
            continue;
        }
        if (!busy)
            break;
        pc_wait_locked(&flags);
    }
    spin_unlock_irqrestore(&pc_lock,flags);
    return 0;
}

void pc_set_limit(uint64_t pages) {
    uint64_t flags=spin_lock_irqsave(&pc_lock);
    pc_limit=pages<16ULL ? 16ULL : (pages>PC_MAX_PAGES ? PC_MAX_PAGES : pages);
    spin_unlock_irqrestore(&pc_lock,flags);
    (void)pc_shrink(0);
}

/* Release clean pages until the cache is at or below `target` pages
 * (target 0 = the current limit). Returns pages released. */
uint64_t pc_shrink(uint64_t target) {
    uint64_t released=0;
    uint64_t flags=spin_lock_irqsave(&pc_lock);
    if (target==0)
        target=pc_limit;
    while (pc_page_count>target) {
        uint64_t frame;
        if (!evict_one_locked(&frame))
            break;
        spin_unlock_irqrestore(&pc_lock,flags);
        page_free((void *)frame);
        ++released;
        flags=spin_lock_irqsave(&pc_lock);
    }
    spin_unlock_irqrestore(&pc_lock,flags);
    return released;
}

void pc_stats_snapshot(struct pc_stats *out) {
    uint64_t flags=spin_lock_irqsave(&pc_lock);
    *out=pc_counters;
    out->limit=pc_limit;
    out->pages=pc_page_count;
    out->dirty=pc_dirty_count;
    out->writeback=pc_writeback_count;
    spin_unlock_irqrestore(&pc_lock,flags);
}

/* Bounded, event-driven background writeback. */
static void pc_flusher_main(void *argument) {
    (void)argument;
    for (;;) {
        kcompletion_reset(&pc_flusher_event);
        if (__atomic_load_n(&pc_dirty_count,__ATOMIC_ACQUIRE)==0) {
            kcompletion_wait(&pc_flusher_event);
            __atomic_fetch_add(&pc_counters.flusher_wakeups,1ULL,__ATOMIC_RELAXED);
            continue;
        }
        task_sleep_ticks(100);
        uint64_t now=timer_ticks();
        uint64_t older=now>PC_DIRTY_EXPIRE ? now-PC_DIRTY_EXPIRE : 1ULL;
        if (pc_dirty_count>pc_limit/10ULL)
            older=0;                        /* above background threshold */
        (void)pc_start_writeback(0,PC_FLUSH_BATCH,older,BLOCK_PRIO_BACKGROUND);
        __atomic_fetch_add(&pc_counters.flusher_passes,1ULL,__ATOMIC_RELAXED);
        /* Memory pressure: keep a reserve of free frames for the system. */
        if (memory_free_pages()<256ULL)
            (void)pc_shrink(pc_page_count/2ULL);
    }
}

void pc_flusher_start(void) {
    if (pc_flusher_task)
        return;
    if (task_create(pc_flusher_main,0,&pc_flusher_task)!=0)
        klog("ZEROOS: page cache flusher task creation failed.");
}
