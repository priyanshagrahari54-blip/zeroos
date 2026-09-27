#include "block.h"
#include "../cpu.h"
#include "../kstring.h"
#include "../memory.h"
#include "../task.h"
#include "../timer.h"

static struct block_device block_devices[BLOCK_MAX_DEVICES];
static struct spinlock block_table_lock;
static uint32_t block_count;
static struct kcompletion watchdog_event;
static uint64_t watchdog_busy_devices;     /* devices with in-flight I/O */
static int block_ready;
static uint64_t watchdog_task_id;

static uint64_t block_now_ns(void) {
    return timer_monotonic_ns();
}

static struct kmutex reset_lock;

int block_init(void) {
    kmutex_init(&reset_lock,"block-reset");
    if (block_ready)
        return 0;
    spinlock_init(&block_table_lock);
    kcompletion_init(&watchdog_event);
    block_ready=1;
    return 0;
}

struct block_device *block_device_alloc(void) {
    uint64_t flags=spin_lock_irqsave(&block_table_lock);
    for (uint32_t i=0; i<BLOCK_MAX_DEVICES; ++i) {
        struct block_device *device=&block_devices[i];
        if (device->in_use)
            continue;
        memset(device,0,sizeof(*device));
        device->in_use=1;
        device->index=i;
        spinlock_init(&device->lock);
        wait_queue_init(&device->pool_waiters);
        if (i>=block_count)
            block_count=i+1;
        spin_unlock_irqrestore(&block_table_lock,flags);
        return device;
    }
    spin_unlock_irqrestore(&block_table_lock,flags);
    return 0;
}

struct block_device *block_device_at(uint32_t index) {
    if (index>=block_count || !block_devices[index].in_use ||
        !block_devices[index].name[0])
        return 0;
    return &block_devices[index];
}

uint32_t block_device_count(void) {
    return block_count;
}

struct block_device *block_find(const char *name) {
    for (uint32_t i=0; i<block_count; ++i) {
        struct block_device *device=block_device_at(i);
        if (device && kstrneq(device->name,name,BLOCK_NAME_MAX))
            return device;
    }
    return 0;
}

struct block_device *block_root(struct block_device *device) {
    while (device && device->parent)
        device=device->parent;
    return device;
}

static int block_pool_create(struct block_device *device) {
    uint32_t count=device->queue_depth*2U;
    if (count<8U)
        count=8U;
    if (count>BLOCK_POOL_MAX)
        count=BLOCK_POOL_MAX;
    uint64_t bytes=(uint64_t)count*sizeof(struct block_request);
    uint64_t pages=(bytes+ZEROOS_PAGE_SIZE-1ULL)/ZEROOS_PAGE_SIZE;
    struct block_request *pool=(struct block_request *)page_alloc_contiguous(pages);
    if (!pool)
        return -SE_NOMEM;
    memset(pool,0,pages*ZEROOS_PAGE_SIZE);
    device->pool=pool;
    device->pool_size=count;
    device->pool_free=0;
    for (uint32_t i=count; i>0; --i) {
        struct block_request *request=&pool[i-1U];
        request->pool_index=(uint16_t)(i-1U);
        request->device=device;
        request->next=device->pool_free;
        device->pool_free=request;
    }
    device->pool_free_count=count;
    return 0;
}

int block_register(struct block_device *device) {
    if (!device || !device->name[0] || !device->sector_size ||
        (device->sector_size&(device->sector_size-1U)) ||
        device->sector_size>4096U || device->sectors==0)
        return -SE_INVAL;
    if (!device->parent) {
        if (!device->ops || !device->ops->submit || !device->ops->kick)
            return -SE_INVAL;
        if (device->queue_depth==0)
            device->queue_depth=1;
        if (device->hw_queues==0)
            device->hw_queues=1;
        if (device->max_sectors==0)
            device->max_sectors=(64U*1024U)/device->sector_size;
        if (device->timeout_ticks==0)
            device->timeout_ticks=timer_frequency_hz()*5U;
        if (device->aging_ticks==0)
            device->aging_ticks=BLOCK_AGING_TICKS;
        int rc=block_pool_create(device);
        if (rc)
            return rc;
    }
    device->state=BLOCK_STATE_ONLINE;
    klog("ZEROOS: block device %s registered: sectors=%llu sector_size=%u media=%u depth=%u hwq=%u mode=%s%s.",
         device->name,device->sectors,device->sector_size,device->media,
         device->parent ? block_root(device)->queue_depth : device->queue_depth,
         device->parent ? block_root(device)->hw_queues : device->hw_queues,
         (block_root(device)->flags&BLOCK_DEV_POLLED) ? "polled" : "interrupt",
         (device->flags&BLOCK_DEV_READ_ONLY) ? " ro" : "");
    return 0;
}

int block_open(struct block_device *device) {
    struct block_device *root=block_root(device);
    uint64_t flags=spin_lock_irqsave(&root->lock);
    if (root->state==BLOCK_STATE_GONE || root->state==BLOCK_STATE_FAILED) {
        spin_unlock_irqrestore(&root->lock,flags);
        return -SE_NODEV;
    }
    ++device->refcount;
    spin_unlock_irqrestore(&root->lock,flags);
    return 0;
}

