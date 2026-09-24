/*
 * AHCI (SATA) host driver.
 *
 * Bring-up: BIOS/OS handoff -> AHCI enable -> HBA reset -> per port: stop
 * engines, DMA structures, FIS receive, SERR clear, presence (DET=3) with
 * COMRESET retry, BSY/DRQ settle, start, IDENTIFY DEVICE (polled, bounded),
 * block device registration.
 *
 * I/O: NCQ (READ/WRITE FPDMA QUEUED, up to 32 tags) when the device and HBA
 * support it, else READ/WRITE DMA EXT with one outstanding command. FLUSH
 * CACHE EXT is non-queued: it is only issued on an idle port and nothing is
 * issued behind it (-SE_AGAIN back-pressure into the block scheduler).
 *
 * Completion: one MSI vector per HBA (degraded to watchdog polling when MSI
 * is unavailable); completed slots = active & ~(PxCI | PxSACT).
 *
 * Errors: TFES/HBFS/HBDS/IFS and connect changes are handed from the IRQ
 * handler to a per-HBA error-handler task (event driven), which completes
 * finished slots, restarts the port (CLO or COMRESET) and fails the rest
 * with a retryable -SE_IO (or -SE_NODEV when the device vanished). Watchdog
 * timeouts use the same restart path with -SE_TIMEDOUT.
 */
#include "ahci.h"
#include "../interrupts.h"
#include "../kstring.h"
#include "../memory.h"
#include "../pci.h"
#include "../task.h"
#include "../timer.h"

#define AHCI_MAX_HBAS 2U
#define AHCI_MAX_PORTS 32U
#define AHCI_PRDT_ENTRIES 32U
#define AHCI_TABLE_SIZE (0x80U + AHCI_PRDT_ENTRIES * 16U)

/* HBA registers */
#define HBA_CAP 0x00
#define HBA_GHC 0x04
#define HBA_IS 0x08
#define HBA_PI 0x0c
#define HBA_VS 0x10
#define HBA_CAP2 0x24
#define HBA_BOHC 0x28
#define CAP_S64A (1U << 31)
#define CAP_SNCQ (1U << 30)
#define CAP_SCLO (1U << 24)
#define GHC_HR (1U << 0)
#define GHC_IE (1U << 1)
#define GHC_AE (1U << 31)

/* Port registers */
#define PX_CLB 0x00
#define PX_CLBU 0x04
#define PX_FB 0x08
#define PX_FBU 0x0c
#define PX_IS 0x10
#define PX_IE 0x14
#define PX_CMD 0x18
#define PX_TFD 0x20
#define PX_SIG 0x24
#define PX_SSTS 0x28
#define PX_SCTL 0x2c
#define PX_SERR 0x30
#define PX_SACT 0x34
#define PX_CI 0x38
#define CMD_ST (1U << 0)
#define CMD_SUD (1U << 1)
#define CMD_POD (1U << 2)
#define CMD_CLO (1U << 3)
#define CMD_FRE (1U << 4)
#define CMD_FR (1U << 14)
#define CMD_CR (1U << 15)
#define TFD_ERR (1U << 0)
#define TFD_DRQ (1U << 3)
#define TFD_BSY (1U << 7)
#define IS_DHRS (1U << 0)
#define IS_PSS (1U << 1)
#define IS_DSS (1U << 2)
#define IS_SDBS (1U << 3)
#define IS_DPS (1U << 5)
#define IS_PCS (1U << 6)
#define IS_PRCS (1U << 22)
#define IS_OFS (1U << 24)
#define IS_INFS (1U << 26)
#define IS_IFS (1U << 27)
#define IS_HBDS (1U << 28)
#define IS_HBFS (1U << 29)
#define IS_TFES (1U << 30)
#define IS_ERRORS (IS_TFES | IS_HBFS | IS_HBDS | IS_IFS | IS_OFS | IS_INFS)
#define IS_HOTPLUG (IS_PCS | IS_PRCS)
#define IS_ENABLE (IS_DHRS | IS_PSS | IS_DSS | IS_SDBS | IS_DPS | \
                   IS_ERRORS | IS_HOTPLUG)

#define ATA_CMD_READ_DMA_EXT 0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_CMD_READ_FPDMA 0x60
#define ATA_CMD_WRITE_FPDMA 0x61
#define ATA_CMD_FLUSH_EXT 0xea
#define ATA_CMD_FLUSH 0xe7
#define ATA_CMD_IDENTIFY 0xec

