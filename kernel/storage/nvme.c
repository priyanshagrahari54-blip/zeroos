/*
 * NVMe controller driver.
 *
 * Controller: disable (CC.EN=0, wait RDY=0 within CAP.TO) -> admin SQ/CQ
 * (64 entries) -> enable -> Identify Controller / Namespace -> Set Features
 * (Number of Queues) -> N I/O queue pairs (N <= online CPUs, <= 4), each
 * with its own MSI-X vector (single MSI vector or watchdog polling are the
 * degraded fallbacks). One namespace (the first active) per controller.
 *
 * Data: PRP1/PRP2 with a per-command preallocated PRP list page, so any
 * request the block layer builds (<= 128 KiB, page-aligned interior
 * segment boundaries, BLOCK_DEV_PRP_SEGMENTS) maps without allocation.
 *
 * Completion: phase-tag CQ walk, SQ head from SQHD, CQ head doorbell.
 * Status: success / LBA-range -> -SE_INVAL / media & others -> -SE_IO.
 *
 * Recovery (watchdog timeout): controller reset and full queue rebuild; all
 * outstanding commands complete with -SE_TIMEDOUT (retried by the block
 * layer). CSTS reading all-ones means the function is gone -> -SE_NODEV.
 * Shutdown: CC.SHN=normal, wait SHST=complete (bounded by CAP.TO).
 */
#include "nvme.h"
#include "../interrupts.h"
#include "../kstring.h"
#include "../memory.h"
#include "../pci.h"
#include "../smp.h"
#include "../task.h"
#include "../timer.h"

#define NVME_MAX_CONTROLLERS 2U
#define NVME_MAX_IO_QUEUES 4U
#define NVME_ADMIN_DEPTH 64U
#define NVME_IO_DEPTH 64U
#define NVME_MAX_TRANSFER (128U * 1024U)

#define NVME_REG_CAP 0x00
#define NVME_REG_VS 0x08
#define NVME_REG_CC 0x14
#define NVME_REG_CSTS 0x1c
#define NVME_REG_AQA 0x24
#define NVME_REG_ASQ 0x28
#define NVME_REG_ACQ 0x30

#define NVME_ADMIN_DELETE_SQ 0x00
#define NVME_ADMIN_CREATE_SQ 0x01
#define NVME_ADMIN_DELETE_CQ 0x04
#define NVME_ADMIN_CREATE_CQ 0x05
#define NVME_ADMIN_IDENTIFY 0x06
#define NVME_ADMIN_SET_FEATURES 0x09
#define NVME_CMD_FLUSH 0x00
#define NVME_CMD_WRITE 0x01
#define NVME_CMD_READ 0x02

struct nvme_sqe {
    uint32_t cdw0;
    uint32_t nsid;
    uint64_t reserved;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
};

struct nvme_cqe {
    uint32_t result;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;        /* bit0 phase */
};

struct nvme_controller;

/* Bounded quiesce window before a controller disable (10 ms ticks). */
#define NVME_RESET_DRAIN_TICKS 20U

struct nvme_queue {
    struct nvme_controller *controller;
    struct spinlock lock;
    uint32_t id;
    uint32_t depth;
    struct nvme_sqe *sq;
    struct nvme_cqe *cq;
    uint64_t *prp_lists;            /* depth pages, one per CID */
    uint32_t sq_tail;
    uint32_t sq_head;
    uint32_t cq_head;
    uint32_t phase;
    uint32_t doorbell_pending;
    uint64_t free_cids;
    struct block_request *requests[NVME_IO_DEPTH];
    int vector;
    uint32_t msix_entry;
    uint64_t completions;
};

struct nvme_controller {
    struct pci_device *pci;
    volatile uint8_t *bar;
    uint64_t cap;
    uint32_t stride;
    uint32_t index;
    uint32_t nsid;
    uint32_t io_queue_count;
    uint64_t msix_table;
    int msi_vector;                 /* single-MSI fallback */
    int gone;
    struct kmutex reset_lock;
    struct nvme_queue admin;
    struct nvme_queue io[NVME_MAX_IO_QUEUES];
    struct block_device *device;
    uint8_t *identify;              /* 4 KiB scratch for admin data */
    uint32_t irq_masked;
    uint64_t resets;
};

