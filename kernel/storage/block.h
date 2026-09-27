#ifndef ZEROOS_BLOCK_H
#define ZEROOS_BLOCK_H
#include "../types.h"
#include "../sync.h"
#include "../ksync.h"
#include "errno.h"

/*
 * ZEROOS block layer.
 *
 *   discovery (PCI/AHCI/NVMe/ramdisk) -> struct block_device (+ partition
 *   views) -> bounded request pool -> per-device scheduler queues
 *   (priority + aging + HDD C-SCAN / SSD FIFO / NVMe multi-queue) ->
 *   driver submit/kick (DMA) -> IRQ (or degraded poll) completion ->
 *   retry / timeout / reset / failure accounting -> caller callback.
 *
 * Ownership & concurrency
 *   - Every request belongs to exactly one physical device pool; it is
 *     allocated with block_request_alloc() and returned with
 *     block_request_free() by whoever owns it after completion (the
 *     synchronous helper, or the async `done` callback).
 *   - dev->lock (irqsave spinlock) protects queues, in-flight list, pool and
 *     statistics. ops->submit() runs under it and must not block;
 *     ops->kick/poll/reset() run without it. Drivers complete requests with
 *     block_complete() from IRQ or task context after dropping their own
 *     locks.
 *   - Completion callbacks run in IRQ context: they must not block.
 *
 * Boundedness: requests per device = pool size (<= BLOCK_POOL_MAX); queued
 * requests never exceed the pool; background I/O may not consume the last
 * quarter of the pool nor more than a quarter of the hardware depth.
 */
#define BLOCK_MAX_DEVICES 32U
#define BLOCK_MAX_SEGMENTS 32U
#define BLOCK_POOL_MAX 64U
#define BLOCK_NAME_MAX 16U
#define BLOCK_MAX_RETRIES 2U
#define BLOCK_AGING_TICKS 50U
#define BLOCK_LAT_BUCKETS 24U

enum block_op {
    BLOCK_OP_READ=0,
    BLOCK_OP_WRITE=1,
    BLOCK_OP_FLUSH=2
};

enum block_priority {
    BLOCK_PRIO_FOREGROUND=0,    /* synchronous user I/O, fsync, journal */
    BLOCK_PRIO_NORMAL=1,        /* metadata reads, mount */
    BLOCK_PRIO_BACKGROUND=2     /* writeback, readahead, scrub */
};
#define BLOCK_PRIO_COUNT 3U

enum block_media {
    BLOCK_MEDIA_RAM=0,
    BLOCK_MEDIA_HDD=1,          /* seek-sensitive: C-SCAN ordering */
    BLOCK_MEDIA_SSD=2,          /* FIFO within priority */
    BLOCK_MEDIA_NVME=3          /* FIFO, multiple hardware queues */
};

enum block_state {
    BLOCK_STATE_ONLINE=0,
    BLOCK_STATE_RESETTING=1,
    BLOCK_STATE_FAILED=2,       /* recovery exhausted: all I/O -EIO */
    BLOCK_STATE_GONE=3,         /* surprise removal: all I/O -ENODEV */
    BLOCK_STATE_POWERED_OFF=4   /* test-only simulated power loss */
};

enum block_request_state {
    BLOCK_REQ_FREE=0,
    BLOCK_REQ_ALLOCATED=1,
    BLOCK_REQ_QUEUED=2,
    BLOCK_REQ_DISPATCHED=3,
    BLOCK_REQ_COMPLETE=4
};

#define BLOCK_DEV_READ_ONLY (1U << 0)
#define BLOCK_DEV_POLLED    (1U << 1)   /* no usable interrupt: watchdog polls */
#define BLOCK_DEV_VOLATILE_CACHE (1U << 2)
#define BLOCK_DEV_REMOVABLE (1U << 3)
/* Driver requires PRP-style segments: every segment boundary inside a
 * request is page aligned (NVMe). Merges respect this. */
#define BLOCK_DEV_PRP_SEGMENTS (1U << 4)

struct block_segment {
    uint64_t physical;
    uint32_t length;
    uint32_t reserved;
};

struct block_request;
typedef void (*block_done_t)(struct block_request *request);

