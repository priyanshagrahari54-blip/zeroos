/* Block-layer self-tests on ramdisks (no hardware dependency). */
#include "block.h"
#include "ramdisk.h"
#include "storage.h"
#include "../kstring.h"
#include "../memory.h"
#include "../task.h"
#include "../timer.h"

#define TB_CHECK(cond, what) do { if (!(cond)) { \
    klog("ZEROOS: storage block test FAILED: %s (line %d).", what, __LINE__); \
    return -1; } } while (0)

static struct block_request *tb_async(struct block_device *device, uint32_t op,
                                      uint64_t lba, void *buffer, uint32_t bytes,
                                      uint8_t priority) {
    struct block_request *request=block_request_alloc(device,priority,1);
    if (!request)
        return 0;
    request->op=(uint8_t)op;
    request->lba=lba;
    if (op!=BLOCK_OP_FLUSH &&
        block_request_add_buffer(request,(uint64_t)buffer,bytes)!=0) {
        block_request_free(request);
        return 0;
    }
    block_submit(device,request);
    return request;
}

static int tb_finish(struct block_request *request) {
    int rc=block_wait(request);
    block_request_free(request);
    return rc;
}

static void tb_fill(uint8_t *buffer, uint64_t length, uint32_t seed) {
    for (uint64_t i=0; i<length; ++i)
        buffer[i]=(uint8_t)((i*31U)^(seed*131U)^(i>>9));
}

static int tb_basic(struct block_device *disk, uint8_t *a, uint8_t *b) {
    tb_fill(a,65536,1);
    TB_CHECK(block_rw(disk,BLOCK_OP_WRITE,64,a,65536,BLOCK_PRIO_FOREGROUND)==0,
             "64 KiB write");
    memset(b,0,65536);
    TB_CHECK(block_rw(disk,BLOCK_OP_READ,64,b,65536,BLOCK_PRIO_FOREGROUND)==0,
             "64 KiB read");
    TB_CHECK(memcmp(a,b,65536)==0,"read-back matches");
    TB_CHECK(block_rw(disk,BLOCK_OP_READ,disk->sectors-1,b,1024,
                      BLOCK_PRIO_FOREGROUND)==-SE_INVAL,"out-of-range rejected");
    TB_CHECK(block_rw(disk,BLOCK_OP_READ,0,b,100,BLOCK_PRIO_FOREGROUND)==-SE_INVAL,
             "partial sector rejected");
    TB_CHECK(block_flush(disk,BLOCK_PRIO_FOREGROUND)==0,"flush");
    return 0;
}

/* Sequential 4 KiB writes queued behind a stalled head are back-merged.
 * (Pool of a depth-1 device is 8 requests: 1 in flight + 7 queued.) */
static int tb_merge(struct block_device *disk, uint8_t *a, uint8_t *b) {
    struct block_request *r[8];
    struct block_stats before, after;
    block_stats_snapshot(disk,&before,0,0);
    tb_fill(a,7*4096,7);
    ramdisk_pause(disk,1);
    ramdisk_trace_reset(disk);
    r[0]=tb_async(disk,BLOCK_OP_WRITE,4096,b,4096,BLOCK_PRIO_NORMAL);
    for (uint32_t i=1; i<8; ++i)
        r[i]=tb_async(disk,BLOCK_OP_WRITE,2048+(i-1)*8,a+(i-1)*4096,4096,
                      BLOCK_PRIO_NORMAL);
    ramdisk_pause(disk,0);
    for (uint32_t i=0; i<8; ++i)
        TB_CHECK(r[i] && tb_finish(r[i])==0,"merged write completes");
    block_stats_snapshot(disk,&after,0,0);
    TB_CHECK(after.merges-before.merges==6,"6 back-merges");
    struct ramdisk_info info;
    ramdisk_info(disk,&info);
    TB_CHECK(info.trace_count==2 && info.trace[1]==2048,"one merged dispatch");
    memset(b,0,7*4096);
    TB_CHECK(block_rw(disk,BLOCK_OP_READ,2048,b,7*4096,BLOCK_PRIO_NORMAL)==0,
             "merged read-back");
    TB_CHECK(memcmp(a,b,7*4096)==0,"merged data intact");
    return 0;
}