static struct nvme_controller nvme_controllers[NVME_MAX_CONTROLLERS];
static uint32_t nvme_count;
static const struct block_device_ops nvme_ops;

static inline uint32_t nvme_read32(struct nvme_controller *c, uint32_t reg) {
    return *(volatile uint32_t *)(c->bar+reg);
}
static inline uint64_t nvme_read64(struct nvme_controller *c, uint32_t reg) {
    return (uint64_t)nvme_read32(c,reg)|((uint64_t)nvme_read32(c,reg+4U)<<32);
}
static inline void nvme_write32(struct nvme_controller *c, uint32_t reg, uint32_t v) {
    *(volatile uint32_t *)(c->bar+reg)=v;
}
static inline void nvme_write64(struct nvme_controller *c, uint32_t reg, uint64_t v) {
    nvme_write32(c,reg,(uint32_t)v);
    nvme_write32(c,reg+4U,(uint32_t)(v>>32));
}
static inline void nvme_sq_doorbell(struct nvme_controller *c, uint32_t qid, uint32_t v) {
    nvme_write32(c,0x1000U+(2U*qid)*c->stride,v);
}
static inline void nvme_cq_doorbell(struct nvme_controller *c, uint32_t qid, uint32_t v) {
    nvme_write32(c,0x1000U+(2U*qid+1U)*c->stride,v);
}

static uint64_t nvme_timeout_ticks(struct nvme_controller *c) {
    uint64_t units=(c->cap>>24)&0xffU;          /* 500 ms units */
    if (units==0)
        units=1;
    return units*timer_frequency_hz()/2ULL+timer_frequency_hz();
}

static int nvme_wait_ready(struct nvme_controller *c, uint32_t ready) {
    uint64_t deadline=timer_ticks()+nvme_timeout_ticks(c);
    for (;;) {
        uint32_t csts=nvme_read32(c,NVME_REG_CSTS);
        if (csts==0xffffffffU)
            return -SE_NODEV;
        if ((csts&1U)==ready)
            return 0;
        if (ready && (csts&2U))
            return -SE_IO;                  /* controller fatal status */
        if (timer_ticks()>deadline)
            return -SE_TIMEDOUT;
        task_yield();
    }
}

static int nvme_queue_alloc(struct nvme_queue *q, struct nvme_controller *c,
                            uint32_t id, uint32_t depth, int prp) {
    memset(q,0,sizeof(*q));
    q->controller=c;
    q->id=id;
    q->depth=depth;
    q->vector=-1;
    spinlock_init(&q->lock);
    q->sq=(struct nvme_sqe *)page_alloc_zero();
    q->cq=(struct nvme_cqe *)page_alloc_zero();
    if (!q->sq || !q->cq)
        return -SE_NOMEM;
    if (prp) {
        q->prp_lists=(uint64_t *)page_alloc_contiguous(depth);
        if (!q->prp_lists)
            return -SE_NOMEM;
    }
    return 0;
}

static void nvme_queue_reset_state(struct nvme_queue *q) {
    memset(q->sq,0,ZEROOS_PAGE_SIZE);
    memset(q->cq,0,ZEROOS_PAGE_SIZE);
    q->sq_tail=q->sq_head=q->cq_head=0;
    q->phase=1;
    q->doorbell_pending=0;
    /* CID space is depth-1 so the SQ can never overrun its head. */
    q->free_cids=(q->depth-1U>=64U) ? ~0ULL : ((1ULL<<(q->depth-1U))-1ULL);
}

