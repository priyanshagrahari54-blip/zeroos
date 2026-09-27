/*
 * Hardware driver certification (AHCI / NVMe) on real or emulated disks.
 *
 * Safety policy: whole-disk tests are read-only. Destructive tests
 * (writes, queue-depth, flush, lost-interrupt, reset-with-I/O-in-flight)
 * run ONLY inside a partition whose GPT type is ZEROOS-scratch
 * (8F3D2A11-5A4A-4653-9A2E-5A45524F4F53). Without one they are skipped
 * and the skip is reported explicitly — never counted as a pass.
 */
#include "ahci.h"
#include "block.h"
#include "gpt.h"
#include "nvme.h"
#include "storage.h"
#include "../kstring.h"
#include "../memory.h"
#include "../task.h"
#include "../timer.h"

/* Build with -DZEROOS_TEST_RESET_REPS=N to stress the reset path. */
#ifndef ZEROOS_TEST_RESET_REPS
#define ZEROOS_TEST_RESET_REPS 1U
#endif

#define TD_CHECK(cond, what) do { if (!(cond)) { \
    klog("ZEROOS: storage driver test FAILED on %s: %s (line %d).", disk->name, what, __LINE__); \
    return -1; } } while (0)

static void td_fill(uint8_t *buffer, uint64_t length, uint32_t seed) {
    uint32_t x=seed*2246822519U+7U;
    for (uint64_t i=0; i<length; ++i) {
        x^=x<<13; x^=x>>17; x^=x<<5;
        buffer[i]=(uint8_t)x;
    }
}

static struct block_device *td_scratch_of(struct block_device *disk) {
    uint32_t count=block_device_count();
    for (uint32_t i=0; i<count; ++i) {
        struct block_device *d=block_device_at(i);
        if (d && d->parent==disk && gpt_guid_equal(d->type_guid,gpt_type_zeroos_scratch))
            return d;
    }
    return 0;
}

static int td_mask_irq(struct block_device *disk, int masked) {
    if (ahci_is_ahci_device(disk))
        return ahci_test_mask_irq(disk,masked);
    if (nvme_is_nvme_device(disk))
        return nvme_test_mask_irq(disk,masked);
    return -SE_NOTSUP;
}

static int td_readonly(struct block_device *disk, uint8_t *buffer) {
    uint32_t ss=disk->sector_size;
    TD_CHECK(block_rw(disk,BLOCK_OP_READ,0,buffer,ss,BLOCK_PRIO_NORMAL)==0,"read LBA 0");
    TD_CHECK(block_rw(disk,BLOCK_OP_READ,1,buffer,ss,BLOCK_PRIO_NORMAL)==0,"read LBA 1");
    TD_CHECK(block_rw(disk,BLOCK_OP_READ,disk->sectors-1U,buffer,ss,BLOCK_PRIO_NORMAL)==0,
             "read last LBA");
    TD_CHECK(block_rw(disk,BLOCK_OP_READ,disk->sectors,buffer,ss,BLOCK_PRIO_NORMAL)==-SE_INVAL,
             "read beyond capacity rejected");
    uint64_t span=(uint64_t)disk->max_sectors*ss;
    if (span>128U*1024U)
        span=128U*1024U;
    TD_CHECK(block_rw(disk,BLOCK_OP_READ,0,buffer,span,BLOCK_PRIO_NORMAL)==0,
             "max-size read");
    return 0;
}