/* HDD media: C-SCAN from the head position, wrapping to the lowest LBA. */
static int tb_cscan(struct block_device *hdd, uint8_t *buffer) {
    static const uint64_t lbas[5]={800,100,500,300,900};
    static const uint64_t expected[6]={600,800,900,100,300,500};
    struct block_request *r[6];
    ramdisk_pause(hdd,1);
    ramdisk_trace_reset(hdd);
    r[0]=tb_async(hdd,BLOCK_OP_READ,600,buffer,512,BLOCK_PRIO_NORMAL);
    for (uint32_t i=0; i<5; ++i)
        r[i+1]=tb_async(hdd,BLOCK_OP_READ,lbas[i],buffer+512*(i+1),512,
                        BLOCK_PRIO_NORMAL);
    ramdisk_pause(hdd,0);
    for (uint32_t i=0; i<6; ++i)
        TB_CHECK(r[i] && tb_finish(r[i])==0,"elevator read");
    struct ramdisk_info info;
    ramdisk_info(hdd,&info);
    TB_CHECK(info.trace_count==6,"elevator dispatch count");
    for (uint32_t i=0; i<6; ++i)
        TB_CHECK(info.trace[i]==expected[i],"C-SCAN order");
    return 0;
}

/* Foreground overtakes queued background; background aging promotes. */
static int tb_priority(struct block_device *disk, uint8_t *buffer) {
    struct block_request *r[5];
    ramdisk_pause(disk,1);
    ramdisk_trace_reset(disk);
    r[0]=tb_async(disk,BLOCK_OP_READ,10,buffer,512,BLOCK_PRIO_NORMAL);
    r[1]=tb_async(disk,BLOCK_OP_READ,1000,buffer+512,512,BLOCK_PRIO_BACKGROUND);
    r[2]=tb_async(disk,BLOCK_OP_READ,2000,buffer+1024,512,BLOCK_PRIO_BACKGROUND);
    r[3]=tb_async(disk,BLOCK_OP_READ,3000,buffer+1536,512,BLOCK_PRIO_FOREGROUND);
    r[4]=tb_async(disk,BLOCK_OP_READ,4000,buffer+2048,512,BLOCK_PRIO_NORMAL);
    ramdisk_pause(disk,0);
    for (uint32_t i=0; i<5; ++i)
        TB_CHECK(r[i] && tb_finish(r[i])==0,"priority read");
    struct ramdisk_info info;
    ramdisk_info(disk,&info);
    TB_CHECK(info.trace_count==5 && info.trace[1]==3000 && info.trace[2]==4000 &&
             info.trace[3]==1000 && info.trace[4]==2000,"FG > NORMAL > BG order");

    /* Aging: with a 2-tick threshold an old background request is promoted
     * ahead of newer foreground work. */
    struct block_stats before, after;
    block_stats_snapshot(disk,&before,0,0);
    uint32_t saved_aging=disk->aging_ticks;
    disk->aging_ticks=2;
    ramdisk_pause(disk,1);
    ramdisk_trace_reset(disk);
    r[0]=tb_async(disk,BLOCK_OP_READ,10,buffer,512,BLOCK_PRIO_NORMAL);
    r[1]=tb_async(disk,BLOCK_OP_READ,5000,buffer+512,512,BLOCK_PRIO_BACKGROUND);
    task_sleep_ticks(6);
    r[2]=tb_async(disk,BLOCK_OP_READ,6000,buffer+1024,512,BLOCK_PRIO_FOREGROUND);
    ramdisk_pause(disk,0);
    for (uint32_t i=0; i<3; ++i)
        TB_CHECK(r[i] && tb_finish(r[i])==0,"aging read");
    disk->aging_ticks=saved_aging;
    ramdisk_info(disk,&info);
    block_stats_snapshot(disk,&after,0,0);
    TB_CHECK(info.trace_count==3 && info.trace[1]==5000,"aged BG promoted");
    TB_CHECK(after.aging_promotions>before.aging_promotions,"aging accounted");
    return 0;
}

/* A flush is a barrier: W1 completes before FLUSH, W2 waits for FLUSH. */
static int tb_barrier(struct block_device *disk, uint8_t *buffer) {
    struct block_request *r[3];
    uint32_t saved=disk->queue_depth;
    disk->queue_depth=4;
    ramdisk_pause(disk,1);
    ramdisk_trace_reset(disk);
    r[0]=tb_async(disk,BLOCK_OP_WRITE,100,buffer,512,BLOCK_PRIO_NORMAL);
    r[1]=tb_async(disk,BLOCK_OP_FLUSH,0,0,0,BLOCK_PRIO_NORMAL);
    r[2]=tb_async(disk,BLOCK_OP_WRITE,200,buffer,512,BLOCK_PRIO_FOREGROUND);
    struct ramdisk_info info;
    ramdisk_info(disk,&info);
    TB_CHECK(info.trace_count==1,"barrier holds later write");
    ramdisk_pause(disk,0);
    for (uint32_t i=0; i<3; ++i)
        TB_CHECK(r[i] && tb_finish(r[i])==0,"barrier requests complete");
    disk->queue_depth=saved;
    ramdisk_info(disk,&info);
    TB_CHECK(info.trace_count==3 && info.trace[0]==100 && info.trace[1]==~0ULL &&
             info.trace[2]==200,"W1, FLUSH, W2 order");
    return 0;
}