void block_close(struct block_device *device) {
    struct block_device *root=block_root(device);
    uint64_t flags=spin_lock_irqsave(&root->lock);
    if (device->refcount)
        --device->refcount;
    spin_unlock_irqrestore(&root->lock,flags);
}

int block_add_partition(struct block_device *disk, uint32_t number,
                        uint64_t first_lba, uint64_t sectors,
                        const uint8_t type_guid[16],
                        const uint8_t unique_guid[16],
                        struct block_device **out) {
    if (!disk || disk->parent || sectors==0 || first_lba>disk->sectors ||
        sectors>disk->sectors-first_lba)
        return -SE_INVAL;
    struct block_device *part=block_device_alloc();
    if (!part)
        return -SE_NOSPC;
    ksnprintf(part->name,sizeof(part->name),"%sp%u",disk->name,number);
    part->parent=disk;
    part->start_lba=first_lba;
    part->sectors=sectors;
    part->sector_size=disk->sector_size;
    part->media=disk->media;
    part->flags=disk->flags;
    part->partition_number=number;
    memcpy(part->type_guid,type_guid,16);
    memcpy(part->guid,unique_guid,16);
    int rc=block_register(part);
    if (rc) {
        part->in_use=0;
        return rc;
    }
    ++disk->refcount;
    if (out)
        *out=part;
    return 0;
}

/* ---------------------------------------------------------------- pool */

struct block_request *block_request_alloc(struct block_device *device,
                                          uint8_t priority, int wait) {
    struct block_device *root=block_root(device);
    if (!root || !root->pool || priority>=BLOCK_PRIO_COUNT)
        return 0;
    uint64_t flags=spin_lock_irqsave(&root->lock);
    for (;;) {
        uint32_t reserve=root->pool_size/4U;
        int allowed=priority==BLOCK_PRIO_BACKGROUND ?
                    root->pool_free_count>reserve : root->pool_free_count>0;
        if (allowed && root->pool_free) {
            struct block_request *request=root->pool_free;
            root->pool_free=request->next;
            --root->pool_free_count;
            spin_unlock_irqrestore(&root->lock,flags);
            uint16_t index=request->pool_index;
            memset(request,0,sizeof(*request)-sizeof(request->segments));
            request->pool_index=index;
            request->device=root;
            request->priority=priority;
            request->state=BLOCK_REQ_ALLOCATED;
            kcompletion_init(&request->completion);
            return request;
        }
        if (!wait) {
            spin_unlock_irqrestore(&root->lock,flags);
            return 0;
        }
        ++root->stats.pool_waits;
        uint64_t inner;
        if (wait_queue_prepare(&root->pool_waiters,&inner)!=0) {
            spin_unlock_irqrestore(&root->lock,flags);
            task_yield();
            flags=spin_lock_irqsave(&root->lock);
            continue;
        }
        spin_unlock(&root->lock);
        (void)wait_queue_commit(flags);
        flags=spin_lock_irqsave(&root->lock);
    }
}

void block_request_free(struct block_request *request) {
    if (!request)
        return;
    struct block_device *root=request->device;
    uint64_t flags=spin_lock_irqsave(&root->lock);
    request->state=BLOCK_REQ_FREE;
    request->next=root->pool_free;
    root->pool_free=request;
    ++root->pool_free_count;
    spin_unlock_irqrestore(&root->lock,flags);
    (void)wait_queue_wake_all(&root->pool_waiters);
}

int block_request_add_buffer(struct block_request *request, uint64_t physical,
                             uint32_t length) {
    if (length==0 || (physical&3ULL) || (length&3U))
        return -SE_INVAL;
    if (request->segment_count) {
        struct block_segment *last=&request->segments[request->segment_count-1U];
        if (last->physical+last->length==physical &&
            (uint64_t)last->length+length<=0x400000ULL) {
            last->length+=length;
            return 0;
        }
    }
    if (request->segment_count>=BLOCK_MAX_SEGMENTS)
        return -SE_INVAL;
    request->segments[request->segment_count].physical=physical;
    request->segments[request->segment_count].length=length;
    ++request->segment_count;
    return 0;
}

/* ----------------------------------------------------------- completion */

static void block_finish_one(struct block_request *request) {
    if (request->done)
        request->done(request);
    else
        kcompletion_signal(&request->completion);
}

static void block_account_locked(struct block_device *device,
                                 struct block_request *request) {
    uint32_t op=request->op;
    uint64_t latency=block_now_ns()-request->submit_ns;
    ++device->stats.completed[op];
    device->stats.sectors[op]+=request->own_sectors;
    if (request->status)
        ++device->stats.errors[op];
    device->stats.latency_ns_total[op]+=latency;
    if (latency>device->stats.latency_ns_max[op])
        device->stats.latency_ns_max[op]=latency;
    uint64_t us=latency/1000ULL;
    uint32_t bucket=0;
    while (us>1ULL && bucket+1U<BLOCK_LAT_BUCKETS) {
        us>>=1;
        ++bucket;
    }
    ++device->stats.latency_hist[bucket];
}

/* Called with device->lock held on the device's 0<->nonzero in-flight
 * transitions, so the global count never underflows. */