struct ahci_hba;

struct ahci_port {
    struct ahci_hba *hba;
    uint32_t index;
    volatile uint8_t *regs;
    struct spinlock lock;
    struct kmutex recovery;
    uint8_t *clb;
    uint8_t *fis;
    uint8_t *tables;
    uint32_t slots;
    uint32_t active;
    uint32_t issue_pending;
    uint32_t ncq_pending;
    uint32_t non_queued;            /* slot mask of non-NCQ commands */
    uint32_t ie_saved;
    uint32_t irq_masked;
    struct block_request *slot_request[AHCI_MAX_PORTS];
    struct block_device *device;
    int ncq;
    int flush_ext;
    int present;
    uint64_t errors;
    uint64_t restarts;
    uint64_t hard_resets;
};

struct ahci_hba {
    struct pci_device *pci;
    volatile uint8_t *abar;
    uint32_t cap;
    uint32_t ports_implemented;
    int vector;
    uint32_t index;
    uint32_t eh_pending;            /* port mask, atomic */
    uint32_t eh_status[AHCI_MAX_PORTS];
    struct kcompletion eh_event;
    uint64_t eh_task;
    struct ahci_port ports[AHCI_MAX_PORTS];
};

/* Bounded quiesce window before stopping a port on reset (10 ms ticks). */
#define AHCI_RESET_DRAIN_TICKS 20U

static struct ahci_hba ahci_hbas[AHCI_MAX_HBAS];
static uint32_t ahci_hba_count;
static uint32_t ahci_disk_count;
static const struct block_device_ops ahci_ops;

static inline uint32_t hba_read(struct ahci_hba *hba, uint32_t reg) {
    return *(volatile uint32_t *)(hba->abar+reg);
}
static inline void hba_write(struct ahci_hba *hba, uint32_t reg, uint32_t value) {
    *(volatile uint32_t *)(hba->abar+reg)=value;
}
static inline uint32_t port_read(struct ahci_port *port, uint32_t reg) {
    return *(volatile uint32_t *)(port->regs+reg);
}
static inline void port_write(struct ahci_port *port, uint32_t reg, uint32_t value) {
    *(volatile uint32_t *)(port->regs+reg)=value;
}

/* Bounded register wait for task context. Returns 0 when (reg & mask) ==
 * value within `ms` milliseconds (rounded up to scheduler ticks). */
static int ahci_wait(volatile uint8_t *base, uint32_t reg, uint32_t mask,
                     uint32_t value, uint32_t ms) {
    uint64_t hz=timer_frequency_hz();
    uint64_t deadline=timer_ticks()+(ms*hz+999ULL)/1000ULL+1ULL;
    for (;;) {
        if (((*(volatile uint32_t *)(base+reg))&mask)==value)
            return 0;
        if (timer_ticks()>deadline)
            return -SE_TIMEDOUT;
        task_yield();
    }
}

static int ahci_port_stop(struct ahci_port *port) {
    uint32_t cmd=port_read(port,PX_CMD);
    if (cmd&CMD_ST)
        port_write(port,PX_CMD,cmd&~CMD_ST);
    if (ahci_wait(port->regs,PX_CMD,CMD_CR,0,500)!=0)
        return -SE_TIMEDOUT;
    cmd=port_read(port,PX_CMD);
    if (cmd&CMD_FRE)
        port_write(port,PX_CMD,cmd&~CMD_FRE);
    return ahci_wait(port->regs,PX_CMD,CMD_FR,0,500);
}

static int ahci_port_present(struct ahci_port *port) {
    return (port_read(port,PX_SSTS)&0xfU)==3U;
}

static int ahci_comreset(struct ahci_port *port) {
    uint32_t sctl=port_read(port,PX_SCTL);
    /* DET=1 (COMRESET), IPM=3 (no partial/slumber). */
    port_write(port,PX_SCTL,(sctl&~0xf0fU)|0x301U);
    task_sleep_ticks(2);
    port_write(port,PX_SCTL,(sctl&~0xf0fU)|0x300U);
    if (ahci_wait(port->regs,PX_SSTS,0xfU,3U,1000)!=0)
        return -SE_NODEV;
    port_write(port,PX_SERR,0xffffffffU);
    return 0;
}