/* Synchronous admin command (task context, bounded). */
static int nvme_admin(struct nvme_controller *c, struct nvme_sqe *command,
                      uint32_t *result) {
    struct nvme_queue *q=&c->admin;
    uint64_t flags=spin_lock_irqsave(&q->lock);
    uint16_t cid=(uint16_t)(q->sq_tail);
    command->cdw0=(command->cdw0&0xffffU)|((uint32_t)cid<<16);
    q->sq[q->sq_tail]=*command;
    q->sq_tail=(q->sq_tail+1U)%q->depth;
    __asm__ volatile ("" ::: "memory");
    nvme_sq_doorbell(c,0,q->sq_tail);
    spin_unlock_irqrestore(&q->lock,flags);

    uint64_t deadline=timer_ticks()+timer_frequency_hz()*2ULL;
    for (;;) {
        volatile struct nvme_cqe *cqe=&q->cq[q->cq_head];
        if ((cqe->status&1U)==q->phase) {
            uint16_t status=cqe->status>>1;
            uint16_t got=cqe->cid;
            if (result)
                *result=cqe->result;
            q->sq_head=cqe->sq_head;
            q->cq_head=(q->cq_head+1U)%q->depth;
            if (q->cq_head==0)
                q->phase^=1U;
            nvme_cq_doorbell(c,0,q->cq_head);
            if (got!=cid)
                continue;
            return status ? -SE_IO : 0;
        }
        if (nvme_read32(c,NVME_REG_CSTS)==0xffffffffU)
            return -SE_NODEV;
        if (timer_ticks()>deadline)
            return -SE_TIMEDOUT;
        task_yield();
    }
}

static int nvme_status_to_error(uint16_t status) {
    uint32_t sc=status&0xffU;
    uint32_t sct=(status>>8)&7U;
    if (sc==0 && sct==0)
        return 0;
    if (sct==0 && sc==0x80U)
        return -SE_INVAL;                   /* LBA out of range */
    if (sct==0 && sc==0x81U)
        return -SE_NOSPC;                   /* capacity exceeded */
    return -SE_IO;
}

/* Walk a CQ; caller holds q->lock. Returns completed requests. */
static uint32_t nvme_reap_locked(struct nvme_queue *q, struct block_request **done,
                                 int *status) {
    struct nvme_controller *c=q->controller;
    uint32_t count=0;
    for (;;) {
        volatile struct nvme_cqe *cqe=&q->cq[q->cq_head];
        uint16_t raw=cqe->status;
        if ((raw&1U)!=q->phase)
            break;
        uint16_t cid=cqe->cid;
        q->sq_head=cqe->sq_head;
        q->cq_head=(q->cq_head+1U)%q->depth;
        if (q->cq_head==0)
            q->phase^=1U;
        if (cid<NVME_IO_DEPTH && q->requests[cid]) {
            done[count]=q->requests[cid];
            status[count]=nvme_status_to_error((uint16_t)(raw>>1));
            ++count;
            q->requests[cid]=0;
            q->free_cids|=1ULL<<cid;
        }
        ++q->completions;
    }
    if (count)
        nvme_cq_doorbell(c,q->id,q->cq_head);
    return count;
}

static void nvme_service_queue(struct nvme_queue *q) {
    struct block_request *done[NVME_IO_DEPTH];
    int status[NVME_IO_DEPTH];
    uint64_t flags=spin_lock_irqsave(&q->lock);
    uint32_t count=nvme_reap_locked(q,done,status);
    spin_unlock_irqrestore(&q->lock,flags);
    for (uint32_t i=0; i<count; ++i)
        block_complete(done[i],status[i]);
}

static void nvme_irq(uint8_t vector, struct interrupt_frame *frame, void *context) {
    struct nvme_queue *q=(struct nvme_queue *)context;
    (void)vector;
    (void)frame;
    if (q->controller->device)
        block_note_interrupt(q->controller->device);
    nvme_service_queue(q);
}

static void nvme_irq_all(uint8_t vector, struct interrupt_frame *frame, void *context) {
    struct nvme_controller *c=(struct nvme_controller *)context;
    (void)vector;
    (void)frame;
    if (c->device)
        block_note_interrupt(c->device);
    for (uint32_t i=0; i<c->io_queue_count; ++i)
        nvme_service_queue(&c->io[i]);
}

/* ---------------------------------------------------------------- I/O */

