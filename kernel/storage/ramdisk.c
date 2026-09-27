#include "ramdisk.h"
#include "../kstring.h"
#include "../memory.h"

#define RAMDISK_SECTOR 512U
#define RAMDISK_MAX 4U

struct ramdisk_log_entry {
    uint64_t lba;
    uint32_t sectors;
    uint32_t reserved;
    uint8_t *old_data;
    uint8_t *new_data;
};

struct ramdisk {
    struct block_device *device;
    struct spinlock lock;
    uint64_t *pages;
    uint64_t page_count;
    uint64_t pointer_pages;
    struct block_request *pending_head;
    struct block_request *pending_tail;
    int track_cache;
    int paused;
    int powered_off;
    struct ramdisk_log_entry *log;
    uint32_t log_count;
    struct ramdisk_info info;
};

static struct ramdisk ramdisks[RAMDISK_MAX];

static uint8_t *ramdisk_sector_ptr(struct ramdisk *disk, uint64_t lba) {
    uint64_t byte=lba*RAMDISK_SECTOR;
    return (uint8_t *)(disk->pages[byte/ZEROOS_PAGE_SIZE]+(byte%ZEROOS_PAGE_SIZE));
}

static void ramdisk_log_free_entry(struct ramdisk_log_entry *entry) {
    if (entry->old_data)
        page_free(entry->old_data);
    if (entry->new_data)
        page_free(entry->new_data);
    entry->old_data=0;
    entry->new_data=0;
}

static void ramdisk_log_clear(struct ramdisk *disk) {
    for (uint32_t i=0; i<disk->log_count; ++i)
        ramdisk_log_free_entry(&disk->log[i]);
    disk->log_count=0;
}

/* Record a write chunk that lies within one backing page. */
static void ramdisk_log_write(struct ramdisk *disk, uint64_t lba,
                              uint32_t sectors, const uint8_t *new_data) {
    if (disk->log_count==RAMDISK_LOG_MAX) {
        ramdisk_log_free_entry(&disk->log[0]);
        memmove(&disk->log[0],&disk->log[1],
                (RAMDISK_LOG_MAX-1U)*sizeof(disk->log[0]));
        --disk->log_count;
        ++disk->info.log_evictions;
    }
    struct ramdisk_log_entry *entry=&disk->log[disk->log_count];
    entry->old_data=(uint8_t *)page_alloc();
    entry->new_data=(uint8_t *)page_alloc();
    if (!entry->old_data || !entry->new_data) {
        /* Cannot model this write's volatility: treat it as durable. */
        ramdisk_log_free_entry(entry);
        ++disk->info.log_evictions;
        return;
    }
    entry->lba=lba;
    entry->sectors=sectors;
    memcpy(entry->old_data,ramdisk_sector_ptr(disk,lba),sectors*RAMDISK_SECTOR);
    memcpy(entry->new_data,new_data,sectors*RAMDISK_SECTOR);
    ++disk->log_count;
}

static int ramdisk_execute(struct ramdisk *disk, struct block_request *request) {
    if (request->op==BLOCK_OP_FLUSH) {
        uint64_t flags=spin_lock_irqsave(&disk->lock);
        ramdisk_log_clear(disk);
        spin_unlock_irqrestore(&disk->lock,flags);
        return 0;
    }
    uint64_t lba=request->lba;
    for (uint32_t s=0; s<request->segment_count; ++s) {
        uint8_t *buffer=(uint8_t *)request->segments[s].physical;
        uint32_t remaining=request->segments[s].length;
        while (remaining) {
            uint64_t byte=lba*RAMDISK_SECTOR;
            uint32_t in_page=(uint32_t)(ZEROOS_PAGE_SIZE-(byte%ZEROOS_PAGE_SIZE));
            uint32_t length=remaining<in_page ? remaining : in_page;
            uint32_t sectors=length/RAMDISK_SECTOR;
            if (request->op==BLOCK_OP_READ) {
                memcpy(buffer,ramdisk_sector_ptr(disk,lba),length);
            } else {
                uint64_t flags=spin_lock_irqsave(&disk->lock);
                if (disk->powered_off) {
                    /* Power was cut while this request was in flight. */
                    spin_unlock_irqrestore(&disk->lock,flags);
                    return -SE_NODEV;
                }
                if (disk->track_cache)
                    ramdisk_log_write(disk,lba,sectors,buffer);
                memcpy(ramdisk_sector_ptr(disk,lba),buffer,length);
                spin_unlock_irqrestore(&disk->lock,flags);
            }
            buffer+=length;
            remaining-=length;
            lba+=sectors;
        }
    }
    return 0;
}