static int tb_cancel(struct block_device *disk, uint8_t *buffer) {
    ramdisk_pause(disk,1);
    struct block_request *a=tb_async(disk,BLOCK_OP_READ,1,buffer,512,BLOCK_PRIO_NORMAL);
    struct block_request *b=tb_async(disk,BLOCK_OP_READ,3,buffer+512,512,BLOCK_PRIO_NORMAL);
    TB_CHECK(a && b,"cancel setup");
    TB_CHECK(block_cancel(a)==-SE_BUSY,"dispatched request not cancellable");
    TB_CHECK(block_cancel(b)==0,"queued request cancelled");
    TB_CHECK(tb_finish(b)==-SE_CANCELED,"cancelled status");
    ramdisk_pause(disk,0);
    TB_CHECK(tb_finish(a)==0,"survivor completes");
    return 0;
}

static int tb_pool(struct block_device *disk) {
    struct block_request *held[BLOCK_POOL_MAX];
    uint32_t count=0, background=0;
    while (count<BLOCK_POOL_MAX) {
        struct block_request *r=block_request_alloc(disk,BLOCK_PRIO_BACKGROUND,0);
        if (!r)
            break;
        held[count++]=r;
    }
    background=count;
    TB_CHECK(background==disk->pool_size-disk->pool_size/4U,
             "background admission reserve");
    while (count<BLOCK_POOL_MAX) {
        struct block_request *r=block_request_alloc(disk,BLOCK_PRIO_FOREGROUND,0);
        if (!r)
            break;
        held[count++]=r;
    }
    TB_CHECK(count==disk->pool_size,"foreground may use the reserve");
    TB_CHECK(!block_request_alloc(disk,BLOCK_PRIO_FOREGROUND,0),"pool bounded");
    for (uint32_t i=0; i<count; ++i)
        block_request_free(held[i]);
    return 0;
}