static void block_watchdog_note_busy(struct block_device *device, int busy) {
    (void)device;
    if (busy) {
        if (__atomic_fetch_add(&watchdog_busy_devices,1ULL,__ATOMIC_ACQ_REL)==0)
            kcompletion_signal(&watchdog_event);
    } else {
        __atomic_fetch_sub(&watchdog_busy_devices,1ULL,__ATOMIC_ACQ_REL);
    }
}

/* Finalize a request (and anything merged into it) that is no longer on a
 * queue or in flight. Caller must NOT hold the device lock. */
static void block_finalize_chain(struct block_device *device,
                                 struct block_request *request, int status) {
    uint64_t flags=spin_lock_irqsave(&device->lock);
    for (struct block_request *r=request; r; r=r->merged_next) {
        r->status=status;
        r->state=BLOCK_REQ_COMPLETE;
        block_account_locked(device,r);
    }
    spin_unlock_irqrestore(&device->lock,flags);
    while (request) {
        struct block_request *next=request->merged_next;
        request->merged_next=0;
        block_finish_one(request);
        request=next;
    }
}

static void block_queue_insert_locked(struct block_device *device,
                                      struct block_request *request, int front);
static void block_run_queue(struct block_device *device);

/* Undo a merge: the head keeps its own segments; each merged request goes
 * back on the queue individually so a media error is attributed only to the
 * range that actually failed. */
static void block_unmerge_locked(struct block_device *device,
                                 struct block_request *head) {
    struct block_request *chain=head->merged_next;
    head->merged_next=0;
    head->segment_count=head->own_segment_count;
    head->sectors=head->own_sectors;
    while (chain) {
        struct block_request *next=chain->merged_next;
        chain->merged_next=0;
        chain->state=BLOCK_REQ_QUEUED;
        block_queue_insert_locked(device,chain,1);
        ++device->queued;
        chain=next;
    }
}

void block_complete(struct block_request *request, int status) {
    struct block_device *device=request->device;
    uint64_t flags=spin_lock_irqsave(&device->lock);
    if (request->state!=BLOCK_REQ_DISPATCHED) {
        /* Duplicate completion (e.g. IRQ racing a reset): ignore. */
        spin_unlock_irqrestore(&device->lock,flags);
        return;
    }
    struct block_request **cursor=&device->inflight;
    while (*cursor && *cursor!=request)
        cursor=&(*cursor)->next;
    if (*cursor)
        *cursor=request->next;
    request->next=0;
    --device->inflight_count;
    if (request->priority==BLOCK_PRIO_BACKGROUND && device->inflight_background)
        --device->inflight_background;
    if (device->inflight_count==0)
        block_watchdog_note_busy(device,0);

    if (status<0 && (device->state==BLOCK_STATE_GONE ||
                     device->state==BLOCK_STATE_POWERED_OFF))
        status=-SE_NODEV;
    int retryable=(status==-SE_IO || status==-SE_TIMEDOUT) &&
                  (device->state==BLOCK_STATE_ONLINE ||
                   device->state==BLOCK_STATE_RESETTING) &&
                  request->op!=BLOCK_OP_FLUSH;
    if (status<0 && request->merged_next && retryable) {
        ++device->stats.retries;
        block_unmerge_locked(device,request);
        request->state=BLOCK_REQ_QUEUED;
        block_queue_insert_locked(device,request,1);
        ++device->queued;
        spin_unlock_irqrestore(&device->lock,flags);
        block_run_queue(device);
        return;
    }
    if (status<0 && retryable && request->retries<BLOCK_MAX_RETRIES) {
        ++request->retries;
        ++device->stats.retries;
        request->state=BLOCK_REQ_QUEUED;
        block_queue_insert_locked(device,request,1);
        ++device->queued;
        spin_unlock_irqrestore(&device->lock,flags);
        block_run_queue(device);
        return;
    }
    spin_unlock_irqrestore(&device->lock,flags);
    block_finalize_chain(device,request,status);
    block_run_queue(device);
}

void block_note_interrupt(struct block_device *device) {
    __atomic_fetch_add(&device->stats.interrupts,1ULL,__ATOMIC_RELAXED);
}

/* ------------------------------------------------------------ scheduler */

static void block_queue_insert_locked(struct block_device *device,
                                      struct block_request *request, int front) {
    uint32_t prio=request->priority;
    request->next=0;
    if (request->op==BLOCK_OP_FLUSH) {
        /* Barrier FIFO, ordered by sequence. */
        struct block_request **cursor=&device->flush_waiting;
        while (*cursor && (*cursor)->sequence<request->sequence)
            cursor=&(*cursor)->next;
        request->next=*cursor;
        *cursor=request;
        return;
    }
    if (device->media==BLOCK_MEDIA_HDD) {
        /* Sorted by LBA for the C-SCAN elevator. */
        struct block_request **cursor=&device->queue_head[prio];
        struct block_request *previous=0;
        while (*cursor && (*cursor)->lba<=request->lba) {
            previous=*cursor;
            cursor=&(*cursor)->next;
        }
        request->next=*cursor;
        *cursor=request;
        if (!request->next)
            device->queue_tail[prio]=request;
        (void)previous;
        return;
    }
    if (front) {
        request->next=device->queue_head[prio];
        device->queue_head[prio]=request;
        if (!device->queue_tail[prio])
            device->queue_tail[prio]=request;
        return;
    }
    if (device->queue_tail[prio])
        device->queue_tail[prio]->next=request;
    else
        device->queue_head[prio]=request;
    device->queue_tail[prio]=request;
}

