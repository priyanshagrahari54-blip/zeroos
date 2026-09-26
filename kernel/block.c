#include "block.h"
#include "memory.h"
#include "timer.h"
#include "task.h"
#include "cpu.h"

extern void serial_write_public(const char *text);

static struct spinlock block_lock;
static struct zeroos_block_device devices[ZEROOS_BLOCK_MAX_DEVICES];
static uint64_t next_device_id;
static uint64_t next_request_id;

static struct zeroos_block_device *device_lookup_locked(uint64_t device_id) {
    uint32_t slot = (uint32_t)(device_id & 0xffffULL);
    uint32_t gen = (uint32_t)(device_id >> 16);
    if (slot>=ZEROOS_BLOCK_MAX_DEVICES || gen==0) return 0;
    struct zeroos_block_device *dev = &devices[slot];
    if (!dev->used || dev->generation!=gen) return 0;
    return dev;
}

int block_system_init(void) {
    spinlock_init(&block_lock);
    next_device_id = 1;
    next_request_id = 1;
    for (uint32_t i=0;i<ZEROOS_BLOCK_MAX_DEVICES;++i) {
        devices[i].used=0;
        devices[i].generation=0;
        devices[i].state=ZEROOS_BLOCK_DEV_STOPPED;
        devices[i].id=0;
        devices[i].capacity_sectors=0;
        devices[i].sector_size=ZEROOS_BLOCK_SECTOR_SIZE;
        devices[i].max_transfer_sectors=ZEROOS_BLOCK_MAX_TRANSFER_SECTORS;
        devices[i].queue_depth=ZEROOS_BLOCK_MAX_QUEUE_DEPTH;
        devices[i].current_queue_depth=0;
        devices[i].timeout_ticks=100; /* 1s at 100Hz */
        devices[i].error_count=0;
        devices[i].timeout_count=0;
        devices[i].completed_count=0;
        devices[i].owner_cpu=0;
        spinlock_init(&devices[i].lock);
        wait_queue_init(&devices[i].queue_waiters);
        devices[i].queue_head=0;
        devices[i].queue_tail=0;
        devices[i].io_priority=16;
        devices[i].foreground=0;
        devices[i].batch_count=0;
        devices[i].coalesced_writes=0;
        devices[i].dma_buffer=0;
        devices[i].dma_physical=0;
        devices[i].dma_size=0;
        devices[i].memory_budget_bytes=64*1024*1024;
        devices[i].io_budget_iops=1000;
        for (uint32_t j=0;j<ZEROOS_BLOCK_MAX_QUEUE_DEPTH;++j) {
            devices[i].requests[j].id=0;
            devices[i].requests[j].state=ZEROOS_BLOCK_REQ_QUEUED;
            devices[i].requests[j].next=0;
            wait_queue_init(&devices[i].requests[j].completion_waiters);
            devices[i].requests[j].completed=0;
        }
    }
    return 0;
}