static int ramdisk_submit(struct block_device *device, struct block_request *request) {
    struct ramdisk *disk=(struct ramdisk *)device->driver_data;
    uint64_t flags=spin_lock_irqsave(&disk->lock);
    if (disk->info.trace_count<RAMDISK_TRACE_MAX)
        disk->info.trace[disk->info.trace_count++]=
            request->op==BLOCK_OP_FLUSH ? ~0ULL : request->lba;
    request->driver_next=0;
    if (disk->pending_tail)
        disk->pending_tail->driver_next=request;
    else
        disk->pending_head=request;
    disk->pending_tail=request;
    spin_unlock_irqrestore(&disk->lock,flags);
    return 0;
}

static void ramdisk_kick(struct block_device *device, uint32_t hw_queue) {
    struct ramdisk *disk=(struct ramdisk *)device->driver_data;
    (void)hw_queue;
    for (;;) {
        uint64_t flags=spin_lock_irqsave(&disk->lock);
        if (disk->paused || !disk->pending_head) {
            spin_unlock_irqrestore(&disk->lock,flags);
            return;
        }
        struct block_request *request=disk->pending_head;
        disk->pending_head=request->driver_next;
        if (!disk->pending_head)
            disk->pending_tail=0;
        request->driver_next=0;
        int off=disk->powered_off;
        spin_unlock_irqrestore(&disk->lock,flags);
        int status=off ? -SE_NODEV : ramdisk_execute(disk,request);
        block_complete(request,status);
    }
}

static void ramdisk_poll(struct block_device *device) {
    ramdisk_kick(device,0);
}

/* Controller reset: every command the "hardware" still holds is aborted
 * and reported as timed out (the block layer retries it). */
static int ramdisk_reset(struct block_device *device) {
    struct ramdisk *disk=(struct ramdisk *)device->driver_data;
    uint64_t flags=spin_lock_irqsave(&disk->lock);
    struct block_request *list=disk->pending_head;
    disk->pending_head=0;
    disk->pending_tail=0;
    spin_unlock_irqrestore(&disk->lock,flags);
    while (list) {
        struct block_request *next=list->driver_next;
        list->driver_next=0;
        block_complete(list,-SE_TIMEDOUT);
        list=next;
    }
    return 0;
}

static uint32_t ramdisk_rng(uint32_t *state) {
    uint32_t x=*state ? *state : 0x9e3779b9U;
    x^=x<<13;
    x^=x>>17;
    x^=x<<5;
    *state=x;
    return x;
}

static void ramdisk_power_cut(struct block_device *device, uint32_t policy,
                              uint32_t seed) {
    struct ramdisk *disk=(struct ramdisk *)device->driver_data;
    uint64_t flags=spin_lock_irqsave(&disk->lock);
    disk->powered_off=1;
    ++disk->info.power_cuts;
    if (policy!=BLOCK_POWER_KEEP_ALL) {
        for (uint32_t i=disk->log_count; i>0; --i) {
            struct ramdisk_log_entry *e=&disk->log[i-1U];
            memcpy(ramdisk_sector_ptr(disk,e->lba),e->old_data,
                   e->sectors*RAMDISK_SECTOR);
        }
        uint32_t state=seed;
        for (uint32_t i=0; i<disk->log_count; ++i) {
            struct ramdisk_log_entry *e=&disk->log[i];
            int keep=policy!=BLOCK_POWER_DROP_ALL && (ramdisk_rng(&state)&1U);
            if (!keep) {
                ++disk->info.writes_dropped;
                continue;
            }
            uint32_t sectors=e->sectors;
            if (policy==BLOCK_POWER_TORN && sectors>1U && (ramdisk_rng(&state)&1U)) {
                sectors/=2U;
                ++disk->info.writes_torn;
            }
            memcpy(ramdisk_sector_ptr(disk,e->lba),e->new_data,
                   sectors*RAMDISK_SECTOR);
        }
    }
    ramdisk_log_clear(disk);
    spin_unlock_irqrestore(&disk->lock,flags);
}

static const struct block_device_ops ramdisk_ops={
    .submit=ramdisk_submit,
    .kick=ramdisk_kick,
    .poll=ramdisk_poll,
    .reset=ramdisk_reset,
    .power_cut=ramdisk_power_cut,
};