static int ahci_port_start(struct ahci_port *port) {
    port_write(port,PX_CMD,port_read(port,PX_CMD)|CMD_FRE);
    port_write(port,PX_SERR,0xffffffffU);
    port_write(port,PX_IS,0xffffffffU);
    if (ahci_wait(port->regs,PX_TFD,TFD_BSY|TFD_DRQ,0,1000)!=0) {
        if (port->hba->cap&CAP_SCLO) {
            port_write(port,PX_CMD,port_read(port,PX_CMD)|CMD_CLO);
            (void)ahci_wait(port->regs,PX_CMD,CMD_CLO,0,500);
        }
        if (port_read(port,PX_TFD)&(TFD_BSY|TFD_DRQ)) {
            if (ahci_comreset(port)!=0)
                return -SE_NODEV;
            if (ahci_wait(port->regs,PX_TFD,TFD_BSY|TFD_DRQ,0,2000)!=0)
                return -SE_TIMEDOUT;
        }
    }
    port_write(port,PX_CMD,port_read(port,PX_CMD)|CMD_ST|CMD_POD|CMD_SUD);
    return 0;
}

static void ahci_build_fis(uint8_t *fis, uint8_t command, uint64_t lba,
                           uint16_t count, uint16_t features, uint8_t device) {
    memset(fis,0,20);
    fis[0]=0x27;                    /* register H2D */
    fis[1]=0x80;                    /* command */
    fis[2]=command;
    fis[3]=(uint8_t)features;
    fis[4]=(uint8_t)lba;
    fis[5]=(uint8_t)(lba>>8);
    fis[6]=(uint8_t)(lba>>16);
    fis[7]=device;
    fis[8]=(uint8_t)(lba>>24);
    fis[9]=(uint8_t)(lba>>32);
    fis[10]=(uint8_t)(lba>>40);
    fis[11]=(uint8_t)(features>>8);
    fis[12]=(uint8_t)count;
    fis[13]=(uint8_t)(count>>8);
}

/* Fill command header + table for `slot`. Caller holds port->lock. */
static int ahci_prepare_slot(struct ahci_port *port, uint32_t slot,
                             uint8_t command, uint64_t lba, uint16_t count,
                             uint16_t features, uint8_t device, int write,
                             const struct block_segment *segments,
                             uint32_t segment_count) {
    uint8_t *table=port->tables+slot*AHCI_TABLE_SIZE;
    uint32_t prd=0;
    memset(table,0,0x80);
    ahci_build_fis(table,command,lba,count,features,device);
    for (uint32_t i=0; i<segment_count; ++i) {
        uint64_t address=segments[i].physical;
        uint32_t remaining=segments[i].length;
        while (remaining) {
            uint32_t piece=remaining>0x400000U ? 0x400000U : remaining;
            if (prd>=AHCI_PRDT_ENTRIES || (piece&1U))
                return -SE_INVAL;
            uint32_t *entry=(uint32_t *)(table+0x80U+prd*16U);
            entry[0]=(uint32_t)address;
            entry[1]=(uint32_t)(address>>32);
            entry[2]=0;
            entry[3]=(piece-1U)&0x3fffffU;
            address+=piece;
            remaining-=piece;
            ++prd;
        }
    }
    uint32_t *header=(uint32_t *)(port->clb+slot*32U);
    header[0]=5U|(write ? (1U<<6) : 0U)|(prd<<16);
    header[1]=0;
    header[2]=(uint32_t)(uint64_t)table;
    header[3]=(uint32_t)((uint64_t)table>>32);
    header[4]=header[5]=header[6]=header[7]=0;
    return 0;
}

/* Polled command for bring-up (IDENTIFY) on an idle, started port. */
static int ahci_exec_polled(struct ahci_port *port, uint8_t command, void *buffer,
                            uint32_t bytes) {
    struct block_segment segment={(uint64_t)buffer,bytes,0};
    uint64_t flags=spin_lock_irqsave(&port->lock);
    int rc=ahci_prepare_slot(port,0,command,0,0,0,0,0,&segment,buffer ? 1U : 0U);
    spin_unlock_irqrestore(&port->lock,flags);
    if (rc)
        return rc;
    port_write(port,PX_IS,0xffffffffU);
    port_write(port,PX_CI,1U);
    uint64_t deadline=timer_ticks()+timer_frequency_hz()*2ULL;
    while (port_read(port,PX_CI)&1U) {
        if (port_read(port,PX_IS)&IS_TFES)
            return -SE_IO;
        if (timer_ticks()>deadline)
            return -SE_TIMEDOUT;
        task_yield();
    }
    if (port_read(port,PX_TFD)&TFD_ERR)
        return -SE_IO;
    return 0;
}