static void block_queue_remove_locked(struct block_device *device,
                                      struct block_request *request) {
    uint32_t prio=request->priority;
    struct block_request **cursor=&device->queue_head[prio];
    struct block_request *previous=0;
    while (*cursor && *cursor!=request) {
        previous=*cursor;
        cursor=&(*cursor)->next;
    }
    if (!*cursor)
        return;
    *cursor=request->next;
    if (device->queue_tail[prio]==request)
        device->queue_tail[prio]=previous;
    request->next=0;
}

static uint64_t block_barrier_sequence(struct block_device *device) {
    return device->flush_waiting ? device->flush_waiting->sequence : ~0ULL;
}

/* Back-merge `request` into a queued request of the same op/priority that
 * ends exactly where it starts. Never merges across a pending barrier. */
static int block_try_merge_locked(struct block_device *device,
                                  struct block_request *request) {
    uint64_t newest_barrier=0;
    for (struct block_request *f=device->flush_waiting; f; f=f->next)
        newest_barrier=f->sequence;
    for (struct block_request *c=device->queue_head[request->priority]; c;
         c=c->next) {
        if (c->op!=request->op || c->sequence<newest_barrier ||
            c->lba+c->sectors!=request->lba ||
            c->sectors+request->sectors>device->max_sectors ||
            (uint32_t)c->segment_count+request->segment_count>BLOCK_MAX_SEGMENTS)
            continue;
        if (device->flags&BLOCK_DEV_PRP_SEGMENTS) {
            const struct block_segment *last=&c->segments[c->segment_count-1U];
            if (((last->physical+last->length)&(ZEROOS_PAGE_SIZE-1ULL)) ||
                (request->segments[0].physical&(ZEROOS_PAGE_SIZE-1ULL)))
                continue;
        }
        for (uint32_t i=0; i<request->segment_count; ++i)
            c->segments[c->segment_count++]=request->segments[i];
        c->sectors+=request->sectors;
        struct block_request **tail=&c->merged_next;
        while (*tail)
            tail=&(*tail)->merged_next;
        *tail=request;
        request->merged_next=0;
        request->state=BLOCK_REQ_QUEUED;
        ++device->stats.merges;
        return 1;
    }
    return 0;
}

static struct block_request *block_pick_in_queue(struct block_device *device,
                                                 uint32_t prio,
                                                 uint64_t barrier) {
    struct block_request *head=device->queue_head[prio];
    if (!head)
        return 0;
    if (device->media!=BLOCK_MEDIA_HDD)
        return head->sequence<barrier ? head : 0;
    /* C-SCAN: first eligible request at/after the head position, else wrap
     * to the lowest eligible LBA. */
    struct block_request *wrap=0;
    for (struct block_request *r=head; r; r=r->next) {
        if (r->sequence>=barrier)
            continue;
        if (!wrap)
            wrap=r;
        if (r->lba>=device->head_position)
            return r;
    }
    return wrap;
}

static struct block_request *block_oldest_in_queue(struct block_device *device,
                                                   uint32_t prio,
                                                   uint64_t barrier) {
    struct block_request *oldest=0;
    for (struct block_request *r=device->queue_head[prio]; r; r=r->next)
        if (r->sequence<barrier && (!oldest || r->submit_tick<oldest->submit_tick))
            oldest=r;
    return oldest;
}

static struct block_request *block_pick_locked(struct block_device *device) {
    uint64_t barrier=block_barrier_sequence(device);
    uint64_t now=timer_ticks();
    uint32_t bg_cap=device->queue_depth/4U;
    if (bg_cap==0)
        bg_cap=1;

    /* Starvation protection: aged NORMAL/BACKGROUND work jumps ahead. */
    struct block_request *old=block_oldest_in_queue(device,BLOCK_PRIO_NORMAL,barrier);
    if (old && now-old->submit_tick>=device->aging_ticks) {
        ++device->stats.aging_promotions;
        return old;
    }
    old=block_oldest_in_queue(device,BLOCK_PRIO_BACKGROUND,barrier);
    if (old && now-old->submit_tick>=2ULL*device->aging_ticks &&
        device->inflight_background<bg_cap) {
        ++device->stats.aging_promotions;
        return old;
    }
    struct block_request *pick=block_pick_in_queue(device,BLOCK_PRIO_FOREGROUND,barrier);
    if (pick)
        return pick;
    pick=block_pick_in_queue(device,BLOCK_PRIO_NORMAL,barrier);
    if (pick)
        return pick;
    pick=block_pick_in_queue(device,BLOCK_PRIO_BACKGROUND,barrier);
    if (pick) {
        if (device->inflight_background>=bg_cap) {
            ++device->stats.background_throttled;
            return 0;
        }
        return pick;
    }
    /* Only the barrier remains eligible: dispatch it once everything issued
     * before it has completed. */
    if (device->flush_waiting && device->inflight_count==0)
        return device->flush_waiting;
    return 0;
}