static int nvme_build_prps(struct nvme_queue *q, uint32_t cid,
                           struct block_request *request, struct nvme_sqe *sqe) {
    uint64_t pages[NVME_MAX_TRANSFER/ZEROOS_PAGE_SIZE+2U];
    uint32_t count=0;
    for (uint32_t s=0; s<request->segment_count; ++s) {
        uint64_t address=request->segments[s].physical;
        uint64_t end=address+request->segments[s].length;
        if (s>0 && (address&(ZEROOS_PAGE_SIZE-1ULL)))
            return -SE_INVAL;
        if (s+1U<request->segment_count && (end&(ZEROOS_PAGE_SIZE-1ULL)))
            return -SE_INVAL;
        while (address<end) {
            if (count>=sizeof(pages)/sizeof(pages[0]))
                return -SE_INVAL;
            pages[count++]=address;
            address=(address&~(ZEROOS_PAGE_SIZE-1ULL))+ZEROOS_PAGE_SIZE;
        }
    }
    if (count==0)
        return -SE_INVAL;
    sqe->prp1=pages[0];
    if (count==1)
        sqe->prp2=0;
    else if (count==2)
        sqe->prp2=pages[1];
    else {
        uint64_t *list=(uint64_t *)((uint64_t)q->prp_lists+cid*ZEROOS_PAGE_SIZE);
        for (uint32_t i=1; i<count; ++i)
            list[i-1U]=pages[i];
        sqe->prp2=(uint64_t)list;
    }
    return 0;
}

static int nvme_submit(struct block_device *device, struct block_request *request) {
    struct nvme_controller *c=(struct nvme_controller *)device->driver_data;
    if (c->gone)
        return -SE_NODEV;
    struct nvme_queue *q=&c->io[request->hw_queue%c->io_queue_count];
    uint64_t flags=spin_lock_irqsave(&q->lock);
    if (!q->free_cids) {
        spin_unlock_irqrestore(&q->lock,flags);
        return -SE_AGAIN;
    }
    uint32_t cid=(uint32_t)__builtin_ctzll(q->free_cids);
    struct nvme_sqe sqe;
    memset(&sqe,0,sizeof(sqe));
    sqe.nsid=c->nsid;
    if (request->op==BLOCK_OP_FLUSH) {
        sqe.cdw0=NVME_CMD_FLUSH;
    } else {
        sqe.cdw0=request->op==BLOCK_OP_WRITE ? NVME_CMD_WRITE : NVME_CMD_READ;
        sqe.cdw10=(uint32_t)request->lba;
        sqe.cdw11=(uint32_t)(request->lba>>32);
        sqe.cdw12=(request->sectors-1U)&0xffffU;
        int rc=nvme_build_prps(q,cid,request,&sqe);
        if (rc) {
            spin_unlock_irqrestore(&q->lock,flags);
            return rc;
        }
    }
    sqe.cdw0|=cid<<16;
    q->free_cids&=~(1ULL<<cid);
    q->requests[cid]=request;
    request->tag=(q->id<<16)|cid;
    q->sq[q->sq_tail]=sqe;
    q->sq_tail=(q->sq_tail+1U)%q->depth;
    q->doorbell_pending=1;
    spin_unlock_irqrestore(&q->lock,flags);
    return 0;
}

static void nvme_kick(struct block_device *device, uint32_t hw_queue) {
    struct nvme_controller *c=(struct nvme_controller *)device->driver_data;
    struct nvme_queue *q=&c->io[hw_queue%c->io_queue_count];
    uint64_t flags=spin_lock_irqsave(&q->lock);
    if (q->doorbell_pending) {
        q->doorbell_pending=0;
        __asm__ volatile ("" ::: "memory");
        nvme_sq_doorbell(c,q->id,q->sq_tail);
    }
    spin_unlock_irqrestore(&q->lock,flags);
}

static void nvme_poll(struct block_device *device) {
    struct nvme_controller *c=(struct nvme_controller *)device->driver_data;
    if (nvme_read32(c,NVME_REG_CSTS)==0xffffffffU)
        return;                             /* reset path reports removal */
    for (uint32_t i=0; i<c->io_queue_count; ++i)
        nvme_service_queue(&c->io[i]);
}