static void ahci_ident_string(const uint16_t *words, uint32_t first,
                              uint32_t count, char *out) {
    uint32_t n=0;
    for (uint32_t i=0; i<count; ++i) {
        out[n++]=(char)(words[first+i]>>8);
        out[n++]=(char)(words[first+i]&0xffU);
    }
    out[n]='\0';
    while (n && out[n-1]==' ')
        out[--n]='\0';
    for (uint32_t i=0; i<n; ++i)
        if (out[i]<0x20 || out[i]>0x7e)
            out[i]='?';
}

/* ---------------------------------------------------------------- I/O */

static int ahci_submit(struct block_device *device, struct block_request *request) {
    struct ahci_port *port=(struct ahci_port *)device->driver_data;
    uint64_t flags=spin_lock_irqsave(&port->lock);
    int queued=port->ncq && request->op!=BLOCK_OP_FLUSH;
    /* Non-queued commands need an otherwise idle port; nothing may be
     * issued while one is outstanding. */
    if (port->non_queued || (!queued && port->active) || !port->present) {
        spin_unlock_irqrestore(&port->lock,flags);
        return port->present ? -SE_AGAIN : -SE_NODEV;
    }
    uint32_t free=~port->active&(port->slots==32U ? 0xffffffffU :
                                 ((1U<<port->slots)-1U));
    if (!free) {
        spin_unlock_irqrestore(&port->lock,flags);
        return -SE_AGAIN;
    }
    uint32_t slot=(uint32_t)__builtin_ctz(free);
    int rc;
    if (request->op==BLOCK_OP_FLUSH) {
        rc=ahci_prepare_slot(port,slot,port->flush_ext ? ATA_CMD_FLUSH_EXT :
                             ATA_CMD_FLUSH,0,0,0,0x40,0,0,0);
    } else if (queued) {
        rc=ahci_prepare_slot(port,slot,request->op==BLOCK_OP_WRITE ?
                             ATA_CMD_WRITE_FPDMA : ATA_CMD_READ_FPDMA,
                             request->lba,(uint16_t)(slot<<3),
                             (uint16_t)request->sectors,0x40,
                             request->op==BLOCK_OP_WRITE,request->segments,
                             request->segment_count);
    } else {
        rc=ahci_prepare_slot(port,slot,request->op==BLOCK_OP_WRITE ?
                             ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT,
                             request->lba,(uint16_t)request->sectors,0,0x40,
                             request->op==BLOCK_OP_WRITE,request->segments,
                             request->segment_count);
    }
    if (rc) {
        spin_unlock_irqrestore(&port->lock,flags);
        return rc;
    }
    request->tag=slot;
    port->slot_request[slot]=request;
    port->active|=1U<<slot;
    port->issue_pending|=1U<<slot;
    if (queued)
        port->ncq_pending|=1U<<slot;
    else
        port->non_queued|=1U<<slot;
    spin_unlock_irqrestore(&port->lock,flags);
    return 0;
}

static void ahci_kick(struct block_device *device, uint32_t hw_queue) {
    struct ahci_port *port=(struct ahci_port *)device->driver_data;
    (void)hw_queue;
    uint64_t flags=spin_lock_irqsave(&port->lock);
    uint32_t issue=port->issue_pending;
    uint32_t ncq=port->ncq_pending;
    port->issue_pending=0;
    port->ncq_pending=0;
    __asm__ volatile ("" ::: "memory");     /* tables visible before doorbell */
    if (ncq)
        port_write(port,PX_SACT,ncq);
    if (issue)
        port_write(port,PX_CI,issue);
    spin_unlock_irqrestore(&port->lock,flags);
}

/* Collect finished slots. Returns requests via `done` (count). */
static uint32_t ahci_harvest_locked(struct ahci_port *port,
                                    struct block_request **done) {
    uint32_t outstanding=port_read(port,PX_CI)|port_read(port,PX_SACT)|
                         port->issue_pending;
    uint32_t finished=port->active&~outstanding;
    uint32_t count=0;
    while (finished) {
        uint32_t slot=(uint32_t)__builtin_ctz(finished);
        finished&=finished-1U;
        done[count++]=port->slot_request[slot];
        port->slot_request[slot]=0;
        port->active&=~(1U<<slot);
        port->non_queued&=~(1U<<slot);
    }
    return count;
}

static void ahci_complete_list(struct block_request **list, uint32_t count,
                               int status) {
    for (uint32_t i=0; i<count; ++i)
        if (list[i])
            block_complete(list[i],status);
}