static int block_fault_match_locked(struct block_device *device,
                                    struct block_request *request) {
    struct block_fault *fault=&device->fault;
    if (fault->mode==BLOCK_FAULT_NONE || !(fault->op_mask&(1U<<request->op)))
        return 0;
    if (request->op!=BLOCK_OP_FLUSH && fault->lba_end &&
        (request->lba+request->sectors<=fault->lba_start ||
         request->lba>=fault->lba_end))
        return 0;
    if (fault->after) {
        --fault->after;
        return 0;
    }
    ++fault->hits;
    ++device->stats.faults_injected;
    uint32_t mode=fault->mode;
    if (fault->remaining && --fault->remaining==0)
        fault->mode=BLOCK_FAULT_NONE;
    return (int)mode;
}

/* Detach every queued (not yet dispatched) request into a list linked via
 * `next` so the caller can fail them after dropping the lock. */
static struct block_request *block_drain_queued_locked(struct block_device *device) {
    struct block_request *list=0;
    for (uint32_t p=0; p<BLOCK_PRIO_COUNT; ++p) {
        while (device->queue_head[p]) {
            struct block_request *r=device->queue_head[p];
            device->queue_head[p]=r->next;
            r->next=list;
            list=r;
        }
        device->queue_tail[p]=0;
    }
    while (device->flush_waiting) {
        struct block_request *r=device->flush_waiting;
        device->flush_waiting=r->next;
        r->next=list;
        list=r;
    }
    device->queued=0;
    return list;
}

static void block_fail_list(struct block_device *device,
                            struct block_request *list, int status) {
    while (list) {
        struct block_request *next=list->next;
        list->next=0;
        block_finalize_chain(device,list,status);
        list=next;
    }
}

static void block_run_queue(struct block_device *device) {
    uint64_t flags=spin_lock_irqsave(&device->lock);
    if (device->running) {
        device->rerun=1;
        spin_unlock_irqrestore(&device->lock,flags);
        return;
    }
    device->running=1;
    for (;;) {
        struct block_request *drained=0;
        int drained_status=0;
        uint64_t kick_mask=0;
        int became_busy=0;
        device->rerun=0;
        while (device->state==BLOCK_STATE_ONLINE &&
               device->inflight_count<device->queue_depth) {
            struct block_request *request=block_pick_locked(device);
            if (!request)
                break;
            if (request->op==BLOCK_OP_FLUSH)
                device->flush_waiting=request->next;
            else
                block_queue_remove_locked(device,request);
            --device->queued;
            request->next=0;
            request->state=BLOCK_REQ_DISPATCHED;
            request->dispatch_tick=timer_ticks();
            request->hw_queue=(uint8_t)(cpu_current_id()%device->hw_queues);
            if (device->inflight_count==0)
                became_busy=1;
            request->next=device->inflight;
            device->inflight=request;
            ++device->inflight_count;
            if (request->priority==BLOCK_PRIO_BACKGROUND)
                ++device->inflight_background;
            ++device->stats.dispatched_by_prio[request->priority];
            if (device->inflight_count>device->stats.max_inflight)
                device->stats.max_inflight=device->inflight_count;
            if (request->op!=BLOCK_OP_FLUSH)
                device->head_position=request->lba+request->sectors;

            int fault=block_fault_match_locked(device,request);
            if (fault==BLOCK_FAULT_EIO) {
                request->injected=BLOCK_FAULT_EIO;
                /* Completed with -SE_IO after the lock is dropped; stays
                 * on the in-flight list until then. */
                request->tag=0xffffffffU;
                continue;
            }
            if (fault==BLOCK_FAULT_STALL) {
                request->injected=BLOCK_FAULT_STALL;
                continue;
            }
            if (fault==BLOCK_FAULT_DISAPPEAR || fault==BLOCK_FAULT_POWER_CUT) {
                if (fault==BLOCK_FAULT_POWER_CUT && device->ops->power_cut)
                    device->ops->power_cut(device,device->fault.power_policy,
                                           device->fault.power_seed);
                device->state=fault==BLOCK_FAULT_DISAPPEAR ?
                              BLOCK_STATE_GONE : BLOCK_STATE_POWERED_OFF;
                device->fault.mode=BLOCK_FAULT_NONE;
                request->injected=(uint8_t)fault;
                drained=block_drain_queued_locked(device);
                drained_status=-SE_NODEV;
                break;
            }
            int rc=device->ops->submit(device,request);
            if (rc==-SE_AGAIN) {
                /* No hardware slot: undo the dispatch and stop. */
                device->inflight=request->next;
                --device->inflight_count;
                if (request->priority==BLOCK_PRIO_BACKGROUND)
                    --device->inflight_background;
                if (device->inflight_count==0)
                    became_busy=0;
                request->state=BLOCK_REQ_QUEUED;
                block_queue_insert_locked(device,request,1);
                ++device->queued;
                break;
            }
            if (rc<0) {
                request->injected=0xfe;
                request->status=rc;
                continue;
            }
            kick_mask|=1ULL<<(request->hw_queue&63U);
        }
        /* Collect injected-EIO / submit-failed requests. */
        struct block_request *fail_now[BLOCK_POOL_MAX];
        uint32_t fail_count=0;
        for (struct block_request *r=device->inflight; r && fail_count<BLOCK_POOL_MAX;
             r=r->next) {
            if (r->injected==BLOCK_FAULT_EIO || r->injected==0xfe ||
                r->injected==BLOCK_FAULT_DISAPPEAR ||
                r->injected==BLOCK_FAULT_POWER_CUT)
                fail_now[fail_count++]=r;
        }
        if (became_busy)
            block_watchdog_note_busy(device,1);
        spin_unlock_irqrestore(&device->lock,flags);

        for (uint32_t q=0; q<64U && kick_mask; ++q) {
            if (kick_mask&(1ULL<<q)) {
                kick_mask&=~(1ULL<<q);
                device->ops->kick(device,q);
            }
        }
        for (uint32_t i=0; i<fail_count; ++i) {
            struct block_request *r=fail_now[i];
            int status=r->injected==BLOCK_FAULT_EIO ? -SE_IO :
                       (r->injected==0xfe ? r->status : -SE_NODEV);
            r->injected=0;
            block_complete(r,status);
        }
        block_fail_list(device,drained,drained_status);

        flags=spin_lock_irqsave(&device->lock);
        if (!device->rerun)
            break;
    }
    device->running=0;
    spin_unlock_irqrestore(&device->lock,flags);
}