struct block_request {
    struct block_device *device;        /* physical device (pool owner) */
    struct block_request *next;         /* queue / in-flight link */
    struct block_request *merged_next;  /* requests merged into this one */
    struct block_request *driver_next;  /* driver-private link while issued */
    uint8_t op;
    uint8_t priority;
    uint8_t state;
    uint8_t hw_queue;
    uint8_t retries;
    uint8_t injected;                   /* fault-injection disposition */
    uint16_t segment_count;
    uint16_t own_segment_count;         /* before merges */
    uint16_t pool_index;
    uint32_t sectors;
    uint32_t own_sectors;
    uint32_t tag;                       /* driver command slot */
    uint64_t lba;                       /* physical-device LBA */
    uint64_t sequence;                  /* ordering vs. flush barriers */
    int32_t status;                     /* 0 or -SE_* */
    uint32_t flags;
    uint64_t submit_tick;
    uint64_t dispatch_tick;
    uint64_t submit_ns;
    block_done_t done;
    void *private_data;
    struct kcompletion completion;      /* used by synchronous helpers */
    struct block_segment segments[BLOCK_MAX_SEGMENTS];
};

struct block_device;

struct block_device_ops {
    /* Accept `request` for hardware queue request->hw_queue. Called with
     * dev->lock held (IRQs off). Return 0 (issued), -SE_AGAIN (no slot;
     * request is requeued) or another -SE_* (fails the request). */
    int (*submit)(struct block_device *device, struct block_request *request);
    /* Ring doorbells for everything submitted since the last kick. */
    void (*kick)(struct block_device *device, uint32_t hw_queue);
    /* Harvest completions without an interrupt (lost IRQ / polled mode). */
    void (*poll)(struct block_device *device);
    /* Controller/port recovery after a timeout. Must complete every
     * driver-owned in-flight request with block_complete() (usually
     * -SE_TIMEDOUT, which the block layer retries). Returns 0 when the
     * device is usable again. */
    int (*reset)(struct block_device *device);
    /* Optional (simulation devices only): drop/keep/tear the writes that
     * are still in the volatile cache, per BLOCK_POWER_* policy. Called with
     * dev->lock held. */
    void (*power_cut)(struct block_device *device, uint32_t policy,
                      uint32_t seed);
};

#define BLOCK_POWER_DROP_ALL 0U     /* nothing since the last flush survives */
#define BLOCK_POWER_KEEP_ALL 1U     /* cache was written back before loss */
#define BLOCK_POWER_SUBSET 2U       /* deterministic pseudo-random subset */
#define BLOCK_POWER_TORN 3U         /* subset, chosen writes torn in half */

struct block_fault {
    uint32_t mode;
    uint32_t op_mask;           /* 1<<BLOCK_OP_* */
    uint64_t lba_start;
    uint64_t lba_end;           /* exclusive; 0 = whole device */
    uint32_t remaining;         /* 0 = unlimited while armed */
    uint32_t after;             /* matching requests to let through first */
    uint32_t power_policy;      /* BLOCK_FAULT_POWER_CUT */
    uint32_t power_seed;
    uint64_t hits;
};

#define BLOCK_FAULT_NONE 0U
#define BLOCK_FAULT_EIO 1U          /* complete with -SE_IO, no data moved */
#define BLOCK_FAULT_STALL 2U        /* never completes -> timeout path */
#define BLOCK_FAULT_DISAPPEAR 3U    /* device vanishes at the matching I/O */
#define BLOCK_FAULT_POWER_CUT 4U    /* simulated power loss (ramdisk) */

struct block_stats {
    uint64_t submitted[3];
    uint64_t completed[3];
    uint64_t sectors[3];
    uint64_t errors[3];
    uint64_t latency_ns_total[3];
    uint64_t latency_ns_max[3];
    uint64_t latency_hist[BLOCK_LAT_BUCKETS];   /* log2(us) buckets */
    uint64_t merges;
    uint64_t retries;
    uint64_t timeouts;
    uint64_t resets;
    uint64_t cancels;
    uint64_t aging_promotions;
    uint64_t background_throttled;
    uint64_t pool_waits;
    uint64_t dispatched_by_prio[BLOCK_PRIO_COUNT];
    uint64_t max_queued;
    uint64_t max_inflight;
    uint64_t interrupts;
    uint64_t polls;
    uint64_t faults_injected;
};