static void ahci_port_service(struct ahci_port *port, uint32_t is) {
    struct block_request *done[AHCI_MAX_PORTS];
    uint32_t count;
    uint64_t flags=spin_lock_irqsave(&port->lock);
    if (is&(IS_ERRORS|IS_HOTPLUG)) {
        /* Park the port for the error handler: no harvesting here, since
         * CI/SACT state is not trustworthy until the engine is stopped. */
        port->ie_saved=IS_ENABLE;
        port_write(port,PX_IE,0);
        spin_unlock_irqrestore(&port->lock,flags);
        struct ahci_hba *hba=port->hba;
        __atomic_fetch_or(&hba->eh_status[port->index],is,__ATOMIC_ACQ_REL);
        __atomic_fetch_or(&hba->eh_pending,1U<<port->index,__ATOMIC_ACQ_REL);
        kcompletion_signal(&hba->eh_event);
        return;
    }
    count=ahci_harvest_locked(port,done);
    spin_unlock_irqrestore(&port->lock,flags);
    ahci_complete_list(done,count,0);
}

static void ahci_irq(uint8_t vector, struct interrupt_frame *frame, void *context) {
    struct ahci_hba *hba=(struct ahci_hba *)context;
    (void)vector;
    (void)frame;
    uint32_t pending=hba_read(hba,HBA_IS);
    while (pending) {
        uint32_t index=(uint32_t)__builtin_ctz(pending);
        pending&=pending-1U;
        struct ahci_port *port=&hba->ports[index];
        uint32_t is=port_read(port,PX_IS);
        port_write(port,PX_IS,is);
        hba_write(hba,HBA_IS,1U<<index);
        if (port->device)
            block_note_interrupt(port->device);
        if (port->regs)
            ahci_port_service(port,is);
    }
}

static void ahci_poll(struct block_device *device) {
    struct ahci_port *port=(struct ahci_port *)device->driver_data;
    uint32_t is=port_read(port,PX_IS);
    if (is&(IS_ERRORS|IS_HOTPLUG)) {
        port_write(port,PX_IS,is);
        ahci_port_service(port,is);
        return;
    }
    ahci_port_service(port,0);
}

/*
 * Port recovery (task context, serialized by port->recovery):
 * complete finished slots, stop the engine, clear errors, CLO/COMRESET,
 * restart, and fail every other outstanding command with `status`.
 */
static int ahci_port_recover(struct ahci_port *port, int status) {
    struct block_request *done[AHCI_MAX_PORTS];
    struct block_request *failed[AHCI_MAX_PORTS];
    uint32_t done_count, failed_count=0;
    int rc;

    kmutex_lock(&port->recovery);
    uint64_t flags;
    if (status==-SE_TIMEDOUT && port->present) {
        /* Timeout/administrative reset: the engine is still running, so
         * give in-progress commands a bounded window to finish and harvest
         * them. Errors (TFES) and hot-plug skip this; the engine has halted
         * and nothing more will complete. */
        for (uint32_t tick=0; tick<=AHCI_RESET_DRAIN_TICKS; ++tick) {
            flags=spin_lock_irqsave(&port->lock);
            done_count=ahci_harvest_locked(port,done);
            uint32_t active=port->active;
            spin_unlock_irqrestore(&port->lock,flags);
            ahci_complete_list(done,done_count,0);
            if (!active || tick==AHCI_RESET_DRAIN_TICKS)
                break;
            task_sleep_ticks(1);
        }
    }
    flags=spin_lock_irqsave(&port->lock);
    port_write(port,PX_IE,0);
    done_count=ahci_harvest_locked(port,done);
    uint32_t rest=port->active;
    while (rest) {
        uint32_t slot=(uint32_t)__builtin_ctz(rest);
        rest&=rest-1U;
        failed[failed_count++]=port->slot_request[slot];
        port->slot_request[slot]=0;
    }
    port->active=0;
    port->non_queued=0;
    port->issue_pending=0;
    port->ncq_pending=0;
    spin_unlock_irqrestore(&port->lock,flags);
    ahci_complete_list(done,done_count,0);

    ++port->restarts;
    rc=ahci_port_stop(port);
    port_write(port,PX_SERR,0xffffffffU);
    port_write(port,PX_IS,0xffffffffU);
    /* Commands abandoned by the stop may still be held by the device
     * (queued NCQ tags); only a COMRESET returns the drive to a known
     * idle state, otherwise a reused tag can be silently dropped. */
    if (rc!=0 || failed_count || !ahci_port_present(port)) {
        ++port->hard_resets;
        rc=ahci_comreset(port);
    }
    if (rc==0 && !ahci_port_present(port))
        rc=-SE_NODEV;
    if (rc==0)
        rc=ahci_port_start(port);
    flags=spin_lock_irqsave(&port->lock);
    if (rc==0) {
        port_write(port,PX_IE,port->irq_masked ? 0 : IS_ENABLE);
    } else {
        port->present=0;
    }
    spin_unlock_irqrestore(&port->lock,flags);
    if (rc==-SE_NODEV)
        status=-SE_NODEV;
    ahci_complete_list(failed,failed_count,status);
    kmutex_unlock(&port->recovery);
    return rc;
}