void block_submit(struct block_device *device, struct block_request *request) {
    struct block_device *root=block_root(device);
    uint64_t bytes=0;
    int error=0;
    for (uint32_t i=0; i<request->segment_count; ++i)
        bytes+=request->segments[i].length;
    request->device=root;
    request->status=0;
    request->retries=0;
    request->injected=0;
    request->merged_next=0;
    request->submit_tick=timer_ticks();
    request->submit_ns=block_now_ns();
    if (request->op>BLOCK_OP_FLUSH) {
        error=-SE_INVAL;
    } else if (request->op!=BLOCK_OP_FLUSH) {
        if (bytes==0 || (bytes%root->sector_size)!=0)
            error=-SE_INVAL;
        else {
            request->sectors=(uint32_t)(bytes/root->sector_size);
            if (request->sectors>root->max_sectors)
                error=-SE_INVAL;
            else if (request->lba>device->sectors ||
                     request->sectors>device->sectors-request->lba)
                error=-SE_INVAL;
            else if (request->op==BLOCK_OP_WRITE &&
                     ((device->flags|root->flags)&BLOCK_DEV_READ_ONLY))
                error=-SE_ROFS;
            else
                request->lba+=device->start_lba;
        }
    } else {
        request->sectors=0;
    }
    request->own_sectors=request->sectors;
    request->own_segment_count=request->segment_count;

    uint64_t flags=spin_lock_irqsave(&root->lock);
    if (!error) {
        if (root->state==BLOCK_STATE_GONE || root->state==BLOCK_STATE_POWERED_OFF)
            error=-SE_NODEV;
        else if (root->state==BLOCK_STATE_FAILED)
            error=-SE_IO;
    }
    ++root->stats.submitted[request->op<=BLOCK_OP_FLUSH ? request->op : 0];
    if (error) {
        spin_unlock_irqrestore(&root->lock,flags);
        block_finalize_chain(root,request,error);
        return;
    }
    request->sequence=++root->next_sequence;
    request->state=BLOCK_REQ_QUEUED;
    if (request->op==BLOCK_OP_FLUSH || !block_try_merge_locked(root,request)) {
        block_queue_insert_locked(root,request,0);
        ++root->queued;
        if (root->queued>root->stats.max_queued)
            root->stats.max_queued=root->queued;
    }
    spin_unlock_irqrestore(&root->lock,flags);
    block_run_queue(root);
}

int block_wait(struct block_request *request) {
    kcompletion_wait(&request->completion);
    return request->status;
}

int block_cancel(struct block_request *request) {
    struct block_device *device=request->device;
    uint64_t flags=spin_lock_irqsave(&device->lock);
    if (request->state!=BLOCK_REQ_QUEUED) {
        spin_unlock_irqrestore(&device->lock,flags);
        return -SE_BUSY;
    }
    int found=0;
    if (request->op==BLOCK_OP_FLUSH) {
        struct block_request **cursor=&device->flush_waiting;
        while (*cursor && *cursor!=request)
            cursor=&(*cursor)->next;
        if (*cursor) {
            *cursor=request->next;
            found=1;
        }
    } else {
        for (struct block_request *r=device->queue_head[request->priority]; r;
             r=r->next)
            if (r==request)
                found=1;
        /* Requests merged into another are part of that request's DMA
         * program and are not individually cancellable. */
        if (found && request->merged_next)
            found=0;
        if (found)
            block_queue_remove_locked(device,request);
    }
    if (!found) {
        spin_unlock_irqrestore(&device->lock,flags);
        return -SE_BUSY;
    }
    --device->queued;
    ++device->stats.cancels;
    request->next=0;
    spin_unlock_irqrestore(&device->lock,flags);
    block_finalize_chain(device,request,-SE_CANCELED);
    return 0;
}