static int nvme_create_io_queues(struct nvme_controller *c) {
    for (uint32_t i=0; i<c->io_queue_count; ++i) {
        struct nvme_queue *q=&c->io[i];
        struct nvme_sqe command;
        int use_irq=(q->vector>=0 || c->msi_vector>=0);
        nvme_queue_reset_state(q);
        memset(&command,0,sizeof(command));
        command.cdw0=NVME_ADMIN_CREATE_CQ;
        command.prp1=(uint64_t)q->cq;
        command.cdw10=((q->depth-1U)<<16)|q->id;
        command.cdw11=1U|(use_irq ? 2U : 0U)|(q->msix_entry<<16);
        int rc=nvme_admin(c,&command,0);
        if (rc)
            return rc;
        memset(&command,0,sizeof(command));
        command.cdw0=NVME_ADMIN_CREATE_SQ;
        command.prp1=(uint64_t)q->sq;
        command.cdw10=((q->depth-1U)<<16)|q->id;
        command.cdw11=1U|(q->id<<16);
        rc=nvme_admin(c,&command,0);
        if (rc)
            return rc;
    }
    return 0;
}

static int nvme_enable(struct nvme_controller *c) {
    uint32_t cc=nvme_read32(c,NVME_REG_CC);
    if (cc==0xffffffffU)
        return -SE_NODEV;
    if (cc&1U) {
        nvme_write32(c,NVME_REG_CC,cc&~1U);
    }
    int rc=nvme_wait_ready(c,0);
    if (rc)
        return rc;
    nvme_queue_reset_state(&c->admin);
    nvme_write32(c,NVME_REG_AQA,((NVME_ADMIN_DEPTH-1U)<<16)|(NVME_ADMIN_DEPTH-1U));
    nvme_write64(c,NVME_REG_ASQ,(uint64_t)c->admin.sq);
    nvme_write64(c,NVME_REG_ACQ,(uint64_t)c->admin.cq);
    /* IOCQES=4 (16 B), IOSQES=6 (64 B), MPS=0 (4 KiB), NVM command set. */
    nvme_write32(c,NVME_REG_CC,(4U<<20)|(6U<<16)|1U);
    return nvme_wait_ready(c,1);
}

/* Fail every outstanding command on every I/O queue. */
static void nvme_fail_outstanding(struct nvme_controller *c, int status) {
    for (uint32_t i=0; i<c->io_queue_count; ++i) {
        struct nvme_queue *q=&c->io[i];
        struct block_request *list[NVME_IO_DEPTH];
        uint32_t count=0;
        uint64_t flags=spin_lock_irqsave(&q->lock);
        for (uint32_t cid=0; cid<NVME_IO_DEPTH; ++cid) {
            if (q->requests[cid]) {
                list[count++]=q->requests[cid];
                q->requests[cid]=0;
            }
        }
        q->free_cids=0;                     /* nothing new until rebuilt */
        spin_unlock_irqrestore(&q->lock,flags);
        for (uint32_t j=0; j<count; ++j)
            block_complete(list[j],status);
    }
}