static int ahci_reset(struct block_device *device) {
    struct ahci_port *port=(struct ahci_port *)device->driver_data;
    return ahci_port_recover(port,-SE_TIMEDOUT);
}

static void ahci_eh_task(void *argument) {
    struct ahci_hba *hba=(struct ahci_hba *)argument;
    for (;;) {
        kcompletion_wait(&hba->eh_event);
        kcompletion_reset(&hba->eh_event);
        uint32_t pending=__atomic_exchange_n(&hba->eh_pending,0U,__ATOMIC_ACQ_REL);
        while (pending) {
            uint32_t index=(uint32_t)__builtin_ctz(pending);
            pending&=pending-1U;
            struct ahci_port *port=&hba->ports[index];
            uint32_t is=__atomic_exchange_n(&hba->eh_status[index],0U,__ATOMIC_ACQ_REL);
            ++port->errors;
            if (!port->device)
                continue;
            if (is&IS_HOTPLUG) {
                if (!ahci_port_present(port)) {
                    klog("ZEROOS: ahci%u port %u: device removed.",hba->index,index);
                    uint64_t flags=spin_lock_irqsave(&port->lock);
                    port->present=0;
                    spin_unlock_irqrestore(&port->lock,flags);
                    block_device_set_state(port->device,BLOCK_STATE_GONE);
                    (void)ahci_port_recover(port,-SE_NODEV);
                    continue;
                }
            }
            klog("ZEROOS: ahci%u port %u: error is=0x%x tfd=0x%x serr=0x%x; recovering.",
                 hba->index,index,is,port_read(port,PX_TFD),port_read(port,PX_SERR));
            int rc=ahci_port_recover(port,-SE_IO);
            if (rc!=0) {
                klog("ZEROOS: ahci%u port %u: recovery failed (%d).",hba->index,index,rc);
                block_device_set_state(port->device,
                    rc==-SE_NODEV ? BLOCK_STATE_GONE : BLOCK_STATE_FAILED);
            }
        }
    }
}

static const struct block_device_ops ahci_ops={
    .submit=ahci_submit,
    .kick=ahci_kick,
    .poll=ahci_poll,
    .reset=ahci_reset,
    .power_cut=0,
};

/* ----------------------------------------------------------- bring-up */