void block_device_set_state(struct block_device *device, uint32_t state) {
    struct block_device *root=block_root(device);
    uint64_t flags=spin_lock_irqsave(&root->lock);
    struct block_request *drained=0;
    root->state=state;
    if (state==BLOCK_STATE_GONE || state==BLOCK_STATE_FAILED ||
        state==BLOCK_STATE_POWERED_OFF)
        drained=block_drain_queued_locked(root);
    spin_unlock_irqrestore(&root->lock,flags);
    block_fail_list(root,drained,state==BLOCK_STATE_FAILED ? -SE_IO : -SE_NODEV);
    if (state==BLOCK_STATE_ONLINE)
        block_run_queue(root);
}

/* ------------------------------------------------------ sync helpers */

int block_rw(struct block_device *device, uint32_t op, uint64_t lba,
             void *buffer, uint64_t bytes, uint8_t priority) {
    struct block_device *root=block_root(device);
    if (!root || (op!=BLOCK_OP_READ && op!=BLOCK_OP_WRITE) ||
        (bytes%root->sector_size)!=0)
        return -SE_INVAL;
    uint64_t chunk=(uint64_t)root->max_sectors*root->sector_size;
    uint64_t address=(uint64_t)buffer;
    while (bytes) {
        uint64_t length=bytes<chunk ? bytes : chunk;
        struct block_request *request=block_request_alloc(device,priority,1);
        if (!request)
            return -SE_NOMEM;
        request->op=(uint8_t)op;
        request->lba=lba;
        /* Kernel memory is identity mapped: virtual == physical. Split at
         * page boundaries so no segment assumes physical contiguity beyond
         * what the page allocator guarantees for the caller's buffer. */
        uint64_t offset=0;
        int rc=0;
        while (offset<length && rc==0) {
            uint64_t page_left=ZEROOS_PAGE_SIZE-((address+offset)&(ZEROOS_PAGE_SIZE-1ULL));
            uint64_t piece=length-offset<page_left ? length-offset : page_left;
            rc=block_request_add_buffer(request,address+offset,(uint32_t)piece);
            offset+=piece;
        }
        if (rc) {
            block_request_free(request);
            return rc;
        }
        block_submit(device,request);
        rc=block_wait(request);
        block_request_free(request);
        if (rc)
            return rc;
        lba+=length/root->sector_size;
        address+=length;
        bytes-=length;
    }
    return 0;
}

int block_flush(struct block_device *device, uint8_t priority) {
    struct block_request *request=block_request_alloc(device,priority,1);
    if (!request)
        return -SE_NOMEM;
    request->op=BLOCK_OP_FLUSH;
    block_submit(device,request);
    int rc=block_wait(request);
    block_request_free(request);
    return rc;
}

void block_fault_set(struct block_device *device, const struct block_fault *fault) {
    struct block_device *root=block_root(device);
    uint64_t flags=spin_lock_irqsave(&root->lock);
    root->fault=*fault;
    root->fault.hits=0;
    if (device->parent && fault->lba_end) {
        root->fault.lba_start+=device->start_lba;
        root->fault.lba_end+=device->start_lba;
    }
    spin_unlock_irqrestore(&root->lock,flags);
}

void block_fault_clear(struct block_device *device) {
    struct block_device *root=block_root(device);
    uint64_t flags=spin_lock_irqsave(&root->lock);
    root->fault.mode=BLOCK_FAULT_NONE;
    spin_unlock_irqrestore(&root->lock,flags);
}

void block_stats_snapshot(struct block_device *device, struct block_stats *out,
                          uint32_t *queued, uint32_t *inflight) {
    struct block_device *root=block_root(device);
    uint64_t flags=spin_lock_irqsave(&root->lock);
    *out=root->stats;
    if (queued)
        *queued=root->queued;
    if (inflight)
        *inflight=root->inflight_count;
    spin_unlock_irqrestore(&root->lock,flags);
}

int block_idle(struct block_device *device) {
    struct block_device *root=block_root(device);
    uint64_t flags=spin_lock_irqsave(&root->lock);
    int idle=root->queued==0 && root->inflight_count==0;
    spin_unlock_irqrestore(&root->lock,flags);
    return idle;
}

void block_report(struct block_device *device) {
    struct block_stats s;
    uint32_t queued, inflight;
    block_stats_snapshot(device,&s,&queued,&inflight);
    uint64_t avg_r=s.completed[0] ? s.latency_ns_total[0]/s.completed[0]/1000ULL : 0;
    uint64_t avg_w=s.completed[1] ? s.latency_ns_total[1]/s.completed[1]/1000ULL : 0;
    klog("ZEROOS: block stats %s: rd=%llu wr=%llu fl=%llu rd_sect=%llu wr_sect=%llu err=%llu/%llu/%llu merges=%llu retries=%llu timeouts=%llu resets=%llu irq=%llu polls=%llu",
         device->name,s.completed[0],s.completed[1],s.completed[2],
         s.sectors[0],s.sectors[1],s.errors[0],s.errors[1],s.errors[2],
         s.merges,s.retries,s.timeouts,s.resets,s.interrupts,s.polls);
    klog("ZEROOS: block sched %s: avg_rd_us=%llu avg_wr_us=%llu max_rd_us=%llu max_wr_us=%llu max_q=%llu max_inflight=%llu fg=%llu norm=%llu bg=%llu aged=%llu bg_throttled=%llu pool_waits=%llu q=%u inflight=%u faults=%llu",
         device->name,avg_r,avg_w,s.latency_ns_max[0]/1000ULL,
         s.latency_ns_max[1]/1000ULL,s.max_queued,s.max_inflight,
         s.dispatched_by_prio[0],s.dispatched_by_prio[1],s.dispatched_by_prio[2],
         s.aging_promotions,s.background_throttled,s.pool_waits,queued,inflight,
         s.faults_injected);
}