struct block_device *ramdisk_create(const char *name, uint64_t bytes,
                                    uint32_t media, uint32_t queue_depth) {
    struct ramdisk *disk=0;
    for (uint32_t i=0; i<RAMDISK_MAX; ++i) {
        if (!ramdisks[i].device) {
            disk=&ramdisks[i];
            break;
        }
    }
    if (!disk || bytes==0 || (bytes%ZEROOS_PAGE_SIZE)!=0)
        return 0;
    memset(disk,0,sizeof(*disk));
    spinlock_init(&disk->lock);
    disk->page_count=bytes/ZEROOS_PAGE_SIZE;
    disk->pointer_pages=(disk->page_count*8ULL+ZEROOS_PAGE_SIZE-1ULL)/ZEROOS_PAGE_SIZE;
    disk->pages=(uint64_t *)page_alloc_contiguous(disk->pointer_pages);
    uint64_t log_pages=(RAMDISK_LOG_MAX*sizeof(struct ramdisk_log_entry)+
                        ZEROOS_PAGE_SIZE-1ULL)/ZEROOS_PAGE_SIZE;
    disk->log=(struct ramdisk_log_entry *)page_alloc_contiguous(log_pages);
    if (!disk->pages || !disk->log)
        goto fail;
    memset(disk->log,0,log_pages*ZEROOS_PAGE_SIZE);
    for (uint64_t i=0; i<disk->page_count; ++i) {
        void *page=page_alloc_zero();
        if (!page) {
            for (uint64_t j=0; j<i; ++j)
                page_free((void *)disk->pages[j]);
            goto fail;
        }
        disk->pages[i]=(uint64_t)page;
    }
    struct block_device *device=block_device_alloc();
    if (!device) {
        for (uint64_t j=0; j<disk->page_count; ++j)
            page_free((void *)disk->pages[j]);
        goto fail;
    }
    ksnprintf(device->name,sizeof(device->name),"%s",name);
    device->ops=&ramdisk_ops;
    device->driver_data=disk;
    device->sector_size=RAMDISK_SECTOR;
    device->sectors=bytes/RAMDISK_SECTOR;
    device->max_sectors=128U;
    device->queue_depth=queue_depth ? queue_depth : 4U;
    device->hw_queues=1;
    device->media=media;
    device->flags=BLOCK_DEV_VOLATILE_CACHE;
    device->timeout_ticks=50U;
    disk->device=device;
    if (block_register(device)!=0) {
        device->in_use=0;
        disk->device=0;
        return 0;
    }
    return device;
fail:
    if (disk->pages)
        page_free_contiguous(disk->pages,disk->pointer_pages);
    if (disk->log)
        page_free_contiguous(disk->log,log_pages);
    disk->pages=0;
    disk->log=0;
    return 0;
}

void ramdisk_set_cache_tracking(struct block_device *device, int enabled) {
    struct ramdisk *disk=(struct ramdisk *)block_root(device)->driver_data;
    uint64_t flags=spin_lock_irqsave(&disk->lock);
    if (!enabled)
        ramdisk_log_clear(disk);
    disk->track_cache=enabled;
    spin_unlock_irqrestore(&disk->lock,flags);
}

void ramdisk_pause(struct block_device *device, int paused) {
    struct block_device *root=block_root(device);
    struct ramdisk *disk=(struct ramdisk *)root->driver_data;
    uint64_t flags=spin_lock_irqsave(&disk->lock);
    disk->paused=paused;
    spin_unlock_irqrestore(&disk->lock,flags);
    if (!paused)
        ramdisk_kick(root,0);
}

void ramdisk_power_on(struct block_device *device) {
    struct block_device *root=block_root(device);
    struct ramdisk *disk=(struct ramdisk *)root->driver_data;
    uint64_t flags=spin_lock_irqsave(&disk->lock);
    disk->powered_off=0;
    spin_unlock_irqrestore(&disk->lock,flags);
    block_device_set_state(root,BLOCK_STATE_ONLINE);
}

void ramdisk_info(struct block_device *device, struct ramdisk_info *out) {
    struct ramdisk *disk=(struct ramdisk *)block_root(device)->driver_data;
    uint64_t flags=spin_lock_irqsave(&disk->lock);
    *out=disk->info;
    spin_unlock_irqrestore(&disk->lock,flags);
}

void ramdisk_trace_reset(struct block_device *device) {
    struct ramdisk *disk=(struct ramdisk *)block_root(device)->driver_data;
    uint64_t flags=spin_lock_irqsave(&disk->lock);
    disk->info.trace_count=0;
    spin_unlock_irqrestore(&disk->lock,flags);
}

int ramdisk_peek(struct block_device *device, uint64_t lba, void *buffer,
                 uint32_t sectors) {
    struct block_device *root=block_root(device);
    struct ramdisk *disk=(struct ramdisk *)root->driver_data;
    lba+=device->start_lba;
    if (lba+sectors>root->sectors)
        return -SE_INVAL;
    for (uint32_t i=0; i<sectors; ++i)
        memcpy((uint8_t *)buffer+i*RAMDISK_SECTOR,ramdisk_sector_ptr(disk,lba+i),
               RAMDISK_SECTOR);
    return 0;
}

int ramdisk_poke(struct block_device *device, uint64_t lba, const void *buffer,
                 uint32_t sectors) {
    struct block_device *root=block_root(device);
    struct ramdisk *disk=(struct ramdisk *)root->driver_data;
    lba+=device->start_lba;
    if (lba+sectors>root->sectors)
        return -SE_INVAL;
    for (uint32_t i=0; i<sectors; ++i)
        memcpy(ramdisk_sector_ptr(disk,lba+i),(const uint8_t *)buffer+i*RAMDISK_SECTOR,
               RAMDISK_SECTOR);
    return 0;
}