static int nvme_reset(struct block_device *device) {
    struct nvme_controller *c=(struct nvme_controller *)device->driver_data;
    kmutex_lock(&c->reset_lock);
    ++c->resets;
    if (nvme_read32(c,NVME_REG_CSTS)==0xffffffffU) {
        c->gone=1;
        klog("ZEROOS: nvme%u: controller not responding (removed).",c->index);
        nvme_fail_outstanding(c,-SE_NODEV);
        kmutex_unlock(&c->reset_lock);
        return -SE_NODEV;
    }
    /* Quiesce: give commands the controller is still executing a bounded
     * window (NVME_RESET_DRAIN_TICKS) to finish and harvest them, so only
     * genuinely stuck commands are abandoned by the disable. Disabling with
     * device-side work in progress is legal per the spec, but abandoning
     * transfers that would have completed only converts successes into
     * retries (and has crashed QEMU's emulated controller). */
    for (uint32_t tick=0; tick<=NVME_RESET_DRAIN_TICKS; ++tick) {
        uint32_t outstanding=0;
        for (uint32_t i=0; i<c->io_queue_count; ++i) {
            struct nvme_queue *q=&c->io[i];
            nvme_service_queue(q);
            uint64_t flags=spin_lock_irqsave(&q->lock);
            for (uint32_t cid=0; cid<NVME_IO_DEPTH; ++cid)
                if (q->requests[cid])
                    ++outstanding;
            spin_unlock_irqrestore(&q->lock,flags);
        }
        if (!outstanding || tick==NVME_RESET_DRAIN_TICKS)
            break;
        task_sleep_ticks(1);
    }
    nvme_write32(c,NVME_REG_CC,nvme_read32(c,NVME_REG_CC)&~1U);
    int rc=nvme_wait_ready(c,0);
    nvme_fail_outstanding(c,-SE_TIMEDOUT);
    if (rc==0)
        rc=nvme_enable(c);
    if (rc==0)
        rc=nvme_create_io_queues(c);
    if (rc==-SE_NODEV)
        c->gone=1;
    klog("ZEROOS: nvme%u: controller reset %s (%d).",c->index,rc ? "failed" : "complete",rc);
    kmutex_unlock(&c->reset_lock);
    return rc;
}

static const struct block_device_ops nvme_ops={
    .submit=nvme_submit,
    .kick=nvme_kick,
    .poll=nvme_poll,
    .reset=nvme_reset,
    .power_cut=0,
};

static void nvme_setup_interrupts(struct nvme_controller *c) {
    struct pci_device *pci=c->pci;
    if (pci->msix_cap && pci->msix_table_size>=2U &&
        pci_msix_setup(pci,&c->msix_table)==0) {
        uint32_t ok=0;
        for (uint32_t i=0; i<c->io_queue_count; ++i) {
            struct nvme_queue *q=&c->io[i];
            q->msix_entry=i+1U;     /* entry 0: admin (polled, masked) */
            if (q->msix_entry>=pci->msix_table_size)
                break;
            int vector=irq_vector_alloc(nvme_irq,q);
            if (vector<0)
                break;
            q->vector=vector;
            pci_msix_set_entry(pci,c->msix_table,(uint16_t)q->msix_entry,
                               (uint8_t)vector,0);
            ++ok;
        }
        if (ok==c->io_queue_count) {
            pci_msix_enable(pci,1);
            pci_enable_device(pci,1);
            return;
        }
        for (uint32_t i=0; i<c->io_queue_count; ++i) {
            if (c->io[i].vector>=0)
                irq_vector_free(c->io[i].vector);
            c->io[i].vector=-1;
            c->io[i].msix_entry=0;
        }
    }
    if (pci->msi_cap) {
        int vector=irq_vector_alloc(nvme_irq_all,c);
        if (vector>=0 && pci_enable_msi(pci,(uint8_t)vector)==0) {
            c->msi_vector=vector;
            pci_enable_device(pci,1);
            return;
        }
        if (vector>=0)
            irq_vector_free(vector);
    }
}

