#ifndef ZEROOS_RAMDISK_H
#define ZEROOS_RAMDISK_H
#include "block.h"

/*
 * Memory-backed block device used for (a) boot-time storage self-tests on
 * machines without disks and (b) deterministic crash-consistency testing.
 *
 * Volatile-cache model (when `track_cache` is on): every write is applied to
 * the image immediately (reads observe it) and also recorded with its old
 * and new contents until the next FLUSH makes it durable. A simulated power
 * cut then rewinds to the last flushed state and re-applies none, all, a
 * deterministic subset, or torn halves of the unflushed writes — the
 * reorder/partial-persistence behaviour allowed by real write caches. The
 * log is bounded (RAMDISK_LOG_MAX chunks); overflow models cache eviction:
 * the oldest chunk is treated as written back (durable).
 */
#define RAMDISK_LOG_MAX 384U
#define RAMDISK_TRACE_MAX 64U

struct ramdisk_info {
    uint64_t trace[RAMDISK_TRACE_MAX];  /* dispatch-order LBAs */
    uint32_t trace_count;
    uint64_t log_evictions;
    uint64_t power_cuts;
    uint64_t writes_dropped;
    uint64_t writes_torn;
};

struct block_device *ramdisk_create(const char *name, uint64_t bytes,
                                    uint32_t media, uint32_t queue_depth);
void ramdisk_set_cache_tracking(struct block_device *device, int enabled);
void ramdisk_pause(struct block_device *device, int paused);
void ramdisk_power_on(struct block_device *device);
void ramdisk_info(struct block_device *device, struct ramdisk_info *out);
void ramdisk_trace_reset(struct block_device *device);
/* Direct image access for tests (bypasses the block layer). */
int ramdisk_peek(struct block_device *device, uint64_t lba, void *buffer,
                 uint32_t sectors);
int ramdisk_poke(struct block_device *device, uint64_t lba, const void *buffer,
                 uint32_t sectors);

#endif