/* -------------------------------------------------------------- watchdog */

static void block_watchdog_scan(struct block_device *device) {
    struct block_request *expired[BLOCK_POOL_MAX];
    uint32_t expired_count=0;
    int driver_expired=0;

    if ((device->flags&BLOCK_DEV_POLLED) && device->ops->poll) {
        ++device->stats.polls;
        device->ops->poll(device);
    }
    uint64_t now=timer_ticks();
    uint64_t flags=spin_lock_irqsave(&device->lock);
    for (struct block_request *r=device->inflight; r; r=r->next) {
        if (now-r->dispatch_tick<device->timeout_ticks)
            continue;
        if (r->injected==BLOCK_FAULT_STALL) {
            if (expired_count<BLOCK_POOL_MAX)
                expired[expired_count++]=r;
        } else {
            if (!driver_expired)
                klog("ZEROOS: block %s: expired request op=%u lba=%llu sectors=%u age=%llu state=%u.",
                     device->name,r->op,r->lba,r->sectors,now-r->dispatch_tick,r->state);
            driver_expired=1;
        }
    }
    spin_unlock_irqrestore(&device->lock,flags);

    if (driver_expired && device->ops->poll) {
        /* Lost interrupt? Harvest before declaring a timeout. */
        ++device->stats.polls;
        device->ops->poll(device);
        now=timer_ticks();
        driver_expired=0;
        flags=spin_lock_irqsave(&device->lock);
        for (struct block_request *r=device->inflight; r; r=r->next)
            if (r->injected!=BLOCK_FAULT_STALL &&
                now-r->dispatch_tick>=device->timeout_ticks)
                driver_expired=1;
        spin_unlock_irqrestore(&device->lock,flags);
    }
    for (uint32_t i=0; i<expired_count; ++i) {
        __atomic_fetch_add(&device->stats.timeouts,1ULL,__ATOMIC_RELAXED);
        expired[i]->injected=0;
        block_complete(expired[i],-SE_TIMEDOUT);
    }
    if (driver_expired) {
        __atomic_fetch_add(&device->stats.timeouts,1ULL,__ATOMIC_RELAXED);
        klog("ZEROOS: block %s: I/O timeout, resetting device.",device->name);
        (void)block_reset_device(device);
    }
}

/* Controller/port recovery (watchdog timeouts and explicit test/admin
 * requests). Serialized system-wide; the driver completes every in-flight
 * request (normally -SE_TIMEDOUT, retried by the block layer). */
int block_reset_device(struct block_device *device) {
    device=block_root(device);
    kmutex_lock(&reset_lock);
    uint64_t flags=spin_lock_irqsave(&device->lock);
    ++device->stats.resets;
    if (device->state==BLOCK_STATE_ONLINE)
        device->state=BLOCK_STATE_RESETTING;
    spin_unlock_irqrestore(&device->lock,flags);
    int rc=device->ops->reset ? device->ops->reset(device) : -SE_IO;
    if (rc==0) {
        flags=spin_lock_irqsave(&device->lock);
        if (device->state==BLOCK_STATE_RESETTING)
            device->state=BLOCK_STATE_ONLINE;
        spin_unlock_irqrestore(&device->lock,flags);
        klog("ZEROOS: block %s: reset complete, device online.",device->name);
        block_run_queue(device);
    } else {
        klog("ZEROOS: block %s: reset failed (%d); device failed.",device->name,rc);
        block_device_set_state(device,
            rc==-SE_NODEV ? BLOCK_STATE_GONE : BLOCK_STATE_FAILED);
    }
    kmutex_unlock(&reset_lock);
    return rc;
}

static void block_watchdog_task(void *argument) {
    (void)argument;
    for (;;) {
        kcompletion_reset(&watchdog_event);
        if (__atomic_load_n(&watchdog_busy_devices,__ATOMIC_ACQUIRE)==0) {
            kcompletion_wait(&watchdog_event);
            continue;
        }
        int polled_busy=0;
        for (uint32_t i=0; i<block_count; ++i) {
            struct block_device *device=block_device_at(i);
            if (device && !device->parent && (device->flags&BLOCK_DEV_POLLED) &&
                device->inflight_count)
                polled_busy=1;
        }
        task_sleep_ticks(polled_busy ? 1U : 10U);
        for (uint32_t i=0; i<block_count; ++i) {
            struct block_device *device=block_device_at(i);
            if (!device || device->parent || !device->ops)
                continue;
            if (__atomic_load_n(&device->inflight_count,__ATOMIC_ACQUIRE)==0)
                continue;
            block_watchdog_scan(device);
        }
    }
}

void block_watchdog_start(void) {
    if (watchdog_task_id)
        return;
    if (task_create(block_watchdog_task,0,&watchdog_task_id)!=0)
        klog("ZEROOS: block watchdog task creation failed.");
}