static int nvme_controller_init(struct pci_device *pci) {
    if (nvme_count>=NVME_MAX_CONTROLLERS)
        return -SE_NOSPC;
    struct nvme_controller *c=&nvme_controllers[nvme_count];
    memset(c,0,sizeof(*c));
    c->pci=pci;
    c->index=nvme_count;
    c->msi_vector=-1;
    kmutex_init(&c->reset_lock,"nvme-reset");
    pci_enable_device(pci,0);
    uint64_t bar=pci_map_bar(pci,0,0);
    if (!bar) {
        klog("ZEROOS: nvme%u: BAR0 mapping failed.",c->index);
        return -SE_NODEV;
    }
    c->bar=(volatile uint8_t *)bar;
    c->cap=nvme_read64(c,NVME_REG_CAP);
    c->stride=4U<<((c->cap>>32)&0xfU);
    uint32_t mqes=(uint32_t)(c->cap&0xffffU)+1U;
    uint32_t version=nvme_read32(c,NVME_REG_VS);
    if (((c->cap>>37)&1U)==0 || ((c->cap>>48)&0xfU)>0) {
        klog("ZEROOS: nvme%u: unsupported command set or minimum page size.",c->index);
        return -SE_NODEV;
    }
    c->identify=(uint8_t *)page_alloc_zero();
    if (!c->identify ||
        nvme_queue_alloc(&c->admin,c,0,NVME_ADMIN_DEPTH,0)!=0)
        return -SE_NOMEM;
    uint32_t cpus=smp_online_count();
    c->io_queue_count=cpus<NVME_MAX_IO_QUEUES ? cpus : NVME_MAX_IO_QUEUES;
    if (c->io_queue_count==0)
        c->io_queue_count=1;
    uint32_t depth=mqes<NVME_IO_DEPTH ? mqes : NVME_IO_DEPTH;
    for (uint32_t i=0; i<c->io_queue_count; ++i)
        if (nvme_queue_alloc(&c->io[i],c,i+1U,depth,1)!=0)
            return -SE_NOMEM;
    int rc=nvme_enable(c);
    if (rc) {
        klog("ZEROOS: nvme%u: enable failed (%d).",c->index,rc);
        return rc;
    }
    struct nvme_sqe command;
    memset(&command,0,sizeof(command));
    command.cdw0=NVME_ADMIN_IDENTIFY;
    command.prp1=(uint64_t)c->identify;
    command.cdw10=1;
    if ((rc=nvme_admin(c,&command,0))!=0)
        return rc;
    char model[41];
    for (uint32_t i=0; i<40; ++i) {
        char ch=(char)c->identify[24+i];
        model[i]=(ch>=0x20 && ch<0x7f) ? ch : '?';
    }
    model[40]='\0';
    for (int i=39; i>=0 && model[i]==' '; --i)
        model[i]='\0';
    uint32_t mdts=c->identify[77];
    uint32_t nn=*(uint32_t *)(c->identify+516);
    int vwc=c->identify[525]&1U;
    uint32_t max_transfer=NVME_MAX_TRANSFER;
    if (mdts && (ZEROOS_PAGE_SIZE<<mdts)<max_transfer)
        max_transfer=(uint32_t)(ZEROOS_PAGE_SIZE<<mdts);

    /* First active namespace. */
    memset(&command,0,sizeof(command));
    command.cdw0=NVME_ADMIN_IDENTIFY;
    command.prp1=(uint64_t)c->identify;
    command.cdw10=2;
    if (nvme_admin(c,&command,0)==0)
        c->nsid=*(uint32_t *)c->identify;
    if (c->nsid==0 && nn)
        c->nsid=1;
    if (c->nsid==0) {
        klog("ZEROOS: nvme%u: no active namespace.",c->index);
        return -SE_NODEV;
    }
    memset(&command,0,sizeof(command));
    command.cdw0=NVME_ADMIN_IDENTIFY;
    command.nsid=c->nsid;
    command.prp1=(uint64_t)c->identify;
    command.cdw10=0;
    if ((rc=nvme_admin(c,&command,0))!=0)
        return rc;
    uint64_t nsze=*(uint64_t *)c->identify;
    uint32_t flbas=c->identify[26]&0xfU;
    uint32_t lbaf=*(uint32_t *)(c->identify+128U+flbas*4U);
    uint32_t lbads=(lbaf>>16)&0xffU;
    uint32_t metadata=lbaf&0xffffU;
    if (nsze==0 || lbads<9U || lbads>12U || metadata) {
        klog("ZEROOS: nvme%u: namespace %u format unsupported (lbads=%u ms=%u).",
             c->index,c->nsid,lbads,metadata);
        return -SE_NODEV;
    }
    uint32_t sector_size=1U<<lbads;

    memset(&command,0,sizeof(command));
    command.cdw0=NVME_ADMIN_SET_FEATURES;
    command.cdw10=0x07;
    command.cdw11=((c->io_queue_count-1U)<<16)|(c->io_queue_count-1U);
    uint32_t granted=0;
    if ((rc=nvme_admin(c,&command,&granted))!=0)
        return rc;
    uint32_t sq_granted=(granted&0xffffU)+1U, cq_granted=(granted>>16)+1U;
    uint32_t allowed=sq_granted<cq_granted ? sq_granted : cq_granted;
    if (allowed<c->io_queue_count)
        c->io_queue_count=allowed;

    nvme_setup_interrupts(c);
    if ((rc=nvme_create_io_queues(c))!=0) {
        klog("ZEROOS: nvme%u: I/O queue creation failed (%d).",c->index,rc);
        return rc;
    }
    struct block_device *device=block_device_alloc();
    if (!device)
        return -SE_NOSPC;
    ksnprintf(device->name,sizeof(device->name),"nvme%un1",c->index);
    device->ops=&nvme_ops;
    device->driver_data=c;
    device->sector_size=sector_size;
    device->sectors=nsze;
    device->max_sectors=max_transfer/sector_size;
    uint32_t total=c->io_queue_count*(depth-1U);
    device->queue_depth=total<BLOCK_POOL_MAX/2U ? total : BLOCK_POOL_MAX/2U;
    device->hw_queues=c->io_queue_count;
    device->media=BLOCK_MEDIA_NVME;
    int polled=c->io[0].vector<0 && c->msi_vector<0;
    device->flags=BLOCK_DEV_PRP_SEGMENTS|(vwc ? BLOCK_DEV_VOLATILE_CACHE : 0U)|
                  (polled ? BLOCK_DEV_POLLED : 0U);
    device->timeout_ticks=timer_frequency_hz()*5U;
    c->device=device;
    ++nvme_count;
    klog("ZEROOS: nvme%u: version=%u.%u model=\"%s\" nsid=%u lba=%u io_queues=%u depth=%u mdts_bytes=%u vwc=%d irq=%s.",
         c->index,version>>16,(version>>8)&0xffU,model,c->nsid,sector_size,
         c->io_queue_count,depth,max_transfer,vwc,
         c->io[0].vector>=0 ? "msix" : (c->msi_vector>=0 ? "msi" : "polled"));
    rc=block_register(device);
    if (rc) {
        c->device=0;
        device->in_use=0;
    }
    return rc;
}