static int tb_faults(struct block_device *disk, uint8_t *a, uint8_t *b) {
    struct block_fault fault;
    struct block_stats before, after;

    /* Transient media error: succeeds on retry. */
    memset(&fault,0,sizeof(fault));
    fault.mode=BLOCK_FAULT_EIO;
    fault.op_mask=1U<<BLOCK_OP_READ;
    fault.lba_start=64;
    fault.lba_end=72;
    fault.remaining=1;
    block_stats_snapshot(disk,&before,0,0);
    block_fault_set(disk,&fault);
    TB_CHECK(block_rw(disk,BLOCK_OP_READ,64,b,4096,BLOCK_PRIO_NORMAL)==0,
             "transient EIO retried");
    block_stats_snapshot(disk,&after,0,0);
    TB_CHECK(after.retries==before.retries+1,"one retry");

    /* Persistent error on one sector fails only that range; a neighbouring
     * merged request still succeeds (unmerge on error). */
    fault.remaining=0;
    fault.lba_start=5000;
    fault.lba_end=5001;
    block_fault_set(disk,&fault);
    TB_CHECK(block_rw(disk,BLOCK_OP_READ,5000,b,512,BLOCK_PRIO_NORMAL)==-SE_IO,
             "persistent EIO reported");
    ramdisk_pause(disk,1);
    struct block_request *h=tb_async(disk,BLOCK_OP_READ,0,b,512,BLOCK_PRIO_NORMAL);
    struct block_request *x=tb_async(disk,BLOCK_OP_READ,4992,b+512,4096,BLOCK_PRIO_NORMAL);
    struct block_request *y=tb_async(disk,BLOCK_OP_READ,5000,b+8192,512,BLOCK_PRIO_NORMAL);
    ramdisk_pause(disk,0);
    TB_CHECK(h && x && y,"unmerge setup");
    TB_CHECK(tb_finish(h)==0,"unmerge head");
    TB_CHECK(tb_finish(x)==0,"healthy neighbour survives unmerge");
    TB_CHECK(tb_finish(y)==-SE_IO,"bad sector isolated");
    block_fault_clear(disk);

    /* Write error: data not written, error surfaced. */
    tb_fill(a,4096,3);
    memset(&fault,0,sizeof(fault));
    fault.mode=BLOCK_FAULT_EIO;
    fault.op_mask=1U<<BLOCK_OP_WRITE;
    block_fault_set(disk,&fault);
    TB_CHECK(block_rw(disk,BLOCK_OP_WRITE,7000,a,4096,BLOCK_PRIO_NORMAL)==-SE_IO,
             "write EIO");
    fault.op_mask=1U<<BLOCK_OP_FLUSH;
    block_fault_set(disk,&fault);
    TB_CHECK(block_flush(disk,BLOCK_PRIO_NORMAL)==-SE_IO,"flush EIO not retried away");
    block_fault_clear(disk);

    /* Stalled command: watchdog timeout, retry succeeds. */
    uint32_t saved_timeout=disk->timeout_ticks;
    disk->timeout_ticks=5;
    memset(&fault,0,sizeof(fault));
    fault.mode=BLOCK_FAULT_STALL;
    fault.op_mask=1U<<BLOCK_OP_READ;
    fault.remaining=1;
    block_stats_snapshot(disk,&before,0,0);
    block_fault_set(disk,&fault);
    TB_CHECK(block_rw(disk,BLOCK_OP_READ,64,b,4096,BLOCK_PRIO_NORMAL)==0,
             "stalled read recovered by timeout+retry");
    block_stats_snapshot(disk,&after,0,0);
    TB_CHECK(after.timeouts>before.timeouts,"timeout accounted");
    /* Persistent stall: bounded retries then -ETIMEDOUT. */
    fault.remaining=0;
    block_fault_set(disk,&fault);
    TB_CHECK(block_rw(disk,BLOCK_OP_READ,64,b,512,BLOCK_PRIO_NORMAL)==-SE_TIMEDOUT,
             "persistent stall times out");
    block_fault_clear(disk);
    disk->timeout_ticks=saved_timeout;
    return 0;
}

static int tb_power(struct block_device *disk, uint8_t *a, uint8_t *b) {
    struct block_fault fault;
    tb_fill(a,4096,11);
    tb_fill(b,4096,12);
    ramdisk_set_cache_tracking(disk,1);
    TB_CHECK(block_rw(disk,BLOCK_OP_WRITE,9000,a,4096,BLOCK_PRIO_NORMAL)==0,"A");
    TB_CHECK(block_flush(disk,BLOCK_PRIO_NORMAL)==0,"flush A");
    TB_CHECK(block_rw(disk,BLOCK_OP_WRITE,9000,b,4096,BLOCK_PRIO_NORMAL)==0,"B");
    memset(&fault,0,sizeof(fault));
    fault.mode=BLOCK_FAULT_POWER_CUT;
    fault.op_mask=1U<<BLOCK_OP_READ;
    fault.power_policy=BLOCK_POWER_DROP_ALL;
    block_fault_set(disk,&fault);
    TB_CHECK(block_rw(disk,BLOCK_OP_READ,0,b,512,BLOCK_PRIO_NORMAL)==-SE_NODEV,
             "power cut fails I/O");
    TB_CHECK(block_rw(disk,BLOCK_OP_READ,0,b,512,BLOCK_PRIO_NORMAL)==-SE_NODEV,
             "powered-off device rejects I/O");
    ramdisk_power_on(disk);
    TB_CHECK(block_rw(disk,BLOCK_OP_READ,9000,b,4096,BLOCK_PRIO_NORMAL)==0,"reread");
    TB_CHECK(memcmp(a,b,4096)==0,"unflushed write dropped, flushed data kept");

    tb_fill(b,4096,13);
    TB_CHECK(block_rw(disk,BLOCK_OP_WRITE,9000,b,4096,BLOCK_PRIO_NORMAL)==0,"C");
    fault.power_policy=BLOCK_POWER_KEEP_ALL;
    block_fault_set(disk,&fault);
    (void)block_rw(disk,BLOCK_OP_READ,0,a,512,BLOCK_PRIO_NORMAL);
    ramdisk_power_on(disk);
    TB_CHECK(block_rw(disk,BLOCK_OP_READ,9000,a,4096,BLOCK_PRIO_NORMAL)==0,"reread 2");
    TB_CHECK(memcmp(a,b,4096)==0,"keep-all policy persists cached write");
    ramdisk_set_cache_tracking(disk,0);

    /* Surprise removal. */
    memset(&fault,0,sizeof(fault));
    fault.mode=BLOCK_FAULT_DISAPPEAR;
    fault.op_mask=1U<<BLOCK_OP_WRITE;
    block_fault_set(disk,&fault);
    TB_CHECK(block_rw(disk,BLOCK_OP_WRITE,0,a,512,BLOCK_PRIO_NORMAL)==-SE_NODEV,
             "device disappearance -> ENODEV");
    TB_CHECK(block_rw(disk,BLOCK_OP_READ,0,a,512,BLOCK_PRIO_NORMAL)==-SE_NODEV,
             "gone device rejects I/O");
    ramdisk_power_on(disk);
    return 0;
}