int block_device_register(enum zeroos_block_device_type type, const char *name,
                          uint64_t capacity_sectors, uint64_t sector_size,
                          uint64_t *device_id_out) {
    uint64_t flags;
    if (!name || !device_id_out || capacity_sectors==0 || sector_size==0 ||
        sector_size<ZEROOS_BLOCK_SECTOR_SIZE || (sector_size & (sector_size-1))!=0)
        return -1;
    flags = spin_lock_irqsave(&block_lock);
    for (uint32_t i=0;i<ZEROOS_BLOCK_MAX_DEVICES;++i) {
        if (devices[i].used) continue;
        if (devices[i].generation==0xffffffffU) continue;
        devices[i].generation++;
        if (devices[i].generation==0) continue;
        devices[i].used=1;
        devices[i].type=type;
        devices[i].state=ZEROOS_BLOCK_DEV_DORMANT;
        devices[i].id = ((uint64_t)devices[i].generation<<16) | (uint64_t)(i+1);
        devices[i].capacity_sectors=capacity_sectors;
        devices[i].sector_size=sector_size;
        devices[i].current_queue_depth=0;
        devices[i].error_count=0;
        devices[i].timeout_count=0;
        devices[i].completed_count=0;
        uint32_t n=0;
        while (n<ZEROOS_BLOCK_MAX_NAME-1 && name[n]) {
            devices[i].name[n]=name[n];
            n++;
        }
        devices[i].name[n]=0;
        *device_id_out=devices[i].id;
        spin_unlock_irqrestore(&block_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&block_lock,flags);
    return -1;
}

int block_device_unregister(uint64_t device_id) {
    uint64_t flags = spin_lock_irqsave(&block_lock);
    struct zeroos_block_device *dev = device_lookup_locked(device_id);
    if (!dev) { spin_unlock_irqrestore(&block_lock,flags); return -1; }
    /* Must be stopped and no pending requests */
    uint64_t dflags = spin_lock_irqsave(&dev->lock);
    if (dev->current_queue_depth!=0 || dev->state!=ZEROOS_BLOCK_DEV_STOPPED) {
        spin_unlock_irqrestore(&dev->lock,dflags);
        spin_unlock_irqrestore(&block_lock,flags);
        return -1;
    }
    dev->used=0;
    dev->state=ZEROOS_BLOCK_DEV_STOPPED;
    dev->queue_head=0;
    dev->queue_tail=0;
    dev->current_queue_depth=0;
    spin_unlock_irqrestore(&dev->lock,dflags);
    spin_unlock_irqrestore(&block_lock,flags);
    return 0;
}

struct zeroos_block_device *block_device_lookup(uint64_t device_id) {
    uint64_t flags = spin_lock_irqsave(&block_lock);
    struct zeroos_block_device *dev = device_lookup_locked(device_id);
    spin_unlock_irqrestore(&block_lock,flags);
    return dev;
}

int block_device_set_state(uint64_t device_id, enum zeroos_block_device_state state) {
    uint64_t flags = spin_lock_irqsave(&block_lock);
    struct zeroos_block_device *dev = device_lookup_locked(device_id);
    if (!dev) { spin_unlock_irqrestore(&block_lock,flags); return -1; }
    uint64_t dflags = spin_lock_irqsave(&dev->lock);
    dev->state=state;
    spin_unlock_irqrestore(&dev->lock,dflags);
    spin_unlock_irqrestore(&block_lock,flags);
    return 0;
}

static int request_slot_alloc_locked(struct zeroos_block_device *dev, uint64_t *slot_out) {
    for (uint32_t i=0;i<ZEROOS_BLOCK_MAX_QUEUE_DEPTH;++i) {
        if (dev->requests[i].id==0) {
            *slot_out=i;
            return 0;
        }
    }
    return -1;
}

int block_submit_request(uint64_t device_id, enum zeroos_block_request_type type,
                         uint64_t lba, uint64_t sector_count, void *buffer,
                         uint64_t timeout_ticks, uint64_t *request_id_out) {
    uint64_t flags = spin_lock_irqsave(&block_lock);
    struct zeroos_block_device *dev = device_lookup_locked(device_id);
    if (!dev) { spin_unlock_irqrestore(&block_lock,flags); return -1; }
    uint64_t dflags = spin_lock_irqsave(&dev->lock);
    if (dev->state!=ZEROOS_BLOCK_DEV_ACTIVE && dev->state!=ZEROOS_BLOCK_DEV_WARM) {
        spin_unlock_irqrestore(&dev->lock,dflags);
        spin_unlock_irqrestore(&block_lock,flags);
        return -1;
    }
    if (lba>=dev->capacity_sectors || sector_count==0 ||
        sector_count>dev->max_transfer_sectors ||
        lba+sector_count>dev->capacity_sectors ||
        (type!=ZEROOS_BLOCK_REQ_FLUSH && !buffer)) {
        spin_unlock_irqrestore(&dev->lock,dflags);
        spin_unlock_irqrestore(&block_lock,flags);
        return -1;
    }
    if (dev->current_queue_depth>=dev->queue_depth) {
        spin_unlock_irqrestore(&dev->lock,dflags);
        spin_unlock_irqrestore(&block_lock,flags);
        return -1; /* bounded queue depth, backpressure */
    }
    uint64_t slot;
    if (request_slot_alloc_locked(dev,&slot)!=0) {
        spin_unlock_irqrestore(&dev->lock,dflags);
        spin_unlock_irqrestore(&block_lock,flags);
        return -1;
    }
    struct zeroos_block_request *req = &dev->requests[slot];
    req->id = next_request_id++;
    if (req->id==0) req->id=next_request_id++;
    req->type=type;
    req->state=ZEROOS_BLOCK_REQ_QUEUED;
    req->lba=lba;
    req->sector_count=sector_count;
    req->buffer=buffer;
    req->buffer_physical=0;
    req->flags=0;
    req->deadline_ticks = timeout_ticks ? timer_ticks()+timeout_ticks : timer_ticks()+dev->timeout_ticks;
    req->error=0;
    req->enqueue_ticks=timer_ticks();
    req->dispatch_ticks=0;
    req->complete_ticks=0;
    req->next=0;
    req->completed=0;
    wait_queue_init(&req->completion_waiters);
    /* Enqueue with batching/coalescing check: simple coalesce of adjacent writes */
    if (dev->queue_tail && dev->queue_tail->type==ZEROOS_BLOCK_REQ_WRITE &&
        type==ZEROOS_BLOCK_REQ_WRITE &&
        dev->queue_tail->lba+dev->queue_tail->sector_count==lba) {
        dev->coalesced_writes++;
    }
    if (!dev->queue_head) dev->queue_head=req;
    else dev->queue_tail->next=req;
    dev->queue_tail=req;
    dev->current_queue_depth++;
    dev->batch_count++;
    if (request_id_out) *request_id_out=req->id;
    spin_unlock_irqrestore(&dev->lock,dflags);
    spin_unlock_irqrestore(&block_lock,flags);
    return 0;
}

int block_request_complete(uint64_t device_id, uint64_t request_id, int error) {
    uint64_t flags = spin_lock_irqsave(&block_lock);
    struct zeroos_block_device *dev = device_lookup_locked(device_id);
    if (!dev) { spin_unlock_irqrestore(&block_lock,flags); return -1; }
    uint64_t dflags = spin_lock_irqsave(&dev->lock);
    struct zeroos_block_request *req=0;
    for (uint32_t i=0;i<ZEROOS_BLOCK_MAX_QUEUE_DEPTH;++i) {
        if (dev->requests[i].id==request_id) { req=&dev->requests[i]; break; }
    }
    if (!req) { spin_unlock_irqrestore(&dev->lock,dflags); spin_unlock_irqrestore(&block_lock,flags); return -1; }
    req->complete_ticks=timer_ticks();
    req->error=error;
    req->state = error ? ZEROOS_BLOCK_REQ_ERROR : ZEROOS_BLOCK_REQ_COMPLETED;
    req->completed=1;
    if (error) {
        dev->error_count++;
        dev->last_error_lba=req->lba;
        dev->last_error_code=(uint64_t)error;
    } else {
        dev->completed_count++;
    }
    /* Remove from queue */
    struct zeroos_block_request **cursor=&dev->queue_head;
    while (*cursor && *cursor!=req) cursor=&(*cursor)->next;
    if (*cursor==req) {
        *cursor=req->next;
        if (dev->queue_tail==req) dev->queue_tail=0;
        if (dev->current_queue_depth) dev->current_queue_depth--;
    }
    req->next=0;
    (void)wait_queue_wake_all(&req->completion_waiters);
    (void)wait_queue_wake_one(&dev->queue_waiters);
    spin_unlock_irqrestore(&dev->lock,dflags);
    spin_unlock_irqrestore(&block_lock,flags);
    return 0;
}

int block_wait_request(uint64_t device_id, uint64_t request_id, uint64_t timeout_ticks) {
    uint64_t deadline=0;
    if (timeout_ticks) {
        deadline=timer_ticks()+timeout_ticks;
        if (deadline<timer_ticks()) deadline=~0ULL;
    }
    for (;;) {
        uint64_t flags = spin_lock_irqsave(&block_lock);
        struct zeroos_block_device *dev = device_lookup_locked(device_id);
        if (!dev) { spin_unlock_irqrestore(&block_lock,flags); return -1; }
        uint64_t dflags = spin_lock_irqsave(&dev->lock);
        struct zeroos_block_request *req=0;
        for (uint32_t i=0;i<ZEROOS_BLOCK_MAX_QUEUE_DEPTH;++i) {
            if (dev->requests[i].id==request_id) { req=&dev->requests[i]; break; }
        }
        if (!req) { spin_unlock_irqrestore(&dev->lock,dflags); spin_unlock_irqrestore(&block_lock,flags); return -1; }
        if (req->completed) {
            int err=req->error;
            req->id=0; /* free slot */
            req->completed=0;
            spin_unlock_irqrestore(&dev->lock,dflags);
            spin_unlock_irqrestore(&block_lock,flags);
            return err;
        }
        if (timeout_ticks && (long long)(deadline-timer_ticks())<=0) {
            spin_unlock_irqrestore(&dev->lock,dflags);
            spin_unlock_irqrestore(&block_lock,flags);
            return -1; /* timeout */
        }
        uint64_t wflags;
        if (wait_queue_prepare(&req->completion_waiters,&wflags)!=0) {
            spin_unlock_irqrestore(&dev->lock,dflags);
            spin_unlock_irqrestore(&block_lock,flags);
            return -1;
        }
        spin_unlock(&dev->lock);
        spin_unlock(&block_lock);
        if (wait_queue_commit(wflags)!=0) return -1;
    }
}

int block_cancel_request(uint64_t device_id, uint64_t request_id) {
    uint64_t flags = spin_lock_irqsave(&block_lock);
    struct zeroos_block_device *dev = device_lookup_locked(device_id);
    if (!dev) { spin_unlock_irqrestore(&block_lock,flags); return -1; }
    uint64_t dflags = spin_lock_irqsave(&dev->lock);
    struct zeroos_block_request *req=0;
    for (uint32_t i=0;i<ZEROOS_BLOCK_MAX_QUEUE_DEPTH;++i) {
        if (dev->requests[i].id==request_id) { req=&dev->requests[i]; break; }
    }
    if (!req || req->state!=ZEROOS_BLOCK_REQ_QUEUED) {
        spin_unlock_irqrestore(&dev->lock,dflags);
        spin_unlock_irqrestore(&block_lock,flags);
        return -1;
    }
    req->state=ZEROOS_BLOCK_REQ_CANCELED;
    req->completed=1;
    req->error=-1;
    struct zeroos_block_request **cursor=&dev->queue_head;
    while (*cursor && *cursor!=req) cursor=&(*cursor)->next;
    if (*cursor==req) {
        *cursor=req->next;
        if (dev->queue_tail==req) dev->queue_tail=0;
        if (dev->current_queue_depth) dev->current_queue_depth--;
    }
    (void)wait_queue_wake_all(&req->completion_waiters);
    spin_unlock_irqrestore(&dev->lock,dflags);
    spin_unlock_irqrestore(&block_lock,flags);
    return 0;
}

int block_device_stats(uint64_t device_id, struct zeroos_block_stats *stats_out) {
    if (!stats_out) return -1;
    uint64_t flags = spin_lock_irqsave(&block_lock);
    struct zeroos_block_device *dev = device_lookup_locked(device_id);
    if (!dev) { spin_unlock_irqrestore(&block_lock,flags); return -1; }
    uint64_t dflags = spin_lock_irqsave(&dev->lock);
    stats_out->devices=1;
    stats_out->total_requests=dev->completed_count+dev->error_count;
    stats_out->total_errors=dev->error_count;
    stats_out->total_timeouts=dev->timeout_count;
    stats_out->queue_depth_avg=dev->current_queue_depth;
    stats_out->latency_avg_ticks=0;
    spin_unlock_irqrestore(&dev->lock,dflags);
    spin_unlock_irqrestore(&block_lock,flags);
    return 0;
}

int block_debug_validate(void) {
    uint64_t flags = spin_lock_irqsave(&block_lock);
    for (uint32_t i=0;i<ZEROOS_BLOCK_MAX_DEVICES;++i) {
        struct zeroos_block_device *dev=&devices[i];
        if (!dev->used) {
            if (dev->current_queue_depth || dev->queue_head || dev->queue_tail) {
                spin_unlock_irqrestore(&block_lock,flags);
                return -1;
            }
            continue;
        }
        if (dev->capacity_sectors==0 || dev->sector_size==0 ||
            dev->queue_depth==0 || dev->queue_depth>ZEROOS_BLOCK_MAX_QUEUE_DEPTH ||
            dev->current_queue_depth>dev->queue_depth) {
            spin_unlock_irqrestore(&block_lock,flags);
            return -1;
        }
    }
    spin_unlock_irqrestore(&block_lock,flags);
    return 0;
}