static int ahci_port_init(struct ahci_hba *hba, uint32_t index) {
    struct ahci_port *port=&hba->ports[index];
    port->hba=hba;
    port->index=index;
    port->regs=hba->abar+0x100U+index*0x80U;
    spinlock_init(&port->lock);
    kmutex_init(&port->recovery,"ahci-port");
    port->slots=((hba->cap>>8)&0x1fU)+1U;

    if (ahci_port_stop(port)!=0) {
        klog("ZEROOS: ahci%u port %u: engine did not stop.",hba->index,index);
        return -SE_IO;
    }
    /* 1 page: command list (1 KiB) + received FIS (256 B); 5 pages of
     * command tables (32 x 640 B). All identity mapped, below 4 GiB. */
    port->clb=(uint8_t *)page_alloc_contiguous(6);
    if (!port->clb)
        return -SE_NOMEM;
    memset(port->clb,0,6U*ZEROOS_PAGE_SIZE);
    port->fis=port->clb+2048U;
    port->tables=port->clb+ZEROOS_PAGE_SIZE;
    if (!(hba->cap&CAP_S64A) && (uint64_t)port->clb+6U*ZEROOS_PAGE_SIZE>0x100000000ULL)
        return -SE_NOMEM;
    port_write(port,PX_CLB,(uint32_t)(uint64_t)port->clb);
    port_write(port,PX_CLBU,(uint32_t)((uint64_t)port->clb>>32));
    port_write(port,PX_FB,(uint32_t)(uint64_t)port->fis);
    port_write(port,PX_FBU,(uint32_t)((uint64_t)port->fis>>32));
    port_write(port,PX_CMD,port_read(port,PX_CMD)|CMD_FRE);
    port_write(port,PX_SERR,0xffffffffU);
    port_write(port,PX_IS,0xffffffffU);

    if (!ahci_port_present(port)) {
        /* After HBA reset the HBA spins up every port itself (CAP.SSS=0)
         * or we do it; give the PHY 50 ms to report detection (DET!=0)
         * before paying for a COMRESET, so empty ports cost ~50 ms. */
        if (hba->cap&(1U<<27))
            port_write(port,PX_CMD,port_read(port,PX_CMD)|CMD_SUD);
        (void)ahci_wait(port->regs,PX_SSTS,0xfU,3U,50);
        uint32_t det=port_read(port,PX_SSTS)&0xfU;
        if (det==0)
            return -SE_NODEV;               /* empty port */
        if (det!=3U && ahci_comreset(port)!=0)
            return -SE_NODEV;
    }
    uint32_t sig=port_read(port,PX_SIG);
    if (sig!=0x00000101U) {
        klog("ZEROOS: ahci%u port %u: signature 0x%x not an ATA disk (ATAPI/PM unsupported).",
             hba->index,index,sig);
        return -SE_NODEV;
    }
    if (ahci_port_start(port)!=0) {
        klog("ZEROOS: ahci%u port %u: start failed.",hba->index,index);
        return -SE_IO;
    }
    port->present=1;

    uint16_t *identify=(uint16_t *)page_alloc_zero();
    if (!identify)
        return -SE_NOMEM;
    int rc=ahci_exec_polled(port,ATA_CMD_IDENTIFY,identify,512);
    if (rc) {
        klog("ZEROOS: ahci%u port %u: IDENTIFY failed (%d).",hba->index,index,rc);
        page_free(identify);
        return rc;
    }
    char model[41];
    ahci_ident_string(identify,27,20,model);
    int lba48=(identify[83]&(1U<<10))!=0;
    uint64_t sectors=lba48 ?
        ((uint64_t)identify[100]|((uint64_t)identify[101]<<16)|
         ((uint64_t)identify[102]<<32)|((uint64_t)identify[103]<<48)) :
        ((uint64_t)identify[60]|((uint64_t)identify[61]<<16));
    uint32_t sector_size=512;
    if ((identify[106]&0xc000U)==0x4000U && (identify[106]&(1U<<12)))
        sector_size=((uint32_t)identify[117]|((uint32_t)identify[118]<<16))*2U;
    int ncq=(hba->cap&CAP_SNCQ) && (identify[76]&(1U<<8));
    uint32_t ncq_depth=(identify[75]&0x1fU)+1U;
    uint16_t rotation=identify[217];
    port->flush_ext=(identify[83]&(1U<<13))!=0;
    page_free(identify);
    if (!lba48 || sectors==0 || (sector_size!=512U && sector_size!=4096U)) {
        klog("ZEROOS: ahci%u port %u: unsupported device (lba48=%d sector=%u).",
             hba->index,index,lba48,sector_size);
        return -SE_NODEV;
    }
    port->ncq=ncq;
    if (ncq && ncq_depth<port->slots)
        port->slots=ncq_depth;

    struct block_device *device=block_device_alloc();
    if (!device)
        return -SE_NOSPC;
    ksnprintf(device->name,sizeof(device->name),"sata%u",ahci_disk_count++);
    device->ops=&ahci_ops;
    device->driver_data=port;
    device->sector_size=sector_size;
    device->sectors=sectors;
    device->max_sectors=(128U*1024U)/sector_size;
    device->queue_depth=ncq ? port->slots : 1U;
    device->hw_queues=1;
    device->media=rotation==1U ? BLOCK_MEDIA_SSD : BLOCK_MEDIA_HDD;
    device->flags=BLOCK_DEV_VOLATILE_CACHE|(hba->vector<0 ? BLOCK_DEV_POLLED : 0U);
    device->timeout_ticks=timer_frequency_hz()*5U;
    port->device=device;
    klog("ZEROOS: ahci%u port %u: model=\"%s\" sectors=%llu ncq=%d depth=%u rotation=%u.",
         hba->index,index,model,sectors,ncq,device->queue_depth,rotation);
    rc=block_register(device);
    if (rc) {
        port->device=0;
        device->in_use=0;
        return rc;
    }
    port_write(port,PX_IE,IS_ENABLE);
    return 0;
}

