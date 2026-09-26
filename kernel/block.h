#ifndef ZEROOS_BLOCK_H
#define ZEROOS_BLOCK_H

#include "types.h"
#include "sync.h"
#include "wait.h"

/* Production block-device architecture — Stage 3.
 * Layers: device discovery → block device → request queue → scheduler → DMA/completion → cache/filesystem.
 * Supports: SATA/AHCI, NVMe, legacy, DMA, queueing, timeout, cancellation, error recovery, hotplug.
 * Never fakes success. All queues bounded, event-driven, with explicit ownership/lifetime.
 */

#define ZEROOS_BLOCK_MAX_DEVICES 16U
#define ZEROOS_BLOCK_MAX_QUEUE_DEPTH 64U
#define ZEROOS_BLOCK_SECTOR_SIZE 512U
#define ZEROOS_BLOCK_MAX_TRANSFER_SECTORS 256U
#define ZEROOS_BLOCK_MAX_NAME 32U

enum zeroos_block_device_type {
    ZEROOS_BLOCK_TYPE_UNKNOWN = 0,
    ZEROOS_BLOCK_TYPE_AHCI,
    ZEROOS_BLOCK_TYPE_NVME,
    ZEROOS_BLOCK_TYPE_RAMDISK,
    ZEROOS_BLOCK_TYPE_VIRTIO
};

enum zeroos_block_request_type {
    ZEROOS_BLOCK_REQ_READ = 0,
    ZEROOS_BLOCK_REQ_WRITE,
    ZEROOS_BLOCK_REQ_FLUSH,
    ZEROOS_BLOCK_REQ_DISCARD
};

enum zeroos_block_request_state {
    ZEROOS_BLOCK_REQ_QUEUED = 0,
    ZEROOS_BLOCK_REQ_DISPATCHED,
    ZEROOS_BLOCK_REQ_COMPLETED,
    ZEROOS_BLOCK_REQ_ERROR,
    ZEROOS_BLOCK_REQ_TIMEOUT,
    ZEROOS_BLOCK_REQ_CANCELED
};

enum zeroos_block_device_state {
    ZEROOS_BLOCK_DEV_STOPPED = 0,
    ZEROOS_BLOCK_DEV_DORMANT,
    ZEROOS_BLOCK_DEV_WARM,
    ZEROOS_BLOCK_DEV_ACTIVE,
    ZEROOS_BLOCK_DEV_THROTTLED,
    ZEROOS_BLOCK_DEV_SUSPENDED
};

struct zeroos_block_request {
    uint64_t id;
    enum zeroos_block_request_type type;
    enum zeroos_block_request_state state;
    uint64_t lba;
    uint64_t sector_count;
    void *buffer;
    uint64_t buffer_physical;
    uint64_t flags;
    uint64_t deadline_ticks;
    int error;
    uint64_t enqueue_ticks;
    uint64_t dispatch_ticks;
    uint64_t complete_ticks;
    struct zeroos_block_request *next;
    struct wait_queue completion_waiters;
    uint8_t completed;
};

struct zeroos_block_device {
    uint64_t id;
    uint32_t generation;
    uint8_t used;
    enum zeroos_block_device_type type;
    enum zeroos_block_device_state state;
    char name[ZEROOS_BLOCK_MAX_NAME];
    uint64_t capacity_sectors;
    uint64_t sector_size;
    uint64_t max_transfer_sectors;
    uint64_t queue_depth;
    uint64_t current_queue_depth;
    uint64_t timeout_ticks;
    uint64_t error_count;
    uint64_t timeout_count;
    uint64_t completed_count;
    uint64_t owner_cpu;
    struct spinlock lock;
    struct wait_queue queue_waiters;
    struct zeroos_block_request *queue_head;
    struct zeroos_block_request *queue_tail;
    struct zeroos_block_request requests[ZEROOS_BLOCK_MAX_QUEUE_DEPTH];
    /* I/O priority and scheduling */
    uint8_t io_priority; /* 0=low, 16=default, 31=high, foreground protected */
    uint8_t foreground;  /* 1 if foreground workload */
    uint64_t batch_count;
    uint64_t coalesced_writes;
    /* DMA and completion */
    void *dma_buffer;
    uint64_t dma_physical;
    uint64_t dma_size;
    /* Resource budgets */
    uint64_t memory_budget_bytes;
    uint64_t io_budget_iops;
    /* Diagnostics */
    uint64_t last_error_lba;
    uint64_t last_error_code;
};

struct zeroos_block_stats {
    uint64_t devices;
    uint64_t total_requests;
    uint64_t total_errors;
    uint64_t total_timeouts;
    uint64_t queue_depth_avg;
    uint64_t latency_avg_ticks;
};

/* API */
int block_system_init(void);
int block_device_register(enum zeroos_block_device_type type, const char *name,
                          uint64_t capacity_sectors, uint64_t sector_size,
                          uint64_t *device_id_out);
int block_device_unregister(uint64_t device_id);
struct zeroos_block_device *block_device_lookup(uint64_t device_id);
int block_device_set_state(uint64_t device_id, enum zeroos_block_device_state state);
int block_submit_request(uint64_t device_id, enum zeroos_block_request_type type,
                         uint64_t lba, uint64_t sector_count, void *buffer,
                         uint64_t timeout_ticks, uint64_t *request_id_out);
int block_wait_request(uint64_t device_id, uint64_t request_id, uint64_t timeout_ticks);
int block_cancel_request(uint64_t device_id, uint64_t request_id);
int block_device_stats(uint64_t device_id, struct zeroos_block_stats *stats_out);
int block_debug_validate(void);

/* Internal completion path called by drivers (AHCI/NVMe) from interrupt or task context */
int block_request_complete(uint64_t device_id, uint64_t request_id, int error);

#endif