static int td_destructive(struct block_device *disk, struct block_device *part,
                          uint8_t *a, uint8_t *b) {
    struct block_device *root=disk;
    uint32_t ss=root->sector_size;
    uint32_t per4k=4096U/ss;
    TD_CHECK(part->sectors>=8192U,"scratch partition >= 4 MiB");
    /* 1. single 4 KiB write/read */
    td_fill(a,4096,1);
    TD_CHECK(block_rw(part,BLOCK_OP_WRITE,0,a,4096,BLOCK_PRIO_NORMAL)==0,"4K write");
    TD_CHECK(block_rw(part,BLOCK_OP_READ,0,b,4096,BLOCK_PRIO_NORMAL)==0 &&
             memcmp(a,b,4096)==0,"4K verify");
    /* 2. 128 KiB scatter-gather request from non-adjacent pages */
    struct block_request *r=block_request_alloc(part,BLOCK_PRIO_NORMAL,1);
    td_fill(a,32U*4096U,2);
    for (uint32_t i=0; i<32; ++i)
        (void)block_request_add_buffer(r,(uint64_t)(a+((i*7U)%32U)*4096U),4096);
    r->op=BLOCK_OP_WRITE;
    r->lba=64U*per4k;
    block_submit(part,r);
    TD_CHECK(block_wait(r)==0,"128K scatter-gather write");
    block_request_free(r);
    TD_CHECK(block_rw(part,BLOCK_OP_READ,64U*per4k,b,32U*4096U,BLOCK_PRIO_NORMAL)==0,
             "128K read");
    for (uint32_t i=0; i<32; ++i)
        TD_CHECK(memcmp(b+i*4096U,a+((i*7U)%32U)*4096U,4096)==0,"scatter-gather order");
    /* 3. queue depth: 32 concurrent 4K writes then reads */
    struct block_stats before;
    block_stats_snapshot(root,&before,0,0);
    static struct block_request *batch[32];
    td_fill(a,32U*4096U,3);
    for (uint32_t pass=0; pass<2; ++pass) {
        for (uint32_t i=0; i<32; ++i) {
            batch[i]=block_request_alloc(part,BLOCK_PRIO_NORMAL,1);
            batch[i]->op=pass ? BLOCK_OP_READ : BLOCK_OP_WRITE;
            batch[i]->lba=(200U+i*3U)*per4k;             /* non-contiguous */
            (void)block_request_add_buffer(batch[i],(uint64_t)((pass ? b : a)+i*4096U),4096);
            block_submit(part,batch[i]);
        }
        for (uint32_t i=0; i<32; ++i) {
            TD_CHECK(block_wait(batch[i])==0,"queued I/O");
            block_request_free(batch[i]);
        }
    }
    TD_CHECK(memcmp(a,b,32U*4096U)==0,"queued I/O data");
    struct block_stats after;
    block_stats_snapshot(root,&after,0,0);
    klog("ZEROOS: storage driver %s: queued I/O max_inflight=%llu (hw depth %u, hwq %u).",
         disk->name,after.max_inflight,root->queue_depth,root->hw_queues);
    /* 4. cache flush */
    TD_CHECK(block_flush(part,BLOCK_PRIO_NORMAL)==0,"flush");
    /* 5. lost interrupt: completion must be harvested by the watchdog
     * poll without a reset. */
    if (!(root->flags&BLOCK_DEV_POLLED)) {
        uint32_t saved=root->timeout_ticks;
        root->timeout_ticks=30;
        block_stats_snapshot(root,&before,0,0);
        TD_CHECK(td_mask_irq(disk,1)==0,"mask interrupt");
        int rc=block_rw(part,BLOCK_OP_READ,0,b,4096,BLOCK_PRIO_NORMAL);
        (void)td_mask_irq(disk,0);
        root->timeout_ticks=saved;
        block_stats_snapshot(root,&after,0,0);
        td_fill(a,4096,1);
        TD_CHECK(rc==0 && memcmp(a,b,4096)==0,"I/O completes with interrupt lost");
        TD_CHECK(after.polls>before.polls,"watchdog poll harvested completion");
        TD_CHECK(after.resets==before.resets,"no reset needed for lost interrupt");
        klog("ZEROOS: storage driver %s: lost-interrupt recovery by poll (polls +%llu, resets +0).",
             disk->name,after.polls-before.polls);
    } else {
        klog("ZEROOS: storage driver %s: polled mode; lost-interrupt test not applicable.",
             disk->name);
    }
    /* 6. controller/port reset with I/O genuinely in flight: completions
     * are held back by masking the interrupt, the requests are observed
     * in flight, then the controller is reset. The driver must fail them
     * back (TIMEDOUT) and the block layer must retry them to success. */
    for (uint32_t rep=0; rep<ZEROOS_TEST_RESET_REPS; ++rep) {
        td_fill(a,32U*4096U,4);
        TD_CHECK(block_rw(part,BLOCK_OP_WRITE,400U*per4k,a,32U*4096U,BLOCK_PRIO_NORMAL)==0,
                 "reset test data");
        memset(b,0,16U*4096U);
        block_stats_snapshot(root,&before,0,0);
        int masked=!(root->flags&BLOCK_DEV_POLLED) && td_mask_irq(disk,1)==0;
        for (uint32_t i=0; i<16; ++i) {
            batch[i]=block_request_alloc(part,BLOCK_PRIO_NORMAL,1);
            batch[i]->op=BLOCK_OP_READ;
            batch[i]->lba=(400U+i*2U)*per4k;               /* no merging */
            (void)block_request_add_buffer(batch[i],(uint64_t)(b+i*4096U),4096);
            block_submit(part,batch[i]);
        }
        uint32_t queued=0, inflight=0;
        for (uint32_t spin=0; spin<50; ++spin) {
            block_stats_snapshot(root,&after,&queued,&inflight);
            if (inflight>=root->queue_depth || inflight+queued>=16U)
                break;
            task_sleep_ticks(1);
        }
        int reset_rc=block_reset_device(root);
        if (masked)
            (void)td_mask_irq(disk,0);
        int all_ok=1;
        for (uint32_t i=0; i<16; ++i) {
            if (block_wait(batch[i])!=0)
                all_ok=0;
            block_request_free(batch[i]);
        }
        block_stats_snapshot(root,&after,0,0);
        TD_CHECK(reset_rc==0,"reset succeeded");
        TD_CHECK(inflight>0,"requests in flight at reset");
        TD_CHECK(all_ok,"in-flight I/O completes after reset");
        for (uint32_t i=0; i<16; ++i)
            TD_CHECK(memcmp(b+i*4096U,a+(i*2U)*4096U,4096)==0,"data read across reset");
        TD_CHECK(after.resets==before.resets+1U,"reset accounted");
        /* Recovery first harvests commands the device already finished
         * (success, no duplicate I/O) and fails only the rest for retry. QEMU
         * finishes commands immediately, so retries may legitimately be 0
         * here; the fail-and-retry path is certified by the ramdisk STALL /
         * timeout tests. The invariant checked: no request lost or failed,
         * data intact. */
        klog("ZEROOS: storage driver %s: reset with %u requests in flight (%u queued) -> all completed, data verified (harvested/retried: retries +%llu).",
             disk->name,inflight,queued,after.retries-before.retries);
    }
    /* 7. bounds: partition end enforced */
    TD_CHECK(block_rw(part,BLOCK_OP_WRITE,part->sectors,a,ss,BLOCK_PRIO_NORMAL)==-SE_INVAL,
             "write beyond partition rejected");
    return 0;
}

int storage_selftest_driver(struct block_device *disk) {
    uint8_t *a=(uint8_t *)page_alloc_contiguous(32);
    uint8_t *b=(uint8_t *)page_alloc_contiguous(32);
    if (!a || !b) {
        klog("ZEROOS: storage driver test FAILED on %s: out of memory.",disk->name);
        return -1;
    }
    int rc=td_readonly(disk,a);
    if (rc==0)
        klog("ZEROOS: storage driver test %s read-only passed.",disk->name);
    struct block_device *scratch=rc==0 ? td_scratch_of(disk) : 0;
    if (rc==0 && scratch) {
        rc=td_destructive(disk,scratch,a,b);
        if (rc==0)
            klog("ZEROOS: storage driver test %s destructive (on %s) passed.",disk->name,
                 scratch->name);
    } else if (rc==0) {
        klog("ZEROOS: storage driver test %s destructive SKIPPED: no ZEROOS-scratch partition.",
             disk->name);
    }
    block_report(disk);
    page_free_contiguous(a,32);
    page_free_contiguous(b,32);
    return rc;
}