static int ahci_hba_init(struct pci_device *pci) {
    if (ahci_hba_count>=AHCI_MAX_HBAS)
        return -SE_NOSPC;
    struct ahci_hba *hba=&ahci_hbas[ahci_hba_count];
    memset(hba,0,sizeof(*hba));
    hba->pci=pci;
    hba->index=ahci_hba_count;
    hba->vector=-1;
    kcompletion_init(&hba->eh_event);
    pci_enable_device(pci,0);
    uint64_t abar=pci_map_bar(pci,5,0);
    if (!abar) {
        klog("ZEROOS: ahci: BAR5 mapping failed.");
        return -SE_NODEV;
    }
    hba->abar=(volatile uint8_t *)abar;

    /* BIOS/OS handoff. */
    if (hba_read(hba,HBA_CAP2)&1U) {
        hba_write(hba,HBA_BOHC,hba_read(hba,HBA_BOHC)|2U);
        (void)ahci_wait(hba->abar,HBA_BOHC,1U,0,2000);
    }
    hba_write(hba,HBA_GHC,hba_read(hba,HBA_GHC)|GHC_AE);
    hba_write(hba,HBA_GHC,GHC_AE|GHC_HR);
    if (ahci_wait(hba->abar,HBA_GHC,GHC_HR,0,1000)!=0) {
        klog("ZEROOS: ahci: HBA reset timed out.");
        return -SE_TIMEDOUT;
    }
    hba_write(hba,HBA_GHC,GHC_AE);
    hba->cap=hba_read(hba,HBA_CAP);
    hba->ports_implemented=hba_read(hba,HBA_PI);
    uint32_t version=hba_read(hba,HBA_VS);

    if (pci->msi_cap && pci_msi_supported()) {
        int vector=irq_vector_alloc(ahci_irq,hba);
        if (vector>=0 && pci_enable_msi(pci,(uint8_t)vector)==0) {
            hba->vector=vector;
            pci_enable_device(pci,1);
        } else if (vector>=0) {
            irq_vector_free(vector);
        }
    }
    ++ahci_hba_count;
    klog("ZEROOS: ahci%u: version=%x.%x cap=0x%x ports=0x%x slots=%u ncq=%u s64a=%u irq=%s.",
         hba->index,version>>16,version&0xffffU,hba->cap,hba->ports_implemented,
         ((hba->cap>>8)&0x1fU)+1U,(hba->cap&CAP_SNCQ) ? 1U : 0U,
         (hba->cap&CAP_S64A) ? 1U : 0U,hba->vector>=0 ? "msi" : "polled");

    uint32_t disks=0;
    for (uint32_t i=0; i<AHCI_MAX_PORTS; ++i)
        if ((hba->ports_implemented&(1U<<i)) && ahci_port_init(hba,i)==0)
            ++disks;
    hba_write(hba,HBA_IS,0xffffffffU);
    hba_write(hba,HBA_GHC,GHC_AE|GHC_IE);
    if (task_create(ahci_eh_task,hba,&hba->eh_task)!=0)
        klog("ZEROOS: ahci%u: error-handler task creation failed.",hba->index);
    return (int)disks;
}

int ahci_probe_all(void) {
    int disks=0;
    for (uint32_t i=0; i<pci_device_count(); ++i) {
        struct pci_device *pci=pci_device_at(i);
        if (pci->class_code==0x01U && pci->subclass==0x06U &&
            pci->prog_if==0x01U && !pci->claimed) {
            pci->claimed=1;
            int rc=ahci_hba_init(pci);
            if (rc>0)
                disks+=rc;
        }
    }
    return disks;
}

int ahci_is_ahci_device(struct block_device *device) {
    return device && block_root(device)->ops==&ahci_ops;
}

int ahci_test_mask_irq(struct block_device *device, int masked) {
    struct block_device *root=block_root(device);
    if (!ahci_is_ahci_device(root))
        return -SE_INVAL;
    struct ahci_port *port=(struct ahci_port *)root->driver_data;
    uint64_t flags=spin_lock_irqsave(&port->lock);
    port->irq_masked=(uint32_t)masked;
    port_write(port,PX_IE,masked ? 0 : IS_ENABLE);
    spin_unlock_irqrestore(&port->lock,flags);
    return 0;
}