struct block_device {
    char name[BLOCK_NAME_MAX];
    struct spinlock lock;
    const struct block_device_ops *ops;
    void *driver_data;
    struct block_device *parent;        /* partitions: view onto parent */
    uint64_t start_lba;                 /* partitions */
    uint64_t sectors;                   /* capacity in logical sectors */
    uint32_t sector_size;
    uint32_t max_sectors;               /* per request */
    uint32_t queue_depth;               /* hardware in-flight limit */
    uint32_t hw_queues;
    uint32_t media;
    uint32_t flags;
    uint32_t timeout_ticks;
    uint32_t aging_ticks;               /* starvation promotion threshold */
    uint32_t state;
    uint32_t refcount;                  /* openers (fs, partitions) */
    uint32_t index;
    uint32_t partition_number;
    uint8_t in_use;
    uint8_t running;
    uint8_t rerun;
    uint8_t reserved;
    /* scheduler */
    struct block_request *queue_head[BLOCK_PRIO_COUNT];
    struct block_request *queue_tail[BLOCK_PRIO_COUNT];
    struct block_request *inflight;
    struct block_request *flush_waiting;    /* FIFO of pending barriers */
    uint32_t queued;
    uint32_t inflight_count;
    uint32_t inflight_background;
    uint64_t head_position;             /* HDD elevator position */
    uint64_t next_sequence;
    /* pool */
    struct block_request *pool;
    uint32_t pool_size;
    uint32_t pool_free_count;
    struct block_request *pool_free;
    struct wait_queue pool_waiters;
    /* failure injection (test builds and runtime-armed tests only) */
    struct block_fault fault;
    struct block_stats stats;
    uint8_t guid[16];                   /* partition unique GUID */
    uint8_t type_guid[16];
};

int block_init(void);
int block_register(struct block_device *device);
struct block_device *block_device_alloc(void);
struct block_device *block_device_at(uint32_t index);
struct block_device *block_find(const char *name);
struct block_device *block_root(struct block_device *device);
uint32_t block_device_count(void);
int block_open(struct block_device *device);
void block_close(struct block_device *device);
int block_add_partition(struct block_device *disk, uint32_t number,
                        uint64_t first_lba, uint64_t sectors,
                        const uint8_t type_guid[16],
                        const uint8_t unique_guid[16],
                        struct block_device **out);

/* Request lifecycle. `wait` = 1 blocks (task context) until a pool slot is
 * available; 0 returns 0 when the pool is exhausted. */
struct block_request *block_request_alloc(struct block_device *device,
                                          uint8_t priority, int wait);
void block_request_free(struct block_request *request);
int block_request_add_buffer(struct block_request *request, uint64_t physical,
                             uint32_t length);
/* Submit (partition LBAs are remapped, bounds/RO/state checked). On
 * immediate failure the request is completed with the error. */
void block_submit(struct block_device *device, struct block_request *request);
int block_wait(struct block_request *request);
int block_cancel(struct block_request *request);

/* Called by drivers (any context) when hardware finishes a request. */
void block_complete(struct block_request *request, int status);
/* Called by drivers from their interrupt handler (statistics only). */
void block_note_interrupt(struct block_device *device);
void block_device_set_state(struct block_device *device, uint32_t state);

/* Synchronous helper: `buffer` is a kernel (identity-mapped) address. */
int block_rw(struct block_device *device, uint32_t op, uint64_t lba,
             void *buffer, uint64_t bytes, uint8_t priority);
int block_flush(struct block_device *device, uint8_t priority);

/* Fault injection (applies to the physical device of `device`). */
void block_fault_set(struct block_device *device, const struct block_fault *fault);
void block_fault_clear(struct block_device *device);

void block_stats_snapshot(struct block_device *device, struct block_stats *out,
                          uint32_t *queued, uint32_t *inflight);
void block_report(struct block_device *device);
void block_watchdog_start(void);
int block_reset_device(struct block_device *device);
int block_idle(struct block_device *device);

#endif
