#ifndef ZEROOS_STORAGE_H
#define ZEROOS_STORAGE_H
#include "../types.h"

/* Starts the storage manager task (idempotent). Called by the boot monitor
 * after the Stage 2 userspace gate. The task discovers controllers, runs the
 * storage self-tests, mounts the root filesystem and launches the Ring-3
 * storage probe. */
void storage_start(void);
/* 1 once the storage manager finished (success or reported failure). */
int storage_finished(void);
/* 1 once the full Stage 3 certification (including the Ring-3 probe)
 * completed. */
int storage_certification_complete(void);

/* Orderly storage quiesce for shutdown (called by power_request only):
 * sync every mount, unmount all (journal commit + CLEAN superblock), flush
 * the write cache of every AHCI/NVMe disk, then NVMe CC.SHN. Every step runs
 * even if an earlier one failed; the report carries each result. */
struct storage_shutdown_report {
    int sync_result;
    int unmount_result;
    uint32_t mounts;
    uint32_t disks_flushed;
    uint32_t flush_failures;
    int nvme_timeouts;
};
void storage_shutdown(struct storage_shutdown_report *report);
/* Periodic hook from the boot monitor (non-blocking). */
void storage_service_step(void);

/* Test helpers shared by the self-test units. */
int storage_selftest_block(void);
int storage_selftest_gpt(void);
int storage_selftest_fs(void);
int storage_selftest_crash(void);
struct block_device;
int storage_selftest_driver(struct block_device *disk);

#endif