int nvme_probe_all(void) {
    int found=0;
    for (uint32_t i=0; i<pci_device_count(); ++i) {
        struct pci_device *pci=pci_device_at(i);
        if (pci->class_code==0x01U && pci->subclass==0x08U &&
            pci->prog_if==0x02U && !pci->claimed) {
            pci->claimed=1;
            if (nvme_controller_init(pci)==0)
                ++found;
        }
    }
    return found;
}

void nvme_shutdown_all(void) {
    for (uint32_t i=0; i<nvme_count; ++i) {
        struct nvme_controller *c=&nvme_controllers[i];
        if (c->gone)
            continue;
        uint32_t cc=nvme_read32(c,NVME_REG_CC);
        nvme_write32(c,NVME_REG_CC,(cc&~(3U<<14))|(1U<<14));
        uint64_t deadline=timer_ticks()+nvme_timeout_ticks(c);
        while (((nvme_read32(c,NVME_REG_CSTS)>>2)&3U)!=2U &&
               timer_ticks()<deadline)
            task_yield();
        klog("ZEROOS: nvme%u: shutdown %s.",c->index,
             ((nvme_read32(c,NVME_REG_CSTS)>>2)&3U)==2U ? "complete" : "timed out");
    }
}

int nvme_is_nvme_device(struct block_device *device) {
    return device && block_root(device)->ops==&nvme_ops;
}

int nvme_test_mask_irq(struct block_device *device, int masked) {
    struct block_device *root=block_root(device);
    if (!nvme_is_nvme_device(root))
        return -SE_INVAL;
    struct nvme_controller *c=(struct nvme_controller *)root->driver_data;
    c->irq_masked=(uint32_t)masked;
    if (!c->msix_table)
        return -SE_NOTSUP;
    for (uint32_t i=0; i<c->io_queue_count; ++i)
        pci_msix_set_entry(c->pci,c->msix_table,(uint16_t)c->io[i].msix_entry,
                           (uint8_t)c->io[i].vector,masked);
    return 0;
}