struct tb_worker {
    struct block_device *disk;
    uint32_t index;
    int result;
    struct kcompletion done;
};

static void tb_worker_main(void *argument) {
    struct tb_worker *w=(struct tb_worker *)argument;
    uint8_t *a=(uint8_t *)page_alloc_contiguous(2);
    uint8_t *b=(uint8_t *)page_alloc_contiguous(2);
    w->result=(a && b) ? 0 : -1;
    for (uint32_t round=0; round<16 && w->result==0; ++round) {
        uint64_t lba=12000+w->index*256+(round%4)*16;
        tb_fill(a,8192,w->index*100+round);
        uint8_t prio=(uint8_t)(w->index%BLOCK_PRIO_COUNT);
        if (block_rw(w->disk,BLOCK_OP_WRITE,lba,a,8192,prio)!=0 ||
            block_rw(w->disk,BLOCK_OP_READ,lba,b,8192,prio)!=0 ||
            memcmp(a,b,8192)!=0)
            w->result=-1;
    }
    if (a) page_free_contiguous(a,2);
    if (b) page_free_contiguous(b,2);
    kcompletion_signal(&w->done);
    task_exit();
}

static int tb_concurrency(struct block_device *disk) {
    static struct tb_worker workers[4];
    uint32_t saved=disk->queue_depth;
    disk->queue_depth=8;
    for (uint32_t i=0; i<4; ++i) {
        uint64_t id;
        workers[i].disk=disk;
        workers[i].index=i;
        workers[i].result=-1;
        kcompletion_init(&workers[i].done);
        TB_CHECK(task_create(tb_worker_main,&workers[i],&id)==0,"worker create");
    }
    int ok=1;
    for (uint32_t i=0; i<4; ++i) {
        kcompletion_wait(&workers[i].done);
        if (workers[i].result!=0)
            ok=0;
    }
    disk->queue_depth=saved;
    TB_CHECK(ok,"4 concurrent writers/readers verified");
    TB_CHECK(block_idle(disk),"queue drained");
    return 0;
}

int storage_selftest_block(void) {
    struct block_device *disk=ramdisk_create("ramtest0",8U*1024U*1024U,
                                             BLOCK_MEDIA_SSD,1);
    struct block_device *hdd=ramdisk_create("ramhdd0",1024U*1024U,
                                            BLOCK_MEDIA_HDD,1);
    uint8_t *a=(uint8_t *)page_alloc_contiguous(16);
    uint8_t *b=(uint8_t *)page_alloc_contiguous(16);
    TB_CHECK(disk && hdd && a && b,"ramdisk setup");
    int rc=0;
#define TB_RUN(call, label) do { if (!rc) { rc=(call); \
        if (!rc) klog("ZEROOS: storage block test %s passed.", label); } } while (0)
    TB_RUN(tb_basic(disk,a,b),"basic-rw");
    TB_RUN(tb_merge(disk,a,b),"merge");
    TB_RUN(tb_cscan(hdd,b),"hdd-cscan");
    TB_RUN(tb_priority(disk,b),"priority-aging");
    TB_RUN(tb_barrier(disk,b),"flush-barrier");
    TB_RUN(tb_cancel(disk,b),"cancel");
    TB_RUN(tb_pool(disk),"pool-backpressure");
    TB_RUN(tb_faults(disk,a,b),"eio-timeout-retry");
    TB_RUN(tb_power(disk,a,b),"power-cut-removal");
    TB_RUN(tb_concurrency(disk),"concurrency");
#undef TB_RUN
    if (!rc) {
        block_report(disk);
        block_report(hdd);
    }
    page_free_contiguous(a,16);
    page_free_contiguous(b,16);
    return rc;
}
