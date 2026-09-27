/*
 * ZJFS implementation. See zjfs.h for the on-disk format and docs/ZJFS.md
 * for the transaction, recovery and repair contracts.
 *
 * Concurrency: one mutex per filesystem (fs->lock) serializes all metadata
 * (buffer cache, allocation, journal). Data I/O runs outside it through the
 * page cache; only block mapping takes the lock. Lock order: inode->lock ->
 * fs->lock -> page cache / block layer spinlocks.
 *
 * Transactions: every metadata operation calls zj_begin(credits) and then
 * modifies metadata buffers, each joining the single running transaction.
 * A transaction commits when full, on fsync/sync/unmount, or after
 * ZJ_COMMIT_INTERVAL ticks. Operations pre-check free space before
 * modifying anything, so ENOSPC never leaves a half-applied operation; an
 * I/O or checksum error after modification aborts the filesystem (the
 * running transaction is discarded, the filesystem becomes read-only and
 * the on-disk state is the last committed transaction).
 *
 * Commit (ordered data mode):
 *   1. write back dirty page-cache data of inodes that allocated blocks in
 *      this transaction (no stale data can become visible);
 *   2. write jsb(sequence), descriptor, block images and commit record;
 *      commit carries a CRC over all images, so a torn journal write is
 *      detected and discarded without a pre-commit flush;
 *   3. FLUSH (transaction durable);
 *   4. checkpoint the images to their home locations; FLUSH;
 *   5. sequence++. Blocks freed in the transaction become allocatable only
 *      now (pending-free set), so ordered data writes never overwrite
 *      blocks that the last durable state still references.
 */
#include "zjfs.h"
#include "../crc.h"
#include "../kstring.h"
#include "../memory.h"
#include "../task.h"
#include "../timer.h"

#define ZJ_BUF_MAX 256U
#define ZJ_BUF_HASH 128U
#define ZJ_TXN_MAX 192U
#define ZJ_ORDERED_MAX 64U
#define ZJ_PENDING_SLOTS 8192U
#define ZJ_PENDING_CAP 4096U
#define ZJ_TRUNC_CHUNK 1024U
#define ZJ_IO_BATCH 16U

#define ZB_VALID 0x1U
#define ZB_TXN   0x2U

enum zj_kind {
    ZK_RAW=0,
    ZK_META,          /* csum at 4092 (bitmaps, indirect, jsb, sb) */
    ZK_BITMAP,        /* ZK_META + magic at 4088 */
    ZK_INDIRECT,      /* ZK_META + owner ino at 4088 */
    ZK_DIR,           /* header with magic/owner/csum */
    ZK_INODES         /* per-inode checksums */
};

struct zj_buf {
    uint64_t block;
    uint8_t *data;
    uint32_t refcount;
    uint32_t flags;
    uint32_t kind;
    uint32_t owner;
    struct zj_buf *hash_next;
    struct zj_buf *lru_prev, *lru_next;
};

struct zj_fs {
    struct zj_super sb;                 /* in-memory primary superblock */
    struct vfs_superblock *vsb;
    struct block_device *device;
    uint32_t spb;
    uint32_t readonly;
    uint32_t aborted;
    uint32_t checking;                  /* zjfs_check instance (no VFS) */
    struct kmutex lock;
    struct zj_buf bufs[ZJ_BUF_MAX];
    struct zj_buf *hash[ZJ_BUF_HASH];
    struct zj_buf *lru_head, *lru_tail;
    struct zj_buf *txn[ZJ_TXN_MAX];
    uint32_t txn_count;
    uint32_t txn_max;
    uint64_t txn_start_tick;
    struct vfs_inode *ordered[ZJ_ORDERED_MAX];
    uint32_t ordered_count;
    uint32_t *pending;
    uint32_t pending_count;
    uint64_t seq;
    uint64_t block_hint;
    uint64_t inode_hint;
    uint64_t device_blocks;
    uint8_t *scratch;                   /* 3 pages: desc, commit, jsb */
    struct zj_metrics m;
    /* Per-filesystem scratch (all under fs->lock; never function-static,
     * so independent mounts can commit/replay/check concurrently). */
    uint64_t io_targets[ZJ_DESC_MAX+3U];
    uint8_t *io_datas[ZJ_DESC_MAX+3U];
    struct vfs_inode tmp_inode, tmp_dir, tmp_lf;
};
_Static_assert(ZJ_TXN_MAX<=ZJ_DESC_MAX, "txn fits descriptor");

#define ZJ_FS_PAGES ((sizeof(struct zj_fs) + 4095U) / 4096U)

static int zj_commit_locked(struct zj_fs *fs);
static void (*zj_txn_hook)(void);

/* ------------------------------------------------------------ helpers */

uint32_t zj_meta_csum(const void *block, uint64_t block_number) {
    return crc32c(block,4092)^(uint32_t)block_number;
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0]|(p[1]<<8));
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
}

static uint32_t zj_dir_csum(const uint8_t *block, uint64_t number) {
    uint32_t crc=crc32c(block,8);
    static const uint8_t zero[4]={0,0,0,0};
    crc=crc32c_update(crc,zero,4);
    crc=crc32c_update(crc,block+12,4096-12);
    return crc^(uint32_t)number;
}

static uint32_t zj_inode_csum(const struct zj_dinode *inode, uint64_t ino) {
    return crc32c(inode,252)^(uint32_t)ino;
}

static int zj_all_zero(const void *data, uint64_t length) {
    const uint8_t *p=(const uint8_t *)data;
    for (uint64_t i=0; i<length; ++i)
        if (p[i])
            return 0;
    return 1;
}

static int zj_io(struct zj_fs *fs, uint32_t op, uint64_t block, void *data) {
    int rc=block_rw(fs->device,op,block*fs->spb,data,ZJ_BLOCK_SIZE,BLOCK_PRIO_NORMAL);
    if (rc)
        ++fs->m.io_errors;
    return rc;
}

static int zj_flush(struct zj_fs *fs) {
    ++fs->m.journal_flushes;
    int rc=block_flush(fs->device,BLOCK_PRIO_NORMAL);
    if (rc)
        ++fs->m.io_errors;
    return rc;
}

/* Write n blocks asynchronously in bounded batches; returns first error. */
static int zj_write_batch(struct zj_fs *fs, const uint64_t *blocks,
                          uint8_t *const *datas, uint32_t n) {
    int first_error=0;
    for (uint32_t base=0; base<n; base+=ZJ_IO_BATCH) {
        struct block_request *requests[ZJ_IO_BATCH];
        uint32_t count=n-base<ZJ_IO_BATCH ? n-base : ZJ_IO_BATCH;
        for (uint32_t i=0; i<count; ++i) {
            struct block_request *r=block_request_alloc(fs->device,BLOCK_PRIO_NORMAL,1);
            requests[i]=r;
            r->op=BLOCK_OP_WRITE;
            r->lba=blocks[base+i]*fs->spb;
            (void)block_request_add_buffer(r,(uint64_t)datas[base+i],ZJ_BLOCK_SIZE);
            block_submit(fs->device,r);
        }
        for (uint32_t i=0; i<count; ++i) {
            int rc=block_wait(requests[i]);
            if (rc && !first_error)
                first_error=rc;
            block_request_free(requests[i]);
        }
    }
    if (first_error)
        ++fs->m.io_errors;
    return first_error;
}

static uint64_t zj_now(void) {
    return vfs_now_ns();
}

static void zj_record_error(struct zj_fs *fs, int code, uint64_t block) {
    ++fs->sb.error_count;
    fs->sb.last_error_code=(uint32_t)(-code);
    fs->sb.last_error_block=block;
    fs->sb.last_error_time=zj_now();
}

/* Abort: discard the running transaction, drop its buffers from the cache
 * (so readers see the last committed state), become read-only, and mark
 * the on-disk superblock ERROR (best effort, direct write) so the next
 * mount requires a check. */
static void zj_abort(struct zj_fs *fs, int code, const char *why) {
    if (fs->aborted)
        return;
    fs->aborted=1;
    ++fs->m.aborts;
    klog("ZEROOS: zjfs %s: aborting filesystem (%s, error %d); read-only until remount.",
         fs->device->name,why,code);
    for (uint32_t i=0; i<fs->txn_count; ++i) {
        struct zj_buf *b=fs->txn[i];
        b->flags&=~(ZB_TXN|ZB_VALID);
        if (b->refcount)
            --b->refcount;
    }
    fs->txn_count=0;
    fs->ordered_count=0;
    if (fs->vsb)
        fs->vsb->flags|=VFS_SB_ERROR|VFS_SB_RDONLY;
    zj_record_error(fs,code,0);
    /* Best effort: persist ERROR in the primary superblock. The in-memory
     * copy may carry uncommitted counters; re-read and patch the state. */
    uint8_t *page=(uint8_t *)page_alloc();
    if (page && zj_io(fs,BLOCK_OP_READ,0,page)==0) {
        struct zj_super *disk=(struct zj_super *)page;
        if (disk->magic==ZJ_MAGIC && disk->checksum==zj_meta_csum(disk,0)) {
            disk->state|=ZJ_STATE_ERROR;
            disk->error_count=fs->sb.error_count;
            disk->last_error_code=fs->sb.last_error_code;
            disk->last_error_time=fs->sb.last_error_time;
            disk->checksum=zj_meta_csum(disk,0);
            (void)zj_io(fs,BLOCK_OP_WRITE,0,page);
            (void)zj_flush(fs);
        }
    }
    if (page)
        page_free(page);
}

static int zj_writable(struct zj_fs *fs) {
    if (fs->aborted)
        return -SE_IO;
    if (fs->readonly)
        return -SE_ROFS;
    return 0;
}

/* -------------------------------------------------------- buffer cache */

static uint32_t zj_hash_of(uint64_t block) {
    return (uint32_t)((block*0x9e3779b97f4a7c15ULL)>>40)%ZJ_BUF_HASH;
}

static void zj_lru_remove(struct zj_fs *fs, struct zj_buf *b) {
    if (b->lru_prev) b->lru_prev->lru_next=b->lru_next; else fs->lru_head=b->lru_next;
    if (b->lru_next) b->lru_next->lru_prev=b->lru_prev; else fs->lru_tail=b->lru_prev;
    b->lru_prev=b->lru_next=0;
}

static void zj_lru_front(struct zj_fs *fs, struct zj_buf *b) {
    b->lru_prev=0;
    b->lru_next=fs->lru_head;
    if (fs->lru_head) fs->lru_head->lru_prev=b; else fs->lru_tail=b;
    fs->lru_head=b;
}

static void zj_hash_remove(struct zj_fs *fs, struct zj_buf *b) {
    struct zj_buf **cursor=&fs->hash[zj_hash_of(b->block)];
    while (*cursor && *cursor!=b)
        cursor=&(*cursor)->hash_next;
    if (*cursor)
        *cursor=b->hash_next;
    b->hash_next=0;
}

static struct zj_buf *zj_lookup(struct zj_fs *fs, uint64_t block) {
    for (struct zj_buf *b=fs->hash[zj_hash_of(block)]; b; b=b->hash_next)
        if (b->block==block && b->data)
            return b;
    return 0;
}

static int zj_verify(struct zj_fs *fs, struct zj_buf *b) {
    const uint8_t *d=b->data;
    switch (b->kind) {
    case ZK_META:
        return rd32(d+4092)==zj_meta_csum(d,b->block);
    case ZK_BITMAP:
        return rd32(d+4088)==ZJ_BITMAP_MAGIC && rd32(d+4092)==zj_meta_csum(d,b->block);
    case ZK_INDIRECT:
        return rd32(d+4088)==b->owner && rd32(d+4092)==zj_meta_csum(d,b->block);
    case ZK_DIR:
        return rd32(d)==ZJ_DIR_MAGIC && rd32(d+4)==b->owner &&
               rd32(d+8)==zj_dir_csum(d,b->block);
    default:
        (void)fs;
        return 1;       /* inode blocks: verified per inode */
    }
}

static void zj_seal(struct zj_buf *b) {
    uint8_t *d=b->data;
    switch (b->kind) {
    case ZK_META:
        wr32(d+4092,zj_meta_csum(d,b->block));
        break;
    case ZK_BITMAP:
        wr32(d+4088,ZJ_BITMAP_MAGIC);
        wr32(d+4092,zj_meta_csum(d,b->block));
        break;
    case ZK_INDIRECT:
        wr32(d+4088,b->owner);
        wr32(d+4092,zj_meta_csum(d,b->block));
        break;
    case ZK_DIR:
        wr32(d,ZJ_DIR_MAGIC);
        wr32(d+4,b->owner);
        wr32(d+8,zj_dir_csum(d,b->block));
        break;
    default:
        break;
    }
}

static struct zj_buf *zj_slot(struct zj_fs *fs) {
    for (uint32_t i=0; i<ZJ_BUF_MAX; ++i)
        if (!fs->bufs[i].data) {
            void *page=page_alloc();
            if (!page)
                break;
            fs->bufs[i].data=(uint8_t *)page;
            return &fs->bufs[i];
        }
    for (struct zj_buf *b=fs->lru_tail; b; b=b->lru_prev) {
        if (b->refcount || (b->flags&ZB_TXN))
            continue;
        zj_hash_remove(fs,b);
        zj_lru_remove(fs,b);
        ++fs->m.meta_evictions;
        return b;
    }
    return 0;
}

/* Get the buffer for `block`. read=1 loads and verifies it; read=0 returns
 * a buffer whose contents the caller will initialize completely. */
static int zj_bget(struct zj_fs *fs, uint64_t block, uint32_t kind, uint32_t owner,
                   int read, struct zj_buf **out) {
    if (block>=fs->sb.total_blocks) {
        zj_record_error(fs,-SE_UCLEAN,block);
        ++fs->m.checksum_errors;
        return -SE_UCLEAN;
    }
    struct zj_buf *b=zj_lookup(fs,block);
    if (b && (b->flags&ZB_VALID)) {
        if (read && (b->kind!=kind || (kind==ZK_DIR || kind==ZK_INDIRECT ? b->owner!=owner : 0))) {
            /* Same block reached as a different structure: corruption
             * (cross-linked metadata). */
            ++fs->m.checksum_errors;
            zj_record_error(fs,-SE_UCLEAN,block);
            return -SE_UCLEAN;
        }
        b->kind=kind;
        b->owner=owner;
        ++b->refcount;
        zj_lru_remove(fs,b);
        zj_lru_front(fs,b);
        ++fs->m.meta_hits;
        *out=b;
        return 0;
    }
    if (!b) {
        b=zj_slot(fs);
        if (!b)
            return -SE_NOMEM;
        b->block=block;
        b->flags=0;
        b->refcount=0;
        b->hash_next=fs->hash[zj_hash_of(block)];
        fs->hash[zj_hash_of(block)]=b;
        zj_lru_front(fs,b);
    }
    b->kind=kind;
    b->owner=owner;
    if (read) {
        ++fs->m.meta_misses;
        int rc=zj_io(fs,BLOCK_OP_READ,block,b->data);
        if (rc==0 && !zj_verify(fs,b)) {
            ++fs->m.checksum_errors;
            klog("ZEROOS: zjfs %s: metadata checksum/identity mismatch at block %llu (kind %u).",
                 fs->device->name,block,kind);
            rc=-SE_UCLEAN;
        }
        if (rc) {
            zj_record_error(fs,rc,block);
            /* Unhash; the slot stays on the LRU and is reused later. */
            zj_hash_remove(fs,b);
            b->flags=0;
            b->block=~0ULL;
            return rc;
        }
    } else {
        memset(b->data,0,ZJ_BLOCK_SIZE);
    }
    b->flags|=ZB_VALID;
    b->refcount=1;
    *out=b;
    return 0;
}

static void zj_bput(struct zj_buf *b) {
    if (b && b->refcount)
        --b->refcount;
}

/* Add a modified buffer to the running transaction. */
static void zj_bdirty(struct zj_fs *fs, struct zj_buf *b) {
    if (b->flags&ZB_TXN)
        return;
    if (fs->txn_count>=fs->txn_max) {
        /* Credits are reserved in zj_begin; reaching this is a bug. */
        klog("ZEROOS: zjfs %s: transaction credit overflow.",fs->device->name);
        zj_abort(fs,-SE_IO,"credit overflow");
        return;
    }
    if (fs->txn_count==0) {
        fs->txn_start_tick=timer_ticks();
        if (zj_txn_hook && !fs->checking)
            zj_txn_hook();
    }
    b->flags|=ZB_TXN;
    ++b->refcount;
    fs->txn[fs->txn_count++]=b;
}

/* Drop a non-transaction cached copy of a freed block. */
static void zj_forget(struct zj_fs *fs, uint64_t block) {
    struct zj_buf *b=zj_lookup(fs,block);
    if (b && !(b->flags&ZB_TXN) && !b->refcount)
        b->flags&=~ZB_VALID;
}

/* --------------------------------------------------------- pending set */

static int zj_pending_has(struct zj_fs *fs, uint32_t block) {
    uint32_t slot=(block*2654435761U)%ZJ_PENDING_SLOTS;
    for (uint32_t probe=0; probe<ZJ_PENDING_SLOTS; ++probe) {
        uint32_t v=fs->pending[(slot+probe)%ZJ_PENDING_SLOTS];
        if (v==0)
            return 0;
        if (v==block)
            return 1;
    }
    return 0;
}

static void zj_pending_add(struct zj_fs *fs, uint32_t block) {
    uint32_t slot=(block*2654435761U)%ZJ_PENDING_SLOTS;
    for (uint32_t probe=0; probe<ZJ_PENDING_SLOTS; ++probe) {
        uint32_t *v=&fs->pending[(slot+probe)%ZJ_PENDING_SLOTS];
        if (*v==block)
            return;
        if (*v==0) {
            *v=block;
            ++fs->pending_count;
            return;
        }
    }
}

/* -------------------------------------------------------- transactions */

static int zj_begin(struct zj_fs *fs, uint32_t credits) {
    int rc=zj_writable(fs);
    if (rc)
        return rc;
    credits+=1U;                                /* superblock */
    if (fs->txn_count+credits>fs->txn_max ||
        fs->ordered_count+2U>ZJ_ORDERED_MAX ||
        fs->pending_count+ZJ_TRUNC_CHUNK>ZJ_PENDING_CAP) {
        rc=zj_commit_locked(fs);
        if (rc)
            return rc;
    }
    return 0;
}

/* Ordered-data list. Entries hold no reference: an inode leaves the list
 * when it is evicted or destroyed (both under fs->lock, after its data was
 * written back or invalidated). */
static void zj_order_inode(struct zj_fs *fs, struct vfs_inode *inode) {
    for (uint32_t i=0; i<fs->ordered_count; ++i)
        if (fs->ordered[i]==inode)
            return;
    fs->ordered[fs->ordered_count++]=inode;
}

static void zj_unorder_inode(struct zj_fs *fs, struct vfs_inode *inode) {
    for (uint32_t i=0; i<fs->ordered_count; ++i)
        if (fs->ordered[i]==inode) {
            fs->ordered[i]=fs->ordered[--fs->ordered_count];
            return;
        }
}

static int zj_sb_to_txn(struct zj_fs *fs) {
    struct zj_buf *b;
    int rc=zj_bget(fs,0,ZK_META,0,0,&b);
    if (rc)
        return rc;
    fs->sb.last_write_time=zj_now();
    memcpy(b->data,&fs->sb,sizeof(fs->sb));
    zj_bdirty(fs,b);
    zj_bput(b);
    return 0;
}

static int zj_commit_locked(struct zj_fs *fs) {
    if (fs->aborted)
        return -SE_IO;
    if (fs->txn_count==0 && fs->ordered_count==0) {
        memset(fs->pending,0,ZJ_PENDING_SLOTS*sizeof(uint32_t));
        fs->pending_count=0;
        return 0;
    }
    /* 1. Ordered data. Errors are reported through the files' mappings
     * (fsync) and do not block metadata durability. */
    for (uint32_t i=0; i<fs->ordered_count; ++i) {
        (void)pc_writeback_mapping(&fs->ordered[i]->mapping,1,BLOCK_PRIO_NORMAL);
    }
    fs->ordered_count=0;
    if (fs->txn_count==0)
        return 0;
    int rc=zj_sb_to_txn(fs);
    if (rc) {
        zj_abort(fs,rc,"superblock buffer");
        return rc;
    }
    uint32_t n=fs->txn_count;
    uint64_t j0=fs->sb.journal_start;
    struct zj_jsb *jsb=(struct zj_jsb *)fs->scratch;
    struct zj_desc *desc=(struct zj_desc *)(fs->scratch+4096);
    struct zj_commit *commit=(struct zj_commit *)(fs->scratch+8192);
    memset(jsb,0,4096);
    jsb->magic=ZJ_JSB_MAGIC;
    jsb->version=1;
    jsb->sequence=fs->seq;
    jsb->blocks=fs->sb.journal_blocks;
    jsb->checksum=zj_meta_csum(jsb,j0);
    memset(desc,0,4096);
    desc->magic=ZJ_DESC_MAGIC;
    desc->count=n;
    desc->sequence=fs->seq;
    uint32_t data_crc=0;
    for (uint32_t i=0; i<n; ++i) {
        zj_seal(fs->txn[i]);
        desc->targets[i]=fs->txn[i]->block;
        data_crc=crc32c_update(data_crc,fs->txn[i]->data,ZJ_BLOCK_SIZE);
    }
    desc->checksum=zj_meta_csum(desc,j0+1U);
    memset(commit,0,4096);
    commit->magic=ZJ_COMMIT_MAGIC;
    commit->count=n;
    commit->sequence=fs->seq;
    commit->data_crc=data_crc;
    commit->commit_time_ns=zj_now();
    commit->checksum=zj_meta_csum(commit,j0+2U+n);

    /* 2+3. Journal write, then flush: durable after this point. */
    uint64_t *targets=fs->io_targets;
    uint8_t **datas=fs->io_datas;
    targets[0]=j0; datas[0]=(uint8_t *)jsb;
    targets[1]=j0+1U; datas[1]=(uint8_t *)desc;
    for (uint32_t i=0; i<n; ++i) {
        targets[2U+i]=j0+2U+i;
        datas[2U+i]=fs->txn[i]->data;
    }
    targets[2U+n]=j0+2U+n; datas[2U+n]=(uint8_t *)commit;
    rc=zj_write_batch(fs,targets,datas,n+3U);
    if (rc==0)
        rc=zj_flush(fs);
    if (rc) {
        zj_abort(fs,rc,"journal write");
        return rc;
    }
    /* 4. Checkpoint. A failure here leaves a committed journal that the
     * next mount replays; the filesystem still aborts. */
    for (uint32_t i=0; i<n; ++i) {
        targets[i]=fs->txn[i]->block;
        datas[i]=fs->txn[i]->data;
    }
    rc=zj_write_batch(fs,targets,datas,n);
    if (rc==0)
        rc=zj_flush(fs);
    if (rc) {
        zj_abort(fs,rc,"checkpoint");
        return rc;
    }
    for (uint32_t i=0; i<n; ++i) {
        fs->txn[i]->flags&=~ZB_TXN;
        zj_bput(fs->txn[i]);
    }
    ++fs->m.commits;
    fs->m.commit_blocks+=n;
    if (n>fs->m.max_txn_blocks)
        fs->m.max_txn_blocks=n;
    fs->txn_count=0;
    ++fs->seq;
    memset(fs->pending,0,ZJ_PENDING_SLOTS*sizeof(uint32_t));
    fs->pending_count=0;
    return 0;
}

/* Journal replay (mount/check). Returns 1 if a transaction was replayed,
 * 0 if none was pending, -SE_* on error. */
static int zj_replay(struct zj_fs *fs, int write_allowed) {
    uint64_t j0=fs->sb.journal_start;
    uint64_t jn=fs->sb.journal_blocks;
    uint8_t *page=fs->scratch;
    struct zj_jsb jsb;
    int rc=zj_io(fs,BLOCK_OP_READ,j0,page);
    if (rc)
        return rc;
    memcpy(&jsb,page,sizeof(jsb));
    if (jsb.magic!=ZJ_JSB_MAGIC || jsb.checksum!=zj_meta_csum(&jsb,j0) ||
        jsb.blocks!=jn) {
        klog("ZEROOS: zjfs %s: journal superblock invalid.",fs->device->name);
        return -SE_UCLEAN;
    }
    fs->seq=jsb.sequence;
    struct zj_desc *desc=(struct zj_desc *)(fs->scratch+4096);
    rc=zj_io(fs,BLOCK_OP_READ,j0+1U,desc);
    if (rc)
        return rc;
    uint32_t max=(uint32_t)(jn-3U<ZJ_DESC_MAX ? jn-3U : ZJ_DESC_MAX);
    if (desc->magic!=ZJ_DESC_MAGIC || desc->checksum!=zj_meta_csum(desc,j0+1U) ||
        desc->count==0 || desc->count>max || desc->sequence<jsb.sequence)
        return 0;
    uint32_t n=desc->count;
    uint64_t seq=desc->sequence;
    struct zj_commit *commit=(struct zj_commit *)(fs->scratch+8192);
    rc=zj_io(fs,BLOCK_OP_READ,j0+2U+n,commit);
    if (rc)
        return rc;
    if (commit->magic!=ZJ_COMMIT_MAGIC || commit->checksum!=zj_meta_csum(commit,j0+2U+n) ||
        commit->sequence!=seq || commit->count!=n)
        return 0;                               /* uncommitted: discard */
    /* Validate targets and the image CRC before touching home blocks. */
    for (uint32_t i=0; i<n; ++i) {
        uint64_t t=desc->targets[i];
        if (t>=fs->sb.total_blocks || (t>=j0 && t<j0+jn)) {
            klog("ZEROOS: zjfs %s: journal target %llu out of range; replay refused.",
                 fs->device->name,t);
            return -SE_UCLEAN;
        }
    }
    uint8_t *images=(uint8_t *)page_alloc_contiguous(n);
    if (!images)
        return -SE_NOMEM;
    uint32_t crc=0;
    for (uint32_t i=0; i<n && rc==0; ++i) {
        rc=zj_io(fs,BLOCK_OP_READ,j0+2U+i,images+(uint64_t)i*4096U);
        if (rc==0)
            crc=crc32c_update(crc,images+(uint64_t)i*4096U,4096);
    }
    if (rc==0 && crc!=commit->data_crc) {
        /* Torn journal write: the commit record reached the media but
         * some images did not. The transaction never became durable in
         * the protocol sense (no flush completed), so discard it. */
        page_free_contiguous(images,n);
        return 0;
    }
    if (rc==0 && !write_allowed) {
        page_free_contiguous(images,n);
        return -SE_ROFS;
    }
    uint64_t *targets=fs->io_targets;
    uint8_t **datas=fs->io_datas;
    for (uint32_t i=0; rc==0 && i<n; ++i) {
        targets[i]=desc->targets[i];
        datas[i]=images+(uint64_t)i*4096U;
    }
    if (rc==0)
        rc=zj_write_batch(fs,targets,datas,n);
    if (rc==0)
        rc=zj_flush(fs);
    page_free_contiguous(images,n);
    if (rc)
        return rc;
    /* Retire the transaction. */
    memset(page,0,4096);
    struct zj_jsb *out=(struct zj_jsb *)page;
    out->magic=ZJ_JSB_MAGIC;
    out->version=1;
    out->sequence=seq+1U;
    out->blocks=jn;
    out->checksum=zj_meta_csum(out,j0);
    rc=zj_io(fs,BLOCK_OP_WRITE,j0,page);
    if (rc==0)
        rc=zj_flush(fs);
    if (rc)
        return rc;
    fs->seq=seq+1U;
    ++fs->m.replayed_txns;
    fs->m.replayed_blocks+=n;
    klog("ZEROOS: zjfs %s: journal replayed transaction %llu (%u blocks).",
         fs->device->name,seq,n);
    return 1;
}

/* ---------------------------------------------------------- allocation */

static int zj_bitmap_alloc(struct zj_fs *fs, uint64_t start, uint64_t blocks,
                           uint64_t nbits, uint64_t *hint, int check_pending,
                           uint64_t *out) {
    uint64_t first=*hint<nbits ? *hint : 0;
    uint64_t hint_block=first/ZJ_BITS_PER_BITMAP;
    uint32_t hint_bit=(uint32_t)(first%ZJ_BITS_PER_BITMAP);
    /* Visit the hint block from the hint, every other block, then the
     * hint block again below the hint (wrap-around). */
    for (uint64_t visit=0; visit<=blocks; ++visit) {
        uint64_t bitmap=(hint_block+visit)%blocks;
        uint32_t from=visit==0 ? hint_bit : 0;
        uint32_t to=visit==blocks ? hint_bit : ZJ_BITS_PER_BITMAP;
        if (from>=to)
            continue;
        struct zj_buf *b;
        int rc=zj_bget(fs,start+bitmap,ZK_BITMAP,0,1,&b);
        if (rc)
            return rc;
        uint64_t base=bitmap*ZJ_BITS_PER_BITMAP;
        for (uint32_t bit=from; bit<to && base+bit<nbits; ++bit) {
            if (b->data[bit>>3]==0xffU && (bit&7U)==0) {
                bit+=7U;
                continue;
            }
            if (b->data[bit>>3]&(1U<<(bit&7U)))
                continue;
            if (check_pending && zj_pending_has(fs,(uint32_t)(base+bit)))
                continue;
            b->data[bit>>3]|=(uint8_t)(1U<<(bit&7U));
            zj_bdirty(fs,b);
            zj_bput(b);
            *out=base+bit;
            *hint=base+bit+1U;
            return 0;
        }
        zj_bput(b);
    }
    return -SE_NOSPC;
}

static int zj_bitmap_clear(struct zj_fs *fs, uint64_t start, uint64_t index,
                           uint64_t nbits) {
    if (index>=nbits)
        return -SE_UCLEAN;
    struct zj_buf *b;
    int rc=zj_bget(fs,start+index/ZJ_BITS_PER_BITMAP,ZK_BITMAP,0,1,&b);
    if (rc)
        return rc;
    uint32_t bit=(uint32_t)(index%ZJ_BITS_PER_BITMAP);
    if (!(b->data[bit>>3]&(1U<<(bit&7U)))) {
        zj_bput(b);
        klog("ZEROOS: zjfs %s: double free of %llu detected.",fs->device->name,index);
        zj_record_error(fs,-SE_UCLEAN,index);
        return -SE_UCLEAN;
    }
    b->data[bit>>3]&=(uint8_t)~(1U<<(bit&7U));
    zj_bdirty(fs,b);
    zj_bput(b);
    return 0;
}

static int zj_alloc_block(struct zj_fs *fs, uint64_t *out) {
    if (fs->sb.free_blocks==0) {
        ++fs->m.enospc;
        return -SE_NOSPC;
    }
    if (fs->block_hint<fs->sb.data_start)
        fs->block_hint=fs->sb.data_start;
    int rc=zj_bitmap_alloc(fs,fs->sb.block_bitmap_start,fs->sb.block_bitmap_blocks,
                           fs->sb.total_blocks,&fs->block_hint,1,out);
    if (rc==-SE_NOSPC)
        ++fs->m.enospc;
    if (rc)
        return rc;
    if (*out<fs->sb.data_start || *out>=fs->sb.total_blocks-1U) {
        klog("ZEROOS: zjfs %s: allocator returned metadata block %llu (bitmap corrupt).",
             fs->device->name,*out);
        return -SE_UCLEAN;
    }
    --fs->sb.free_blocks;
    ++fs->m.allocations;
    return 0;
}

static int zj_free_block(struct zj_fs *fs, uint64_t block) {
    if (block<fs->sb.data_start || block>=fs->sb.total_blocks-1U) {
        zj_record_error(fs,-SE_UCLEAN,block);
        return -SE_UCLEAN;
    }
    int rc=zj_bitmap_clear(fs,fs->sb.block_bitmap_start,block,fs->sb.total_blocks);
    if (rc)
        return rc;
    zj_pending_add(fs,(uint32_t)block);
    zj_forget(fs,block);
    ++fs->sb.free_blocks;
    ++fs->m.frees;
    return 0;
}

static int zj_alloc_inode(struct zj_fs *fs, uint64_t *out) {
    if (fs->sb.free_inodes==0) {
        ++fs->m.enospc;
        return -SE_NOSPC;
    }
    int rc=zj_bitmap_alloc(fs,fs->sb.inode_bitmap_start,fs->sb.inode_bitmap_blocks,
                           fs->sb.inode_count,&fs->inode_hint,0,out);
    if (rc)
        return rc;
    if (*out<=ZJ_LOSTFOUND_INO) {
        klog("ZEROOS: zjfs %s: reserved inode %llu free in bitmap (corrupt).",
             fs->device->name,*out);
        return -SE_UCLEAN;
    }
    --fs->sb.free_inodes;
    return 0;
}

/* -------------------------------------------------------------- inodes */

static void zj_inode_loc(struct zj_fs *fs, uint64_t ino, uint64_t *block,
                         uint32_t *offset) {
    *block=fs->sb.inode_table_start+ino/ZJ_INODES_PER_BLOCK;
    *offset=(uint32_t)(ino%ZJ_INODES_PER_BLOCK)*ZJ_INODE_SIZE;
}

/* Returns 0 with out->mode==0 for a free (all-zero) inode. */
static int zj_read_dinode(struct zj_fs *fs, uint64_t ino, struct zj_dinode *out) {
    if (ino==0 || ino>=fs->sb.inode_count)
        return -SE_UCLEAN;
    uint64_t block;
    uint32_t offset;
    zj_inode_loc(fs,ino,&block,&offset);
    struct zj_buf *b;
    int rc=zj_bget(fs,block,ZK_INODES,0,1,&b);
    if (rc)
        return rc;
    memcpy(out,b->data+offset,sizeof(*out));
    zj_bput(b);
    if (zj_all_zero(out,sizeof(*out)))
        return 0;
    if (out->checksum!=zj_inode_csum(out,ino)) {
        ++fs->m.checksum_errors;
        zj_record_error(fs,-SE_UCLEAN,block);
        klog("ZEROOS: zjfs %s: inode %llu checksum mismatch.",fs->device->name,ino);
        return -SE_UCLEAN;
    }
    return 0;
}

static int zj_write_dinode(struct zj_fs *fs, uint64_t ino, struct zj_dinode *in) {
    uint64_t block;
    uint32_t offset;
    zj_inode_loc(fs,ino,&block,&offset);
    struct zj_buf *b;
    int rc=zj_bget(fs,block,ZK_INODES,0,1,&b);
    if (rc)
        return rc;
    if (in)
        in->checksum=zj_inode_csum(in,ino);
    if (in)
        memcpy(b->data+offset,in,sizeof(*in));
    else
        memset(b->data+offset,0,ZJ_INODE_SIZE);
    zj_bdirty(fs,b);
    zj_bput(b);
    return 0;
}

static void zj_to_dinode(const struct vfs_inode *inode, struct zj_dinode *d) {
    memset(d,0,sizeof(*d));
    d->mode=(uint16_t)inode->mode;
    d->links=(uint16_t)inode->links;
    d->uid=inode->uid;
    d->gid=inode->gid;
    d->flags=inode->fs_flags;
    d->size=inode->size;
    d->blocks=inode->blocks;
    d->atime_ns=inode->atime_ns;
    d->mtime_ns=inode->mtime_ns;
    d->ctime_ns=inode->ctime_ns;
    d->crtime_ns=inode->crtime_ns;
    d->generation=inode->generation;
    d->orphan_next=inode->fs_orphan_next;
    for (uint32_t i=0; i<ZJ_DIRECT; ++i)
        d->direct[i]=inode->fs_direct[i];
    d->indirect=inode->fs_indirect;
    d->dindirect=inode->fs_dindirect;
    d->parent=inode->fs_parent;
}

static void zj_from_dinode(struct vfs_inode *inode, const struct zj_dinode *d) {
    inode->mode=d->mode;
    inode->links=d->links;
    inode->uid=d->uid;
    inode->gid=d->gid;
    inode->fs_flags=d->flags;
    inode->size=d->size;
    inode->blocks=d->blocks;
    inode->atime_ns=d->atime_ns;
    inode->mtime_ns=d->mtime_ns;
    inode->ctime_ns=d->ctime_ns;
    inode->crtime_ns=d->crtime_ns;
    inode->generation=d->generation;
    inode->fs_orphan_next=d->orphan_next;
    for (uint32_t i=0; i<ZJ_DIRECT; ++i)
        inode->fs_direct[i]=d->direct[i];
    inode->fs_indirect=d->indirect;
    inode->fs_dindirect=d->dindirect;
    inode->fs_parent=d->parent;
}

static int zj_write_inode(struct zj_fs *fs, struct vfs_inode *inode) {
    struct zj_dinode d;
    zj_to_dinode(inode,&d);
    return zj_write_dinode(fs,inode->ino,&d);
}

static int zj_valid_dinode(struct zj_fs *fs, const struct zj_dinode *d) {
    if (!VFS_S_ISREG(d->mode) && !VFS_S_ISDIR(d->mode))
        return 0;
    if (d->tindirect)
        return 0;
    if (d->size>ZJ_MAX_FILE_BLOCKS*ZJ_BLOCK_SIZE)
        return 0;
    if (d->blocks>fs->sb.total_blocks)
        return 0;
    return 1;
}

/* ----------------------------------------------------------- block map */

static int zj_block_ok(struct zj_fs *fs, uint64_t block) {
    return block>=fs->sb.data_start && block<fs->sb.total_blocks-1U;
}

/* Get or create the pointer slot at `*slot_block` (0 = none) as an
 * indirect block owned by `ino`. */
static int zj_get_indirect(struct zj_fs *fs, uint32_t *slot, uint64_t ino,
                           int create, struct zj_buf **out, int *created) {
    *created=0;
    if (*slot==0) {
        if (!create)
            return 1;                           /* hole */
        uint64_t block;
        int rc=zj_alloc_block(fs,&block);
        if (rc)
            return rc;
        rc=zj_bget(fs,block,ZK_INDIRECT,(uint32_t)ino,0,out);
        if (rc)
            return rc;
        zj_bdirty(fs,*out);
        *slot=(uint32_t)block;
        *created=1;
        return 0;
    }
    if (!zj_block_ok(fs,*slot)) {
        zj_record_error(fs,-SE_UCLEAN,*slot);
        ++fs->m.checksum_errors;
        return -SE_UCLEAN;
    }
    return zj_bget(fs,*slot,ZK_INDIRECT,(uint32_t)ino,1,out);
}

/* Map file block `index` of `inode`. create=1 allocates missing blocks.
 * Returns 0 with *block=0 for holes when create=0. Inode fields are
 * updated in memory; caller writes the inode. */
static int zj_bmap(struct zj_fs *fs, struct vfs_inode *inode, uint64_t index,
                   int create, uint64_t *block, int *is_new) {
    *block=0;
    *is_new=0;
    if (index>=ZJ_MAX_FILE_BLOCKS)
        return -SE_FBIG;
    uint32_t *slot;
    struct zj_buf *leaf=0, *mid=0;
    int created;
    int rc;
    if (index<ZJ_DIRECT) {
        slot=&inode->fs_direct[index];
    } else if (index<ZJ_DIRECT+ZJ_PTRS_PER_BLOCK) {
        rc=zj_get_indirect(fs,&inode->fs_indirect,inode->ino,create,&leaf,&created);
        if (rc==1)
            return 0;
        if (rc)
            return rc;
        if (created)
            ++inode->blocks;
        slot=(uint32_t *)(leaf->data+4U*(index-ZJ_DIRECT));
    } else {
        uint64_t rel=index-ZJ_DIRECT-ZJ_PTRS_PER_BLOCK;
        rc=zj_get_indirect(fs,&inode->fs_dindirect,inode->ino,create,&mid,&created);
        if (rc==1)
            return 0;
        if (rc)
            return rc;
        if (created)
            ++inode->blocks;
        uint32_t *mid_slot=(uint32_t *)(mid->data+4U*(rel/ZJ_PTRS_PER_BLOCK));
        uint32_t before=*mid_slot;
        rc=zj_get_indirect(fs,mid_slot,inode->ino,create,&leaf,&created);
        if (*mid_slot!=before)
            zj_bdirty(fs,mid);
        zj_bput(mid);
        if (rc==1)
            return 0;
        if (rc)
            return rc;
        if (created)
            ++inode->blocks;
        slot=(uint32_t *)(leaf->data+4U*(rel%ZJ_PTRS_PER_BLOCK));
    }
    if (*slot) {
        if (!zj_block_ok(fs,*slot)) {
            zj_bput(leaf);
            zj_record_error(fs,-SE_UCLEAN,*slot);
            ++fs->m.checksum_errors;
            return -SE_UCLEAN;
        }
        *block=*slot;
        zj_bput(leaf);
        return 0;
    }
    if (!create) {
        zj_bput(leaf);
        return 0;
    }
    uint64_t fresh;
    rc=zj_alloc_block(fs,&fresh);
    if (rc) {
        zj_bput(leaf);
        return rc;
    }
    *slot=(uint32_t)fresh;
    if (leaf)
        zj_bdirty(fs,leaf);
    zj_bput(leaf);
    ++inode->blocks;
    *block=fresh;
    *is_new=1;
    return 0;
}

/* Free blocks of `inode` with file index >= first, at most `budget`
 * blocks (data + indirect). Returns 0 when done, -SE_AGAIN when the budget
 * ran out (call again in a new transaction), or an error. */
static int zj_no_room(struct zj_fs *fs, const uint32_t *budget) {
    /* Stop a chunk when the budget is spent or the transaction is close
     * to its credit limit (each freed block may dirty a bitmap block). */
    return *budget==0 || fs->txn_count+8U>=fs->txn_max;
}

static int zj_free_range(struct zj_fs *fs, struct vfs_inode *inode, uint64_t first,
                         uint32_t *budget) {
    int rc;
    for (uint64_t i=first; i<ZJ_DIRECT; ++i) {
        if (!inode->fs_direct[i])
            continue;
        if (zj_no_room(fs,budget))
            return -SE_AGAIN;
        rc=zj_free_block(fs,inode->fs_direct[i]);
        if (rc)
            return rc;
        inode->fs_direct[i]=0;
        --inode->blocks;
        --*budget;
    }
    /* single indirect */
    if (inode->fs_indirect) {
        uint64_t start=first>ZJ_DIRECT ? first-ZJ_DIRECT : 0;
        if (start<ZJ_PTRS_PER_BLOCK) {
            struct zj_buf *b;
            rc=zj_bget(fs,inode->fs_indirect,ZK_INDIRECT,(uint32_t)inode->ino,1,&b);
            if (rc)
                return rc;
            uint32_t *e=(uint32_t *)b->data;
            for (uint64_t j=start; j<ZJ_PTRS_PER_BLOCK; ++j) {
                if (!e[j])
                    continue;
                if (zj_no_room(fs,budget)) {
                    zj_bput(b);
                    return -SE_AGAIN;
                }
                rc=zj_free_block(fs,e[j]);
                if (rc) {
                    zj_bput(b);
                    return rc;
                }
                e[j]=0;
                zj_bdirty(fs,b);
                --inode->blocks;
                --*budget;
            }
            zj_bput(b);
            if (start==0) {
                if (zj_no_room(fs,budget))
                    return -SE_AGAIN;
                rc=zj_free_block(fs,inode->fs_indirect);
                if (rc)
                    return rc;
                inode->fs_indirect=0;
                --inode->blocks;
                --*budget;
            }
        }
    }
    /* double indirect */
    if (inode->fs_dindirect) {
        uint64_t base=ZJ_DIRECT+ZJ_PTRS_PER_BLOCK;
        uint64_t start=first>base ? first-base : 0;
        struct zj_buf *mid;
        rc=zj_bget(fs,inode->fs_dindirect,ZK_INDIRECT,(uint32_t)inode->ino,1,&mid);
        if (rc)
            return rc;
        uint32_t *m=(uint32_t *)mid->data;
        for (uint64_t k=start/ZJ_PTRS_PER_BLOCK; k<ZJ_PTRS_PER_BLOCK; ++k) {
            if (!m[k])
                continue;
            uint64_t leaf_first=k*ZJ_PTRS_PER_BLOCK;
            uint64_t from=start>leaf_first ? start-leaf_first : 0;
            struct zj_buf *leaf;
            rc=zj_bget(fs,m[k],ZK_INDIRECT,(uint32_t)inode->ino,1,&leaf);
            if (rc) {
                zj_bput(mid);
                return rc;
            }
            uint32_t *e=(uint32_t *)leaf->data;
            for (uint64_t j=from; j<ZJ_PTRS_PER_BLOCK; ++j) {
                if (!e[j])
                    continue;
                if (zj_no_room(fs,budget)) {
                    zj_bput(leaf);
                    zj_bput(mid);
                    return -SE_AGAIN;
                }
                rc=zj_free_block(fs,e[j]);
                if (rc) {
                    zj_bput(leaf);
                    zj_bput(mid);
                    return rc;
                }
                e[j]=0;
                zj_bdirty(fs,leaf);
                --inode->blocks;
                --*budget;
            }
            zj_bput(leaf);
            if (from==0) {
                if (zj_no_room(fs,budget)) {
                    zj_bput(mid);
                    return -SE_AGAIN;
                }
                rc=zj_free_block(fs,m[k]);
                if (rc) {
                    zj_bput(mid);
                    return rc;
                }
                m[k]=0;
                zj_bdirty(fs,mid);
                --inode->blocks;
                --*budget;
            }
        }
        zj_bput(mid);
        if (start==0) {
            if (zj_no_room(fs,budget))
                return -SE_AGAIN;
            rc=zj_free_block(fs,inode->fs_dindirect);
            if (rc)
                return rc;
            inode->fs_dindirect=0;
            --inode->blocks;
            --*budget;
        }
    }
    return 0;
}

/* --------------------------------------------------------- directories */

#define ZJ_REC_ALIGN(n) (((n)+7U)&~7U)

struct zj_dirloc {
    uint64_t index;
    uint32_t offset;
    uint32_t prev;             /* offset of previous record, 0 = none */
};

static int zj_dir_get(struct zj_fs *fs, struct vfs_inode *dir, uint64_t index,
                      struct zj_buf **out) {
    uint64_t block;
    int is_new;
    int rc=zj_bmap(fs,dir,index,0,&block,&is_new);
    if (rc)
        return rc;
    if (block==0) {
        zj_record_error(fs,-SE_UCLEAN,0);
        klog("ZEROOS: zjfs %s: directory %llu has a hole at block %llu.",
             fs->device->name,dir->ino,index);
        return -SE_UCLEAN;
    }
    return zj_bget(fs,block,ZK_DIR,(uint32_t)dir->ino,1,out);
}

/* Validate the record at `offset`; returns its rec_len or 0 if corrupt. */
static uint32_t zj_rec_valid(struct zj_fs *fs, const uint8_t *d, uint32_t offset) {
    uint32_t rec_len=rd16(d+offset+4);
    uint32_t name_len=d[offset+6];
    uint32_t ino=rd32(d+offset);
    if (rec_len<ZJ_DIRENT_HEADER+8U || (rec_len&7U) || offset+rec_len>ZJ_BLOCK_SIZE ||
        (ino && (name_len==0 || ZJ_DIRENT_HEADER+name_len>rec_len)) ||
        ino>=fs->sb.inode_count)
        return 0;
    return rec_len;
}

static int zj_dir_corrupt(struct zj_fs *fs, struct zj_buf *b) {
    ++fs->m.checksum_errors;
    zj_record_error(fs,-SE_UCLEAN,b->block);
    klog("ZEROOS: zjfs %s: malformed directory block %llu.",fs->device->name,b->block);
    zj_bput(b);
    return -SE_UCLEAN;
}

static int zj_dir_find(struct zj_fs *fs, struct vfs_inode *dir, const char *name,
                       uint32_t length, struct zj_dirloc *loc, uint64_t *ino,
                       uint32_t *type) {
    uint64_t blocks=dir->size/ZJ_BLOCK_SIZE;
    for (uint64_t index=0; index<blocks; ++index) {
        struct zj_buf *b;
        int rc=zj_dir_get(fs,dir,index,&b);
        if (rc)
            return rc;
        uint32_t prev=0;
        for (uint32_t off=ZJ_DIR_HEADER; off<ZJ_BLOCK_SIZE;) {
            uint32_t rec_len=zj_rec_valid(fs,b->data,off);
            if (!rec_len)
                return zj_dir_corrupt(fs,b);
            uint32_t rino=rd32(b->data+off);
            if (rino && b->data[off+6]==length &&
                memcmp(b->data+off+8,name,length)==0) {
                if (loc) {
                    loc->index=index;
                    loc->offset=off;
                    loc->prev=prev;
                }
                *ino=rino;
                if (type)
                    *type=b->data[off+7];
                zj_bput(b);
                return 0;
            }
            prev=off;
            off+=rec_len;
        }
        zj_bput(b);
    }
    return -SE_NOENT;
}

static void zj_rec_fill(uint8_t *d, uint32_t off, uint64_t ino, uint32_t rec_len,
                        const char *name, uint32_t length, uint32_t type) {
    wr32(d+off,(uint32_t)ino);
    wr16(d+off+4,(uint16_t)rec_len);
    d[off+6]=(uint8_t)length;
    d[off+7]=(uint8_t)type;
    memcpy(d+off+8,name,length);
    for (uint32_t i=8U+length; i<rec_len; ++i)
        d[off+i]=0;
}

static int zj_dir_new_block(struct zj_fs *fs, struct vfs_inode *dir,
                            struct zj_buf **out) {
    uint64_t index=dir->size/ZJ_BLOCK_SIZE;
    uint64_t block;
    int is_new;
    int rc=zj_bmap(fs,dir,index,1,&block,&is_new);
    if (rc)
        return rc;
    rc=zj_bget(fs,block,ZK_DIR,(uint32_t)dir->ino,0,out);
    if (rc)
        return rc;
    uint8_t *d=(*out)->data;
    wr32(d,ZJ_DIR_MAGIC);
    wr32(d+4,(uint32_t)dir->ino);
    wr32(d+ZJ_DIR_HEADER,0);
    wr16(d+ZJ_DIR_HEADER+4,(uint16_t)(ZJ_BLOCK_SIZE-ZJ_DIR_HEADER));
    zj_bdirty(fs,*out);
    dir->size+=ZJ_BLOCK_SIZE;
    return 0;
}

static int zj_dir_add(struct zj_fs *fs, struct vfs_inode *dir, const char *name,
                      uint32_t length, uint64_t ino, uint32_t type) {
    uint32_t need=ZJ_REC_ALIGN(ZJ_DIRENT_HEADER+length);
    uint64_t blocks=dir->size/ZJ_BLOCK_SIZE;
    for (uint64_t index=0; index<blocks; ++index) {
        struct zj_buf *b;
        int rc=zj_dir_get(fs,dir,index,&b);
        if (rc)
            return rc;
        for (uint32_t off=ZJ_DIR_HEADER; off<ZJ_BLOCK_SIZE;) {
            uint32_t rec_len=zj_rec_valid(fs,b->data,off);
            if (!rec_len)
                return zj_dir_corrupt(fs,b);
            uint32_t rino=rd32(b->data+off);
            if (!rino && rec_len>=need) {
                zj_rec_fill(b->data,off,ino,rec_len,name,length,type);
                zj_bdirty(fs,b);
                zj_bput(b);
                return 0;
            }
            if (rino) {
                uint32_t used=ZJ_REC_ALIGN(ZJ_DIRENT_HEADER+b->data[off+6]);
                if (rec_len-used>=need && rec_len-used>=ZJ_DIRENT_HEADER+8U) {
                    wr16(b->data+off+4,(uint16_t)used);
                    zj_rec_fill(b->data,off+used,ino,rec_len-used,name,length,type);
                    zj_bdirty(fs,b);
                    zj_bput(b);
                    return 0;
                }
            }
            off+=rec_len;
        }
        zj_bput(b);
    }
    struct zj_buf *b;
    int rc=zj_dir_new_block(fs,dir,&b);
    if (rc)
        return rc;
    zj_rec_fill(b->data,ZJ_DIR_HEADER,ino,ZJ_BLOCK_SIZE-ZJ_DIR_HEADER,name,length,type);
    zj_bput(b);
    return 0;
}

static int zj_dir_remove(struct zj_fs *fs, struct vfs_inode *dir,
                         const struct zj_dirloc *loc) {
    struct zj_buf *b;
    int rc=zj_dir_get(fs,dir,loc->index,&b);
    if (rc)
        return rc;
    if (loc->prev) {
        uint32_t prev_len=rd16(b->data+loc->prev+4);
        uint32_t len=rd16(b->data+loc->offset+4);
        wr16(b->data+loc->prev+4,(uint16_t)(prev_len+len));
        memset(b->data+loc->offset,0,len);
    } else {
        wr32(b->data+loc->offset,0);
        b->data[loc->offset+6]=0;
        b->data[loc->offset+7]=0;
    }
    zj_bdirty(fs,b);
    zj_bput(b);
    return 0;
}

/* Replace the target inode of an existing entry in place (atomic rename
 * over an existing name). */
static int zj_dir_retarget(struct zj_fs *fs, struct vfs_inode *dir,
                           const struct zj_dirloc *loc, uint64_t ino, uint32_t type) {
    struct zj_buf *b;
    int rc=zj_dir_get(fs,dir,loc->index,&b);
    if (rc)
        return rc;
    wr32(b->data+loc->offset,(uint32_t)ino);
    b->data[loc->offset+7]=(uint8_t)type;
    zj_bdirty(fs,b);
    zj_bput(b);
    return 0;
}

static int zj_dir_is_empty(struct zj_fs *fs, struct vfs_inode *dir) {
    uint64_t blocks=dir->size/ZJ_BLOCK_SIZE;
    for (uint64_t index=0; index<blocks; ++index) {
        struct zj_buf *b;
        int rc=zj_dir_get(fs,dir,index,&b);
        if (rc)
            return rc;
        for (uint32_t off=ZJ_DIR_HEADER; off<ZJ_BLOCK_SIZE;) {
            uint32_t rec_len=zj_rec_valid(fs,b->data,off);
            if (!rec_len)
                return zj_dir_corrupt(fs,b);
            if (rd32(b->data+off)) {
                zj_bput(b);
                return 0;
            }
            off+=rec_len;
        }
        zj_bput(b);
    }
    return 1;
}

/* -------------------------------------------------------------- orphans */

static int zj_orphan_add(struct zj_fs *fs, struct vfs_inode *inode) {
    if (inode->fs_flags&ZJ_IF_ORPHAN)
        return 0;
    inode->fs_flags|=ZJ_IF_ORPHAN;
    inode->fs_orphan_next=(uint32_t)fs->sb.orphan_head;
    fs->sb.orphan_head=inode->ino;
    return zj_write_inode(fs,inode);
}

static int zj_orphan_remove(struct zj_fs *fs, struct vfs_inode *inode) {
    if (!(inode->fs_flags&ZJ_IF_ORPHAN))
        return 0;
    uint32_t next=inode->fs_orphan_next;
    inode->fs_flags&=~ZJ_IF_ORPHAN;
    inode->fs_orphan_next=0;
    if (fs->sb.orphan_head==inode->ino) {
        fs->sb.orphan_head=next;
        return zj_write_inode(fs,inode);
    }
    uint64_t cursor=fs->sb.orphan_head;
    for (uint64_t guard=0; cursor && guard<fs->sb.inode_count; ++guard) {
        struct zj_dinode d;
        int rc=zj_read_dinode(fs,cursor,&d);
        if (rc)
            return rc;
        if (d.orphan_next==inode->ino) {
            d.orphan_next=next;
            rc=zj_write_dinode(fs,cursor,&d);
            if (rc)
                return rc;
            /* Keep a cached in-memory copy coherent. */
            if (fs->vsb && !fs->checking) {
                struct vfs_inode *cached=vfs_inode_cached(fs->vsb,cursor);
                if (cached) {
                    cached->fs_orphan_next=next;
                    vfs_inode_put(cached);
                }
            }
            return zj_write_inode(fs,inode);
        }
        cursor=d.orphan_next;
    }
    klog("ZEROOS: zjfs %s: inode %llu missing from orphan list.",fs->device->name,
         inode->ino);
    zj_record_error(fs,-SE_UCLEAN,0);
    return zj_write_inode(fs,inode);
}

/* Free all blocks >= first in bounded chunks (each its own transaction
 * step). Caller holds fs->lock and has made the operation crash-safe
 * (orphan list) if more than one chunk may be needed. */
static int zj_truncate_blocks(struct zj_fs *fs, struct vfs_inode *inode, uint64_t first) {
    for (;;) {
        int rc=zj_begin(fs,72);
        if (rc)
            return rc;
        uint32_t budget=ZJ_TRUNC_CHUNK;
        rc=zj_free_range(fs,inode,first,&budget);
        int wrc=zj_write_inode(fs,inode);
        if (rc==-SE_AGAIN && wrc==0)
            continue;
        if (rc==0)
            rc=wrc;
        if (rc) {
            zj_abort(fs,rc,"truncate");
            return rc;
        }
        return 0;
    }
}

/* Destroy an unlinked inode: free its blocks and the inode itself. */
static int zj_destroy(struct zj_fs *fs, struct vfs_inode *inode) {
    int rc=zj_truncate_blocks(fs,inode,0);
    if (rc)
        return rc;
    rc=zj_begin(fs,6);
    if (rc)
        return rc;
    rc=zj_orphan_remove(fs,inode);
    if (rc==0)
        rc=zj_bitmap_clear(fs,fs->sb.inode_bitmap_start,inode->ino,fs->sb.inode_count);
    if (rc==0)
        rc=zj_write_dinode(fs,inode->ino,0);
    if (rc) {
        zj_abort(fs,rc,"inode destroy");
        return rc;
    }
    ++fs->sb.free_inodes;
    inode->flags|=VFS_I_DEAD;
    return 0;
}

/* ------------------------------------------------------ VFS operations */

static struct zj_fs *zj_of(struct vfs_inode *inode) {
    return (struct zj_fs *)inode->sb->fs;
}

static int zjop_lookup(struct vfs_inode *dir, const char *name, uint32_t length,
                       uint64_t *ino) {
    struct zj_fs *fs=zj_of(dir);
    kmutex_lock(&fs->lock);
    int rc=zj_dir_find(fs,dir,name,length,0,ino,0);
    kmutex_unlock(&fs->lock);
    return rc;
}

static int zjop_read_inode(struct vfs_superblock *sb, uint64_t ino,
                           struct vfs_inode *inode) {
    struct zj_fs *fs=(struct zj_fs *)sb->fs;
    struct zj_dinode d;
    kmutex_lock(&fs->lock);
    int rc=zj_read_dinode(fs,ino,&d);
    kmutex_unlock(&fs->lock);
    if (rc)
        return rc;
    if (d.mode==0)
        return -SE_STALE;           /* entry points to a free inode */
    if (!zj_valid_dinode(fs,&d)) {
        klog("ZEROOS: zjfs %s: inode %llu has invalid fields.",fs->device->name,ino);
        return -SE_UCLEAN;
    }
    zj_from_dinode(inode,&d);
    return 0;
}

static int zjop_create(struct vfs_inode *dir, const char *name, uint32_t length,
                       uint32_t mode, uint32_t uid, uint32_t gid, uint64_t *ino_out) {
    struct zj_fs *fs=zj_of(dir);
    int directory=VFS_S_ISDIR(mode);
    kmutex_lock(&fs->lock);
    int rc=zj_begin(fs,14);
    uint64_t existing;
    if (rc==0 && zj_dir_find(fs,dir,name,length,0,&existing,0)==0)
        rc=-SE_EXIST;
    if (rc==0 && directory && dir->links>=0xfffeU)
        rc=-SE_MLINK;
    /* Pre-check resources so nothing below can fail with ENOSPC. */
    if (rc==0 && (fs->sb.free_inodes<1U || fs->sb.free_blocks<5U)) {
        ++fs->m.enospc;
        rc=-SE_NOSPC;
    }
    uint64_t ino=0;
    if (rc==0)
        rc=zj_alloc_inode(fs,&ino);
    if (rc) {
        kmutex_unlock(&fs->lock);
        return rc;
    }
    struct vfs_inode fresh;
    memset(&fresh,0,sizeof(fresh));
    fresh.ino=ino;
    fresh.mode=mode;
    fresh.links=directory ? 2U : 1U;
    fresh.uid=uid;
    fresh.gid=gid;
    uint64_t now=zj_now();
    fresh.atime_ns=fresh.mtime_ns=fresh.ctime_ns=fresh.crtime_ns=now;
    fresh.generation=(uint32_t)++fs->sb.generation_counter;
    fresh.fs_parent=directory ? (uint32_t)dir->ino : 0;
    if (directory) {
        struct zj_buf *b;
        rc=zj_dir_new_block(fs,&fresh,&b);
        if (rc==0)
            zj_bput(b);
    }
    if (rc==0)
        rc=zj_write_inode(fs,&fresh);
    if (rc==0)
        rc=zj_dir_add(fs,dir,name,length,ino,directory ? ZJ_FT_DIR : ZJ_FT_REG);
    if (rc==0) {
        if (directory)
            ++dir->links;
        dir->mtime_ns=dir->ctime_ns=now;
        rc=zj_write_inode(fs,dir);
    }
    if (rc)
        zj_abort(fs,rc,"create");
    kmutex_unlock(&fs->lock);
    if (rc==0)
        *ino_out=ino;
    return rc;
}

static int zjop_unlink(struct vfs_inode *dir, const char *name, uint32_t length,
                       struct vfs_inode *victim, int directory) {
    struct zj_fs *fs=zj_of(dir);
    kmutex_lock(&fs->lock);
    struct zj_dirloc loc;
    uint64_t ino;
    int rc=zj_begin(fs,8);
    if (rc==0)
        rc=zj_dir_find(fs,dir,name,length,&loc,&ino,0);
    if (rc==0 && ino!=victim->ino)
        rc=-SE_STALE;
    if (rc==0 && directory) {
        int empty=zj_dir_is_empty(fs,victim);
        rc=empty<0 ? empty : (empty ? 0 : -SE_NOTEMPTY);
    }
    if (rc) {
        kmutex_unlock(&fs->lock);
        return rc;
    }
    uint64_t now=zj_now();
    rc=zj_dir_remove(fs,dir,&loc);
    if (rc==0) {
        if (directory) {
            victim->links=0;
            --dir->links;
        } else {
            --victim->links;
        }
        victim->ctime_ns=now;
        dir->mtime_ns=dir->ctime_ns=now;
        if (victim->links==0)
            rc=zj_orphan_add(fs,victim);      /* destroyed on last release */
        else
            rc=zj_write_inode(fs,victim);
    }
    if (rc==0)
        rc=zj_write_inode(fs,dir);
    if (rc)
        zj_abort(fs,rc,"unlink");
    kmutex_unlock(&fs->lock);
    return rc;
}

static int zjop_link(struct vfs_inode *dir, const char *name, uint32_t length,
                     struct vfs_inode *inode) {
    struct zj_fs *fs=zj_of(dir);
    kmutex_lock(&fs->lock);
    uint64_t existing;
    int rc=zj_begin(fs,10);
    if (rc==0 && zj_dir_find(fs,dir,name,length,0,&existing,0)==0)
        rc=-SE_EXIST;
    if (rc==0 && inode->links>=0xffffU)
        rc=-SE_MLINK;
    if (rc==0 && inode->links==0)
        rc=-SE_NOENT;
    if (rc==0 && fs->sb.free_blocks<4U) {
        ++fs->m.enospc;
        rc=-SE_NOSPC;
    }
    if (rc) {
        kmutex_unlock(&fs->lock);
        return rc;
    }
    uint64_t now=zj_now();
    rc=zj_dir_add(fs,dir,name,length,inode->ino,ZJ_FT_REG);
    if (rc==0) {
        ++inode->links;
        inode->ctime_ns=now;
        rc=zj_write_inode(fs,inode);
    }
    if (rc==0) {
        dir->mtime_ns=dir->ctime_ns=now;
        rc=zj_write_inode(fs,dir);
    }
    if (rc)
        zj_abort(fs,rc,"link");
    kmutex_unlock(&fs->lock);
    return rc;
}

static int zjop_rename(struct vfs_inode *old_dir, const char *old_name, uint32_t old_len,
                       struct vfs_inode *new_dir, const char *new_name, uint32_t new_len,
                       struct vfs_inode *moved, struct vfs_inode *replaced) {
    struct zj_fs *fs=zj_of(old_dir);
    int directory=VFS_S_ISDIR(moved->mode);
    kmutex_lock(&fs->lock);
    struct zj_dirloc old_loc, new_loc;
    uint64_t ino;
    int rc=zj_begin(fs,20);
    if (rc==0)
        rc=zj_dir_find(fs,old_dir,old_name,old_len,&old_loc,&ino,0);
    if (rc==0 && ino!=moved->ino)
        rc=-SE_STALE;
    int have_new=0;
    if (rc==0) {
        uint64_t target;
        int frc=zj_dir_find(fs,new_dir,new_name,new_len,&new_loc,&target,0);
        if (frc==0) {
            have_new=1;
            if (!replaced || target!=replaced->ino)
                rc=-SE_STALE;
        } else if (frc!=-SE_NOENT) {
            rc=frc;
        } else if (replaced) {
            rc=-SE_STALE;
        }
    }
    if (rc==0 && replaced && VFS_S_ISDIR(replaced->mode)) {
        int empty=zj_dir_is_empty(fs,replaced);
        rc=empty<0 ? empty : (empty ? 0 : -SE_NOTEMPTY);
    }
    if (rc==0 && !have_new && fs->sb.free_blocks<4U) {
        ++fs->m.enospc;
        rc=-SE_NOSPC;
    }
    if (rc==0 && directory && old_dir!=new_dir && !replaced && new_dir->links>=0xfffeU)
        rc=-SE_MLINK;
    if (rc) {
        kmutex_unlock(&fs->lock);
        return rc;
    }
    uint64_t now=zj_now();
    uint32_t type=directory ? ZJ_FT_DIR : ZJ_FT_REG;
    if (have_new)
        rc=zj_dir_retarget(fs,new_dir,&new_loc,moved->ino,type);
    else
        rc=zj_dir_add(fs,new_dir,new_name,new_len,moved->ino,type);
    /* Re-locate the old entry: adding may have split a record in the
     * same block, which never moves existing records, so old_loc stays
     * valid except for the prev link, which zj_dir_remove re-reads. */
    if (rc==0)
        rc=zj_dir_find(fs,old_dir,old_name,old_len,&old_loc,&ino,0);
    if (rc==0)
        rc=zj_dir_remove(fs,old_dir,&old_loc);
    if (rc==0 && replaced) {
        if (VFS_S_ISDIR(replaced->mode)) {
            replaced->links=0;
            --new_dir->links;
        } else {
            --replaced->links;
        }
        replaced->ctime_ns=now;
        rc=replaced->links==0 ? zj_orphan_add(fs,replaced) : zj_write_inode(fs,replaced);
    }
    if (rc==0 && directory && old_dir!=new_dir) {
        moved->fs_parent=(uint32_t)new_dir->ino;
        --old_dir->links;
        ++new_dir->links;
    }
    if (rc==0) {
        moved->ctime_ns=now;
        rc=zj_write_inode(fs,moved);
    }
    if (rc==0) {
        old_dir->mtime_ns=old_dir->ctime_ns=now;
        rc=zj_write_inode(fs,old_dir);
    }
    if (rc==0 && new_dir!=old_dir) {
        new_dir->mtime_ns=new_dir->ctime_ns=now;
        rc=zj_write_inode(fs,new_dir);
    }
    if (rc)
        zj_abort(fs,rc,"rename");
    kmutex_unlock(&fs->lock);
    return rc;
}

/* Cookie: 0 ".", 1 "..", then 2 + block*4096 + offset of the next record. */
static int zjop_readdir(struct vfs_inode *dir, uint64_t *cookie, struct vfs_dirent *out) {
    struct zj_fs *fs=zj_of(dir);
    if (*cookie<2U) {
        out->ino=*cookie==0 ? dir->ino : (dir->fs_parent ? dir->fs_parent : dir->ino);
        out->type=VFS_S_IFDIR>>12;
        out->name_len=(uint32_t)(*cookie+1U);
        out->name[0]='.';
        out->name[1]=*cookie ? '.' : 0;
        out->name[out->name_len]=0;
        ++*cookie;
        return 1;
    }
    kmutex_lock(&fs->lock);
    uint64_t position=*cookie-2U;
    uint64_t index=position/ZJ_BLOCK_SIZE;
    uint32_t want=(uint32_t)(position%ZJ_BLOCK_SIZE);
    uint64_t blocks=dir->size/ZJ_BLOCK_SIZE;
    for (; index<blocks; ++index, want=0) {
        struct zj_buf *b;
        int rc=zj_dir_get(fs,dir,index,&b);
        if (rc) {
            kmutex_unlock(&fs->lock);
            return rc;
        }
        for (uint32_t off=ZJ_DIR_HEADER; off<ZJ_BLOCK_SIZE;) {
            uint32_t rec_len=zj_rec_valid(fs,b->data,off);
            if (!rec_len) {
                rc=zj_dir_corrupt(fs,b);
                kmutex_unlock(&fs->lock);
                return rc;
            }
            uint32_t rino=rd32(b->data+off);
            if (off>=want && rino) {
                out->ino=rino;
                out->type=b->data[off+7]==ZJ_FT_DIR ? (VFS_S_IFDIR>>12) : (VFS_S_IFREG>>12);
                out->name_len=b->data[off+6];
                memcpy(out->name,b->data+off+8,out->name_len);
                out->name[out->name_len]=0;
                *cookie=2U+index*ZJ_BLOCK_SIZE+off+rec_len;
                zj_bput(b);
                kmutex_unlock(&fs->lock);
                return 1;
            }
            off+=rec_len;
        }
        zj_bput(b);
    }
    *cookie=2U+blocks*ZJ_BLOCK_SIZE;
    kmutex_unlock(&fs->lock);
    return 0;
}

static int zjop_map_page(struct vfs_inode *inode, uint64_t index, int create,
                         uint64_t *block_out, int *new_out) {
    struct zj_fs *fs=zj_of(inode);
    kmutex_lock(&fs->lock);
    int rc=0;
    if (create) {
        rc=zj_begin(fs,10);
        if (rc==0 && fs->sb.free_blocks<3U) {
            /* Might still succeed if the block exists; check first. */
            uint64_t block;
            int is_new;
            rc=zj_bmap(fs,inode,index,0,&block,&is_new);
            if (rc==0 && block) {
                *block_out=block;
                *new_out=0;
                kmutex_unlock(&fs->lock);
                return 0;
            }
            if (rc==0) {
                ++fs->m.enospc;
                rc=-SE_NOSPC;
            }
        }
    }
    if (rc) {
        kmutex_unlock(&fs->lock);
        return rc;
    }
    uint64_t block;
    int is_new;
    rc=zj_bmap(fs,inode,index,create,&block,&is_new);
    if (rc==0 && is_new) {
        /* Zero-filled, dirty page before the allocation can commit: the
         * ordered commit writes it, so no stale block is ever exposed. */
        struct pc_page *page;
        rc=pc_get(&inode->mapping,index,block,0,BLOCK_PRIO_NORMAL,&page);
        if (rc==0) {
            pc_mark_dirty(page,block);
            pc_put(page);
            zj_order_inode(fs,inode);
            rc=zj_write_inode(fs,inode);
        }
        if (rc)
            zj_abort(fs,rc,"block allocation");
    }
    kmutex_unlock(&fs->lock);
    if (rc==0) {
        *block_out=block;
        *new_out=is_new;
    }
    return rc;
}

static int zjop_set_size(struct vfs_inode *inode, uint64_t size) {
    struct zj_fs *fs=zj_of(inode);
    if (size>ZJ_MAX_FILE_BLOCKS*ZJ_BLOCK_SIZE)
        return -SE_FBIG;
    kmutex_lock(&fs->lock);
    int rc=zj_writable(fs);
    if (rc) {
        kmutex_unlock(&fs->lock);
        return rc;
    }
    uint64_t now=zj_now();
    if (size>=inode->size) {
        rc=zj_begin(fs,2);
        if (rc==0) {
            inode->size=size;
            inode->mtime_ns=inode->ctime_ns=now;
            rc=zj_write_inode(fs,inode);
        }
        kmutex_unlock(&fs->lock);
        return rc;
    }
    uint64_t first=(size+ZJ_BLOCK_SIZE-1U)/ZJ_BLOCK_SIZE;
    rc=pc_invalidate(&inode->mapping,first);
    if (rc) {
        kmutex_unlock(&fs->lock);
        return rc;                              /* pinned by mmap */
    }
    /* Zero the tail of a partial last page so a later extension reads
     * zeros (the block stays allocated). */
    if (size%ZJ_BLOCK_SIZE) {
        struct pc_page *page=pc_find(&inode->mapping,size/ZJ_BLOCK_SIZE);
        if (page) {
            memset(pc_data(page)+size%ZJ_BLOCK_SIZE,0,ZJ_BLOCK_SIZE-size%ZJ_BLOCK_SIZE);
            if (page->block)
                pc_mark_dirty(page,0);
            pc_put(page);
        } else {
            uint64_t block;
            int is_new;
            rc=zj_bmap(fs,inode,size/ZJ_BLOCK_SIZE,0,&block,&is_new);
            if (rc==0 && block) {
                rc=pc_get(&inode->mapping,size/ZJ_BLOCK_SIZE,block,1,BLOCK_PRIO_NORMAL,&page);
                if (rc==0) {
                    memset(pc_data(page)+size%ZJ_BLOCK_SIZE,0,
                           ZJ_BLOCK_SIZE-size%ZJ_BLOCK_SIZE);
                    pc_mark_dirty(page,block);
                    zj_order_inode(fs,inode);
                    pc_put(page);
                }
            }
            if (rc) {
                kmutex_unlock(&fs->lock);
                return rc;
            }
        }
    }
    /* Step 1 (atomic): new size + TRUNC flag + orphan list membership, so
     * a crash between chunks resumes the truncate at mount. */
    rc=zj_begin(fs,4);
    if (rc==0) {
        inode->size=size;
        inode->mtime_ns=inode->ctime_ns=now;
        inode->fs_flags|=ZJ_IF_TRUNC;
        rc=zj_orphan_add(fs,inode);
    }
    if (rc==0)
        rc=zj_truncate_blocks(fs,inode,first);
    if (rc==0) {
        rc=zj_begin(fs,4);
        if (rc==0 && inode->links) {
            inode->fs_flags&=~ZJ_IF_TRUNC;
            rc=zj_orphan_remove(fs,inode);
        } else if (rc==0) {
            inode->fs_flags&=~ZJ_IF_TRUNC;
            rc=zj_write_inode(fs,inode);        /* stays orphan: unlinked */
        }
    }
    if (rc && !fs->aborted)
        zj_abort(fs,rc,"truncate");
    kmutex_unlock(&fs->lock);
    return rc;
}

static int zjop_update_inode(struct vfs_inode *inode) {
    struct zj_fs *fs=zj_of(inode);
    kmutex_lock(&fs->lock);
    int rc=zj_begin(fs,2);
    if (rc==0)
        rc=zj_write_inode(fs,inode);
    if (rc && rc!=-SE_ROFS && rc!=-SE_IO)
        zj_abort(fs,rc,"inode update");
    kmutex_unlock(&fs->lock);
    return rc;
}

static void zjop_release_inode(struct vfs_inode *inode) {
    struct zj_fs *fs=zj_of(inode);
    (void)pc_invalidate(&inode->mapping,0);
    kmutex_lock(&fs->lock);
    zj_unorder_inode(fs,inode);
    if (inode->links==0 && !(inode->flags&VFS_I_DEAD) && zj_writable(fs)==0)
        (void)zj_destroy(fs,inode);
    /* Otherwise (read-only/aborted) the inode stays on the orphan list
     * and mount-time orphan processing destroys it. */
    kmutex_unlock(&fs->lock);
}

static void zjop_evict_inode(struct vfs_inode *inode) {
    struct zj_fs *fs=zj_of(inode);
    kmutex_lock(&fs->lock);
    zj_unorder_inode(fs,inode);
    kmutex_unlock(&fs->lock);
}

static int zjop_fsync(struct vfs_inode *inode, int data_only) {
    (void)data_only;
    struct zj_fs *fs=zj_of(inode);
    kmutex_lock(&fs->lock);
    int rc;
    if (fs->aborted)
        rc=-SE_IO;
    else if (fs->txn_count || fs->ordered_count)
        rc=zj_commit_locked(fs);        /* includes device cache flushes */
    else
        rc=zj_flush(fs);                /* data written in place: flush cache */
    kmutex_unlock(&fs->lock);
    return rc;
}

static int zjop_sync(struct vfs_superblock *sb) {
    struct zj_fs *fs=(struct zj_fs *)sb->fs;
    kmutex_lock(&fs->lock);
    int rc=fs->aborted ? -SE_IO : zj_commit_locked(fs);
    if (rc==0 && !fs->readonly)
        rc=zj_flush(fs);
    kmutex_unlock(&fs->lock);
    return rc;
}

static int zjop_statfs(struct vfs_superblock *sb, struct vfs_statfs *out) {
    struct zj_fs *fs=(struct zj_fs *)sb->fs;
    kmutex_lock(&fs->lock);
    out->block_size=ZJ_BLOCK_SIZE;
    out->total_blocks=fs->sb.total_blocks;
    out->free_blocks=fs->sb.free_blocks;
    out->total_inodes=fs->sb.inode_count;
    out->free_inodes=fs->sb.free_inodes;
    out->flags=sb->flags;
    out->name_max=VFS_NAME_MAX;
    kmutex_unlock(&fs->lock);
    return 0;
}

int zjfs_commit(struct vfs_superblock *sb) {
    struct zj_fs *fs=(struct zj_fs *)sb->fs;
    kmutex_lock(&fs->lock);
    int rc=fs->aborted ? -SE_IO : zj_commit_locked(fs);
    kmutex_unlock(&fs->lock);
    return rc;
}

int zjfs_periodic(struct vfs_superblock *sb) {
    struct zj_fs *fs=(struct zj_fs *)sb->fs;
    if (!kmutex_trylock(&fs->lock))
        return 1;
    if (!fs->aborted && (fs->txn_count || fs->ordered_count) &&
        timer_ticks()-fs->txn_start_tick>=ZJ_COMMIT_INTERVAL)
        (void)zj_commit_locked(fs);
    int pending=!fs->aborted && (fs->txn_count || fs->ordered_count);
    kmutex_unlock(&fs->lock);
    return pending;
}

void zjfs_set_txn_hook(void (*hook)(void)) {
    zj_txn_hook=hook;
}

void zjfs_metrics_snapshot(struct vfs_superblock *sb, struct zj_metrics *out) {
    struct zj_fs *fs=(struct zj_fs *)sb->fs;
    kmutex_lock(&fs->lock);
    *out=fs->m;
    kmutex_unlock(&fs->lock);
}

/* ------------------------------------------------ format / mount / check */

static uint64_t zj_div_up(uint64_t a, uint64_t b) {
    return (a+b-1U)/b;
}

static int zj_super_valid(const struct zj_super *sb, uint64_t block_number,
                          uint64_t device_blocks) {
    if (sb->magic!=ZJ_MAGIC || sb->checksum!=zj_meta_csum(sb,block_number))
        return 0;
    if (sb->version!=ZJ_VERSION || sb->block_size!=ZJ_BLOCK_SIZE ||
        sb->inode_size!=ZJ_INODE_SIZE || sb->features_incompat)
        return 0;
    uint64_t total=sb->total_blocks;
    if (total<256U || total>device_blocks || total>0xffffffffULL)
        return 0;
    if (sb->inode_count<64U || sb->inode_count%ZJ_INODES_PER_BLOCK ||
        sb->inode_count>0xffffffffULL)
        return 0;
    if (sb->journal_start!=1U || sb->journal_blocks<16U || sb->journal_blocks>total/2U)
        return 0;
    if (sb->inode_bitmap_start!=1U+sb->journal_blocks ||
        sb->inode_bitmap_blocks!=zj_div_up(sb->inode_count,ZJ_BITS_PER_BITMAP) ||
        sb->block_bitmap_start!=sb->inode_bitmap_start+sb->inode_bitmap_blocks ||
        sb->block_bitmap_blocks!=zj_div_up(total,ZJ_BITS_PER_BITMAP) ||
        sb->inode_table_start!=sb->block_bitmap_start+sb->block_bitmap_blocks ||
        sb->inode_table_blocks!=sb->inode_count/ZJ_INODES_PER_BLOCK ||
        sb->data_start!=sb->inode_table_start+sb->inode_table_blocks ||
        sb->data_start+2U>=total)
        return 0;
    if (sb->root_ino!=ZJ_ROOT_INO || sb->free_blocks>total ||
        sb->free_inodes>sb->inode_count || sb->orphan_head>=sb->inode_count)
        return 0;
    return 1;
}

static void zj_fs_free(struct zj_fs *fs) {
    if (!fs)
        return;
    for (uint32_t i=0; i<ZJ_BUF_MAX; ++i)
        if (fs->bufs[i].data)
            page_free(fs->bufs[i].data);
    if (fs->pending)
        page_free_contiguous(fs->pending,ZJ_PENDING_SLOTS*4U/4096U);
    if (fs->scratch)
        page_free_contiguous(fs->scratch,3);
    page_free_contiguous(fs,ZJ_FS_PAGES);
}

static struct zj_fs *zj_fs_alloc(struct block_device *device) {
    struct zj_fs *fs=(struct zj_fs *)page_alloc_contiguous(ZJ_FS_PAGES);
    if (!fs)
        return 0;
    memset(fs,0,ZJ_FS_PAGES*4096U);
    fs->pending=(uint32_t *)page_alloc_contiguous(ZJ_PENDING_SLOTS*4U/4096U);
    fs->scratch=(uint8_t *)page_alloc_contiguous(3);
    if (!fs->pending || !fs->scratch) {
        zj_fs_free(fs);
        return 0;
    }
    memset(fs->pending,0,ZJ_PENDING_SLOTS*4U);
    kmutex_init(&fs->lock,"zjfs");
    fs->device=device;
    fs->spb=ZJ_BLOCK_SIZE/block_root(device)->sector_size;
    fs->device_blocks=device->sectors/fs->spb;
    return fs;
}

int zjfs_format(struct block_device *device, const struct zj_format_options *options) {
    uint32_t ss=block_root(device)->sector_size;
    if (ss>ZJ_BLOCK_SIZE || ZJ_BLOCK_SIZE%ss)
        return -SE_NOTSUP;
    uint64_t spb=ZJ_BLOCK_SIZE/ss;
    uint64_t total=device->sectors/spb;
    if (total<256U)
        return -SE_NOSPC;
    if (total>0xffffffffULL)
        total=0xffffffffULL;
    uint64_t journal=options && options->journal_blocks ? options->journal_blocks :
                     (total/32U>1024U ? 1024U : (total/32U<64U ? 64U : total/32U));
    uint64_t inodes=options && options->inode_count ? options->inode_count : total/4U;
    if (inodes<64U)
        inodes=64U;
    inodes=zj_div_up(inodes,ZJ_INODES_PER_BLOCK)*ZJ_INODES_PER_BLOCK;
    if (journal<16U || journal>total/4U)
        return -SE_INVAL;
    struct zj_super *sb=(struct zj_super *)page_alloc_zero();
    uint8_t *block=(uint8_t *)page_alloc_zero();
    if (!sb || !block) {
        if (sb) page_free(sb);
        if (block) page_free(block);
        return -SE_NOMEM;
    }
    sb->magic=ZJ_MAGIC;
    sb->version=ZJ_VERSION;
    sb->block_size=ZJ_BLOCK_SIZE;
    sb->inode_size=ZJ_INODE_SIZE;
    sb->total_blocks=total;
    sb->inode_count=inodes;
    sb->journal_start=1;
    sb->journal_blocks=journal;
    sb->inode_bitmap_start=1U+journal;
    sb->inode_bitmap_blocks=zj_div_up(inodes,ZJ_BITS_PER_BITMAP);
    sb->block_bitmap_start=sb->inode_bitmap_start+sb->inode_bitmap_blocks;
    sb->block_bitmap_blocks=zj_div_up(total,ZJ_BITS_PER_BITMAP);
    sb->inode_table_start=sb->block_bitmap_start+sb->block_bitmap_blocks;
    sb->inode_table_blocks=inodes/ZJ_INODES_PER_BLOCK;
    sb->data_start=sb->inode_table_start+sb->inode_table_blocks;
    int rc=0;
    if (sb->data_start+16U>=total)
        rc=-SE_NOSPC;
    uint64_t root_block=sb->data_start;
    uint64_t lf_block=sb->data_start+1U;
    sb->root_ino=ZJ_ROOT_INO;
    sb->state=ZJ_STATE_CLEAN;
    sb->free_blocks=total-(sb->data_start+2U)-1U;
    sb->free_inodes=inodes-3U;
    uint64_t seed=timer_monotonic_ns()^(total<<17)^0x5a45524f4f53ULL;
    for (uint32_t i=0; i<16; ++i) {
        seed=seed*6364136223846793005ULL+1442695040888963407ULL;
        sb->uuid[i]=(uint8_t)(seed>>56);
    }
    const char *label=options && options->label ? options->label : "zeroos";
    for (uint32_t i=0; label[i] && i<31U; ++i)
        sb->label[i]=label[i];
    sb->generation_counter=2;
    sb->lost_found_ino=ZJ_LOSTFOUND_INO;
    uint64_t now=vfs_now_ns();
    sb->last_check_time=now;
#define ZWRITE(blk) do { if (rc==0) rc=block_rw(device,BLOCK_OP_WRITE,(blk)*spb,block, \
                         ZJ_BLOCK_SIZE,BLOCK_PRIO_NORMAL); } while (0)
    /* Journal: jsb + invalid descriptor slot. */
    struct zj_jsb *jsb=(struct zj_jsb *)block;
    memset(block,0,4096);
    jsb->magic=ZJ_JSB_MAGIC;
    jsb->version=1;
    jsb->sequence=1;
    jsb->blocks=journal;
    jsb->checksum=zj_meta_csum(jsb,1);
    ZWRITE(1U);
    memset(block,0,4096);
    ZWRITE(2U);
    /* Inode bitmap. */
    for (uint64_t i=0; i<sb->inode_bitmap_blocks; ++i) {
        memset(block,0,4096);
        uint64_t base=i*ZJ_BITS_PER_BITMAP;
        for (uint32_t bit=0; bit<ZJ_BITS_PER_BITMAP; ++bit)
            if (base+bit<=ZJ_LOSTFOUND_INO || base+bit>=inodes)
                block[bit>>3]|=(uint8_t)(1U<<(bit&7U));
        wr32(block+4088,ZJ_BITMAP_MAGIC);
        wr32(block+4092,zj_meta_csum(block,sb->inode_bitmap_start+i));
        ZWRITE(sb->inode_bitmap_start+i);
    }
    /* Block bitmap. */
    for (uint64_t i=0; i<sb->block_bitmap_blocks; ++i) {
        memset(block,0,4096);
        uint64_t base=i*ZJ_BITS_PER_BITMAP;
        for (uint32_t bit=0; bit<ZJ_BITS_PER_BITMAP; ++bit) {
            uint64_t b=base+bit;
            if (b<=lf_block || b>=total-1U)
                block[bit>>3]|=(uint8_t)(1U<<(bit&7U));
        }
        wr32(block+4088,ZJ_BITMAP_MAGIC);
        wr32(block+4092,zj_meta_csum(block,sb->block_bitmap_start+i));
        ZWRITE(sb->block_bitmap_start+i);
    }
    /* Inode table. */
    for (uint64_t i=0; i<sb->inode_table_blocks; ++i) {
        memset(block,0,4096);
        if (i==0) {
            struct zj_dinode *root=(struct zj_dinode *)(block+ZJ_ROOT_INO*ZJ_INODE_SIZE);
            struct zj_dinode *lf=(struct zj_dinode *)(block+ZJ_LOSTFOUND_INO*ZJ_INODE_SIZE);
            root->mode=(uint16_t)(VFS_S_IFDIR|0755U);
            root->links=3;
            root->size=4096;
            root->blocks=1;
            root->atime_ns=root->mtime_ns=root->ctime_ns=root->crtime_ns=now;
            root->generation=1;
            root->direct[0]=(uint32_t)root_block;
            root->parent=ZJ_ROOT_INO;
            root->checksum=zj_inode_csum(root,ZJ_ROOT_INO);
            lf->mode=(uint16_t)(VFS_S_IFDIR|0700U);
            lf->links=2;
            lf->size=4096;
            lf->blocks=1;
            lf->atime_ns=lf->mtime_ns=lf->ctime_ns=lf->crtime_ns=now;
            lf->generation=2;
            lf->direct[0]=(uint32_t)lf_block;
            lf->parent=ZJ_ROOT_INO;
            lf->checksum=zj_inode_csum(lf,ZJ_LOSTFOUND_INO);
        }
        ZWRITE(sb->inode_table_start+i);
    }
    /* Root directory: "lost+found". */
    memset(block,0,4096);
    wr32(block,ZJ_DIR_MAGIC);
    wr32(block+4,ZJ_ROOT_INO);
    zj_rec_fill(block,ZJ_DIR_HEADER,ZJ_LOSTFOUND_INO,ZJ_BLOCK_SIZE-ZJ_DIR_HEADER,
                "lost+found",10,ZJ_FT_DIR);
    wr32(block+8,zj_dir_csum(block,root_block));
    ZWRITE(root_block);
    memset(block,0,4096);
    wr32(block,ZJ_DIR_MAGIC);
    wr32(block+4,ZJ_LOSTFOUND_INO);
    wr16(block+ZJ_DIR_HEADER+4,(uint16_t)(ZJ_BLOCK_SIZE-ZJ_DIR_HEADER));
    wr32(block+8,zj_dir_csum(block,lf_block));
    ZWRITE(lf_block);
    /* Superblocks last: a crash during format leaves no valid ZJFS. */
    if (rc==0) {
        sb->checksum=zj_meta_csum(sb,total-1U);
        memcpy(block,sb,4096);
        ZWRITE(total-1U);
        sb->checksum=zj_meta_csum(sb,0);
        memcpy(block,sb,4096);
        ZWRITE(0U);
    }
#undef ZWRITE
    if (rc==0)
        rc=block_flush(device,BLOCK_PRIO_NORMAL);
    page_free(sb);
    page_free(block);
    return rc;
}

static int zj_read_super(struct zj_fs *fs, int *used_backup) {
    uint8_t *page=fs->scratch;
    int rc=zj_io(fs,BLOCK_OP_READ,0,page);
    if (rc==0 && zj_super_valid((struct zj_super *)page,0,fs->device_blocks)) {
        memcpy(&fs->sb,page,4096);
        *used_backup=0;
        return 0;
    }
    int primary_rc=rc;
    uint64_t backup=fs->device_blocks-1U;
    rc=zj_io(fs,BLOCK_OP_READ,backup,page);
    if (rc==0 && zj_super_valid((struct zj_super *)page,backup,fs->device_blocks) &&
        ((struct zj_super *)page)->total_blocks==fs->device_blocks) {
        memcpy(&fs->sb,page,4096);
        *used_backup=1;
        klog("ZEROOS: zjfs %s: primary superblock invalid (%d); using backup.",
             fs->device->name,primary_rc);
        return 0;
    }
    return primary_rc ? primary_rc : -SE_UCLEAN;
}

int zjfs_probe(struct block_device *device) {
    struct zj_fs *fs=zj_fs_alloc(device);
    if (!fs)
        return -SE_NOMEM;
    int used_backup;
    int rc=zj_read_super(fs,&used_backup);
    zj_fs_free(fs);
    return rc;
}

/* Recompute free counters from the bitmaps (after crash or backup use). */
static int zj_recount(struct zj_fs *fs) {
    uint64_t used_blocks=0, used_inodes=0;
    for (int which=0; which<2; ++which) {
        uint64_t start=which ? fs->sb.block_bitmap_start : fs->sb.inode_bitmap_start;
        uint64_t blocks=which ? fs->sb.block_bitmap_blocks : fs->sb.inode_bitmap_blocks;
        uint64_t nbits=which ? fs->sb.total_blocks : fs->sb.inode_count;
        for (uint64_t i=0; i<blocks; ++i) {
            struct zj_buf *b;
            int rc=zj_bget(fs,start+i,ZK_BITMAP,0,1,&b);
            if (rc)
                return rc;
            for (uint32_t bit=0; bit<ZJ_BITS_PER_BITMAP && i*ZJ_BITS_PER_BITMAP+bit<nbits; ++bit)
                if (b->data[bit>>3]&(1U<<(bit&7U))) {
                    if (which) ++used_blocks; else ++used_inodes;
                }
            zj_bput(b);
        }
    }
    fs->sb.free_blocks=fs->sb.total_blocks-used_blocks;
    fs->sb.free_inodes=fs->sb.inode_count-used_inodes;
    return 0;
}

static int zj_process_orphans(struct zj_fs *fs) {
    uint32_t processed=0;
    for (uint64_t guard=0; fs->sb.orphan_head && guard<fs->sb.inode_count; ++guard) {
        uint64_t ino=fs->sb.orphan_head;
        struct zj_dinode d;
        int rc=zj_read_dinode(fs,ino,&d);
        if (rc==0 && (d.mode==0 || !(d.flags&ZJ_IF_ORPHAN)))
            rc=-SE_UCLEAN;
        if (rc) {
            klog("ZEROOS: zjfs %s: orphan list corrupt at inode %llu.",fs->device->name,ino);
            return rc;
        }
        struct vfs_inode *temp=&fs->tmp_inode;
        memset(temp,0,sizeof(*temp));
        temp->ino=ino;
        zj_from_dinode(temp,&d);
        if (d.links==0) {
            rc=zj_destroy(fs,temp);
        } else {
            rc=zj_truncate_blocks(fs,temp,zj_div_up(temp->size,ZJ_BLOCK_SIZE));
            if (rc==0)
                rc=zj_begin(fs,4);
            if (rc==0) {
                temp->fs_flags&=~ZJ_IF_TRUNC;
                rc=zj_orphan_remove(fs,temp);
            }
        }
        if (rc)
            return rc;
        ++processed;
    }
    if (fs->sb.orphan_head) {
        klog("ZEROOS: zjfs %s: orphan list cycle detected.",fs->device->name);
        return -SE_UCLEAN;
    }
    fs->m.orphans_recovered+=processed;
    if (processed)
        klog("ZEROOS: zjfs %s: recovered %u orphan inode(s).",fs->device->name,processed);
    return 0;
}

static int zjop_unmount(struct vfs_superblock *vsb);

static const struct vfs_fs_ops zj_ops={
    .lookup=zjop_lookup,
    .read_inode=zjop_read_inode,
    .create=zjop_create,
    .unlink=zjop_unlink,
    .link=zjop_link,
    .rename=zjop_rename,
    .readdir=zjop_readdir,
    .map_page=zjop_map_page,
    .set_size=zjop_set_size,
    .update_inode=zjop_update_inode,
    .release_inode=zjop_release_inode,
    .evict_inode=zjop_evict_inode,
    .fsync=zjop_fsync,
    .sync=zjop_sync,
    .statfs=zjop_statfs,
    .unmount=zjop_unmount,
};

/* Open + recover. Leaves fs locked-free and ready; on error frees it. */
static int zj_open(struct block_device *device, int readonly, int checking,
                   struct zj_fs **out) {
    uint32_t ss=block_root(device)->sector_size;
    if (ss>ZJ_BLOCK_SIZE || ZJ_BLOCK_SIZE%ss)
        return -SE_NOTSUP;
    struct zj_fs *fs=zj_fs_alloc(device);
    if (!fs)
        return -SE_NOMEM;
    fs->checking=(uint32_t)checking;
    int device_ro=(device->flags&BLOCK_DEV_READ_ONLY)!=0;
    if (device_ro)
        readonly=1;
    int used_backup=0;
    int rc=zj_read_super(fs,&used_backup);
    if (rc) {
        klog("ZEROOS: zjfs %s: no valid superblock (%d).",device->name,rc);
        zj_fs_free(fs);
        return rc;
    }
    if ((fs->sb.state&ZJ_STATE_ERROR) && !checking) {
        klog("ZEROOS: zjfs %s: filesystem marked ERROR (errors=%u, last=%u); mounting read-only, run fsck.",
             device->name,fs->sb.error_count,fs->sb.last_error_code);
        readonly=1;
    }
    if (fs->sb.features_ro_compat)
        readonly=1;
    fs->readonly=(uint32_t)readonly;
    uint64_t txn_max=fs->sb.journal_blocks-3U;
    if (txn_max>ZJ_TXN_MAX) txn_max=ZJ_TXN_MAX;
    if (txn_max>ZJ_BUF_MAX/2U) txn_max=ZJ_BUF_MAX/2U;
    fs->txn_max=(uint32_t)txn_max;
    fs->block_hint=fs->sb.data_start;
    fs->inode_hint=ZJ_LOSTFOUND_INO+1U;
    kmutex_lock(&fs->lock);
    /* Replay whenever the device is writable, even for read-only mounts
     * (user request or ERROR state): the journal is what makes the
     * on-disk state consistent. A read-only device with a pending
     * transaction cannot be presented consistently: refuse. */
    rc=zj_replay(fs,!device_ro);
    int replayed=rc==1;
    if (rc==-SE_ROFS) {
        klog("ZEROOS: zjfs %s: journal needs replay but device is read-only.",device->name);
    }
    if (rc<0)
        goto fail;
    if (replayed || used_backup) {
        /* Replay may have rewritten block 0; reload the primary. */
        int backup_again=0;
        struct zj_super saved=fs->sb;
        if (zj_read_super(fs,&backup_again)!=0)
            fs->sb=saved;
    }
    if (replayed || used_backup || !(fs->sb.state&ZJ_STATE_CLEAN)) {
        rc=zj_recount(fs);
        if (rc)
            goto fail;
    }
    if (!readonly) {
        rc=zj_process_orphans(fs);
        if (rc)
            goto fail;
        rc=zj_begin(fs,1);
        if (rc)
            goto fail;
        if (!checking) {
            fs->sb.state=(fs->sb.state&ZJ_STATE_ERROR)|ZJ_STATE_DIRTY;
            ++fs->sb.mount_count;
            fs->sb.last_mount_time=zj_now();
        }
        rc=zj_sb_to_txn(fs);
        if (rc==0)
            rc=zj_commit_locked(fs);
        if (rc)
            goto fail;
    }
    kmutex_unlock(&fs->lock);
    klog("ZEROOS: zjfs %s: opened %s blocks=%llu free=%llu inodes=%llu free=%llu journal=%llu replayed=%d backup_sb=%d.",
         device->name,readonly ? "ro" : "rw",fs->sb.total_blocks,fs->sb.free_blocks,
         fs->sb.inode_count,fs->sb.free_inodes,fs->sb.journal_blocks,replayed,used_backup);
    *out=fs;
    return 0;
fail:
    kmutex_unlock(&fs->lock);
    klog("ZEROOS: zjfs %s: mount recovery failed (%d).",device->name,rc);
    zj_fs_free(fs);
    return rc;
}

int zjfs_mount(struct block_device *device, uint32_t flags, struct vfs_superblock *vsb) {
    struct zj_fs *fs;
    int rc=zj_open(device,(flags&VFS_SB_RDONLY) ? 1 : 0,0,&fs);
    if (rc)
        return rc;
    fs->vsb=vsb;
    vsb->ops=&zj_ops;
    vsb->fs=fs;
    vsb->device=device;
    if (fs->readonly)
        vsb->flags|=VFS_SB_RDONLY;
    vsb->fs_name[0]='z'; vsb->fs_name[1]='j'; vsb->fs_name[2]='f'; vsb->fs_name[3]='s';
    return 0;
}

/* Write a clean superblock to both locations (fs locked, journal empty). */
static int zj_write_clean(struct zj_fs *fs) {
    uint8_t *page=fs->scratch;
    /* Retire the last checkpointed transaction so the next mount does not
     * replay it (replay would be idempotent, but it would also restore
     * the DIRTY superblock image it contains). */
    memset(page,0,4096);
    struct zj_jsb *jsb=(struct zj_jsb *)page;
    jsb->magic=ZJ_JSB_MAGIC;
    jsb->version=1;
    jsb->sequence=fs->seq;
    jsb->blocks=fs->sb.journal_blocks;
    jsb->checksum=zj_meta_csum(jsb,fs->sb.journal_start);
    int jrc=zj_io(fs,BLOCK_OP_WRITE,fs->sb.journal_start,page);
    if (jrc==0)
        jrc=zj_flush(fs);
    if (jrc)
        return jrc;
    fs->sb.state=(fs->sb.state&ZJ_STATE_ERROR)|ZJ_STATE_CLEAN;
    fs->sb.last_write_time=zj_now();
    memcpy(page,&fs->sb,4096);
    struct zj_super *out=(struct zj_super *)page;
    out->checksum=zj_meta_csum(out,0);
    int rc=zj_io(fs,BLOCK_OP_WRITE,0,page);
    if (rc==0) {
        out->checksum=zj_meta_csum(out,fs->sb.total_blocks-1U);
        rc=zj_io(fs,BLOCK_OP_WRITE,fs->sb.total_blocks-1U,page);
    }
    if (rc==0)
        rc=zj_flush(fs);
    /* Keep the cached block-0 buffer coherent. */
    struct zj_buf *b=zj_lookup(fs,0);
    if (b)
        b->flags&=~ZB_VALID;
    return rc;
}

static int zj_close(struct zj_fs *fs) {
    kmutex_lock(&fs->lock);
    int rc=0;
    if (!fs->aborted && !fs->readonly) {
        rc=zj_commit_locked(fs);
        if (rc==0)
            rc=zj_write_clean(fs);
    } else if (fs->aborted) {
        rc=-SE_IO;
    }
    kmutex_unlock(&fs->lock);
    zj_fs_free(fs);
    return rc;
}

static int zjop_unmount(struct vfs_superblock *vsb) {
    struct zj_fs *fs=(struct zj_fs *)vsb->fs;
    int rc=zj_close(fs);
    vsb->fs=0;
    return rc;
}

/* ---------------------------------------------------------------- check */

struct zj_checker {
    struct zj_fs *fs;
    uint8_t *used;             /* block bitmap computed from reachability */
    uint16_t *refs;            /* directory references per inode */
    uint16_t *subdirs;
    uint8_t *allocated;        /* inode has valid content */
    uint32_t *queue;
    uint64_t used_pages, refs_pages, alloc_pages, queue_pages;
    struct zj_check_report *report;
    int repair;
};

static int zjc_mark(struct zj_checker *c, uint64_t block, uint64_t ino) {
    struct zj_fs *fs=c->fs;
    if (!zj_block_ok(fs,block)) {
        ++c->report->structure_errors;
        klog("ZEROOS: fsck: inode %llu references invalid block %llu.",ino,block);
        return -1;
    }
    if (c->used[block>>3]&(1U<<(block&7U))) {
        ++c->report->structure_errors;
        ++c->report->fatal;
        klog("ZEROOS: fsck: block %llu cross-linked (inode %llu).",block,ino);
        return -1;
    }
    c->used[block>>3]|=(uint8_t)(1U<<(block&7U));
    return 0;
}

/* Walk the block map of `inode`, marking blocks; returns blocks counted. */
static uint64_t zjc_walk(struct zj_checker *c, struct vfs_inode *inode) {
    struct zj_fs *fs=c->fs;
    uint64_t count=0;
    for (uint32_t i=0; i<ZJ_DIRECT; ++i)
        if (inode->fs_direct[i] && zjc_mark(c,inode->fs_direct[i],inode->ino)==0)
            ++count;
    if (inode->fs_indirect && zjc_mark(c,inode->fs_indirect,inode->ino)==0) {
        ++count;
        struct zj_buf *b;
        if (zj_bget(fs,inode->fs_indirect,ZK_INDIRECT,(uint32_t)inode->ino,1,&b)==0) {
            uint32_t *e=(uint32_t *)b->data;
            for (uint32_t j=0; j<ZJ_PTRS_PER_BLOCK; ++j)
                if (e[j] && zjc_mark(c,e[j],inode->ino)==0)
                    ++count;
            zj_bput(b);
        } else {
            ++c->report->checksum_errors;
            ++c->report->fatal;
        }
    }
    if (inode->fs_dindirect && zjc_mark(c,inode->fs_dindirect,inode->ino)==0) {
        ++count;
        struct zj_buf *mid;
        if (zj_bget(fs,inode->fs_dindirect,ZK_INDIRECT,(uint32_t)inode->ino,1,&mid)==0) {
            uint32_t mids[ZJ_PTRS_PER_BLOCK];
            memcpy(mids,mid->data,sizeof(mids));
            zj_bput(mid);
            for (uint32_t k=0; k<ZJ_PTRS_PER_BLOCK; ++k) {
                if (!mids[k] || zjc_mark(c,mids[k],inode->ino))
                    continue;
                ++count;
                struct zj_buf *leaf;
                if (zj_bget(fs,mids[k],ZK_INDIRECT,(uint32_t)inode->ino,1,&leaf)==0) {
                    uint32_t *e=(uint32_t *)leaf->data;
                    for (uint32_t j=0; j<ZJ_PTRS_PER_BLOCK; ++j)
                        if (e[j] && zjc_mark(c,e[j],inode->ino)==0)
                            ++count;
                    zj_bput(leaf);
                } else {
                    ++c->report->checksum_errors;
                    ++c->report->fatal;
                }
            }
        } else {
            ++c->report->checksum_errors;
            ++c->report->fatal;
        }
    }
    return count;
}

static int zjc_load(struct zj_checker *c, uint64_t ino, struct vfs_inode *inode) {
    struct zj_dinode d;
    int rc=zj_read_dinode(c->fs,ino,&d);
    if (rc)
        return rc;
    memset(inode,0,sizeof(*inode));
    inode->ino=ino;
    if (d.mode==0)
        return 1;
    zj_from_dinode(inode,&d);
    return zj_valid_dinode(c->fs,&d) ? 0 : -SE_UCLEAN;
}

static int zjc_bit(struct zj_fs *fs, uint64_t start, uint64_t index, int set, int *changed) {
    struct zj_buf *b;
    int rc=zj_bget(fs,start+index/ZJ_BITS_PER_BITMAP,ZK_BITMAP,0,1,&b);
    if (rc)
        return rc;
    uint32_t bit=(uint32_t)(index%ZJ_BITS_PER_BITMAP);
    int is_set=(b->data[bit>>3]>>(bit&7U))&1U;
    *changed=0;
    if (set>=0 && is_set!=set) {
        if (set)
            b->data[bit>>3]|=(uint8_t)(1U<<(bit&7U));
        else
            b->data[bit>>3]&=(uint8_t)~(1U<<(bit&7U));
        zj_bdirty(fs,b);
        *changed=1;
    }
    zj_bput(b);
    return is_set;
}

static int zjc_run(struct zj_checker *c) {
    struct zj_fs *fs=c->fs;
    struct zj_check_report *r=c->report;
    struct vfs_inode *inodep=&fs->tmp_inode, *dirp=&fs->tmp_dir;
    uint64_t ninodes=fs->sb.inode_count;
    /* Metadata region is always in use. */
    for (uint64_t b=0; b<fs->sb.data_start; ++b)
        c->used[b>>3]|=(uint8_t)(1U<<(b&7U));
    uint64_t last=fs->sb.total_blocks-1U;
    c->used[last>>3]|=(uint8_t)(1U<<(last&7U));
    /* Pass 1: inodes. */
    for (uint64_t ino=1; ino<ninodes; ++ino) {
        int in_bitmap_changed;
        int in_bitmap=zjc_bit(fs,fs->sb.inode_bitmap_start,ino,-1,&in_bitmap_changed);
        if (in_bitmap<0)
            return in_bitmap;
        int rc=zjc_load(c,ino,inodep);
        if (rc<0) {
            ++r->checksum_errors;
            ++r->fatal;
            klog("ZEROOS: fsck: inode %llu unreadable/invalid (%d).",ino,rc);
            continue;
        }
        if (rc==1) {
            if (in_bitmap && ino>ZJ_LOSTFOUND_INO) {
                ++r->leaked_inodes;
                if (c->repair) {
                    int changed;
                    if (zj_begin(fs,2)==0 &&
                        zjc_bit(fs,fs->sb.inode_bitmap_start,ino,0,&changed)>=0)
                        ++r->repaired;
                }
            }
            continue;
        }
        c->allocated[ino>>3]|=(uint8_t)(1U<<(ino&7U));
        ++r->inodes_used;
        if (!in_bitmap) {
            ++r->structure_errors;
            klog("ZEROOS: fsck: inode %llu in use but free in bitmap.",ino);
            if (c->repair) {
                int changed;
                if (zj_begin(fs,2)==0 &&
                    zjc_bit(fs,fs->sb.inode_bitmap_start,ino,1,&changed)>=0)
                    ++r->repaired;
            }
        }
        uint64_t counted=zjc_walk(c,inodep);
        if (counted!=inodep->blocks) {
            ++r->counter_errors;
            if (c->repair && zj_begin(fs,2)==0) {
                inodep->blocks=counted;
                if (zj_write_inode(fs,inodep)==0)
                    ++r->repaired;
            }
        }
        if (VFS_S_ISDIR(inodep->mode) && inodep->size%ZJ_BLOCK_SIZE) {
            ++r->structure_errors;
            ++r->fatal;
        }
    }
    /* Pass 2: directory tree from the root (BFS, each directory once). */
    uint64_t head=0, tail=0;
    c->queue[tail++]=ZJ_ROOT_INO;
    c->refs[ZJ_ROOT_INO]=1;             /* the root references itself */
    while (head<tail) {
        uint64_t dino=c->queue[head++];
        if (zjc_load(c,dino,dirp)!=0)
            continue;
        uint64_t blocks=dirp->size/ZJ_BLOCK_SIZE;
        for (uint64_t index=0; index<blocks; ++index) {
            struct zj_buf *b;
            if (zj_dir_get(fs,dirp,index,&b)!=0) {
                ++r->checksum_errors;
                ++r->fatal;
                continue;
            }
            for (uint32_t off=ZJ_DIR_HEADER; off<ZJ_BLOCK_SIZE;) {
                uint32_t rec_len=zj_rec_valid(fs,b->data,off);
                if (!rec_len) {
                    ++r->structure_errors;
                    ++r->fatal;
                    break;
                }
                uint64_t target=rd32(b->data+off);
                if (target) {
                    if (!(c->allocated[target>>3]&(1U<<(target&7U)))) {
                        ++r->structure_errors;
                        ++r->fatal;
                        klog("ZEROOS: fsck: dir %llu entry points to free inode %llu.",
                             dino,target);
                    } else if (b->data[off+7]==ZJ_FT_DIR) {
                        if (c->refs[target]) {
                            ++r->structure_errors;
                            ++r->fatal;
                        } else if (tail<ninodes) {
                            c->queue[tail++]=(uint32_t)target;
                            ++c->subdirs[dino];
                        }
                        ++c->refs[target];
                    } else if (c->refs[target]<0xffffU) {
                        ++c->refs[target];
                    }
                }
                off+=rec_len;
            }
            zj_bput(b);
        }
    }
    /* Pass 3: link counts and reachability. */
    for (uint64_t ino=1; ino<ninodes; ++ino) {
        if (!(c->allocated[ino>>3]&(1U<<(ino&7U))))
            continue;
        if (zjc_load(c,ino,inodep)!=0)
            continue;
        uint32_t expected;
        if (c->refs[ino]==0) {
            if (inodep->fs_flags&ZJ_IF_ORPHAN)
                continue;       /* pending orphan (processed at mount) */
            ++r->leaked_inodes;
            klog("ZEROOS: fsck: inode %llu unreachable.",ino);
            if (c->repair && zj_begin(fs,12)==0 && fs->sb.free_blocks>=4U) {
                struct vfs_inode *lfp=&fs->tmp_lf;
                if (zjc_load(c,ZJ_LOSTFOUND_INO,lfp)==0 && VFS_S_ISDIR(lfp->mode)) {
                    char name[24];
                    uint32_t len=(uint32_t)ksnprintf(name,sizeof(name),"#%llu",ino);
                    int directory=VFS_S_ISDIR(inodep->mode);
                    if (zj_dir_add(fs,lfp,name,len,ino,directory ? ZJ_FT_DIR : ZJ_FT_REG)==0) {
                        if (directory) {
                            inodep->fs_parent=ZJ_LOSTFOUND_INO;
                            ++lfp->links;
                            inodep->links=(uint32_t)(2U+c->subdirs[ino]);
                        } else {
                            inodep->links=1;
                        }
                        (void)zj_write_inode(fs,lfp);
                        (void)zj_write_inode(fs,inodep);
                        ++r->repaired;
                    }
                }
            }
            continue;
        }
        expected=VFS_S_ISDIR(inodep->mode) ? 2U+c->subdirs[ino] : c->refs[ino];
        if (inodep->links!=expected) {
            ++r->link_errors;
            klog("ZEROOS: fsck: inode %llu links=%u expected=%u.",ino,inodep->links,expected);
            if (c->repair && zj_begin(fs,2)==0) {
                inodep->links=expected;
                if (zj_write_inode(fs,inodep)==0)
                    ++r->repaired;
            }
        }
    }
    /* Pass 4: block bitmap vs reachability. */
    for (uint64_t b=0; b<fs->sb.total_blocks; ++b) {
        int changed;
        int want=(c->used[b>>3]>>(b&7U))&1U;
        int have=zjc_bit(fs,fs->sb.block_bitmap_start,b,-1,&changed);
        if (have<0)
            return have;
        if (want)
            ++r->blocks_used;
        if (have==want)
            continue;
        if (have && !want)
            ++r->leaked_blocks;
        else {
            ++r->structure_errors;
            klog("ZEROOS: fsck: block %llu in use but free in bitmap.",b);
        }
        if (c->repair && zj_begin(fs,2)==0 &&
            zjc_bit(fs,fs->sb.block_bitmap_start,b,want,&changed)>=0)
            ++r->repaired;
    }
    /* Counters. */
    uint64_t free_blocks=fs->sb.total_blocks-r->blocks_used;
    uint64_t free_inodes=fs->sb.free_inodes;
    if (zj_recount(fs)==0) {
        if (fs->sb.free_blocks!=free_blocks)
            ++r->counter_errors;
        (void)free_inodes;
    }
    return 0;
}

int zjfs_check(struct block_device *device, int repair, struct zj_check_report *report) {
    memset(report,0,sizeof(*report));
    struct zj_fs *fs;
    int rc=zj_open(device,repair ? 0 : 1,1,&fs);
    if (rc)
        return rc;
    uint64_t total=fs->sb.total_blocks;
    uint64_t ninodes=fs->sb.inode_count;
    struct zj_checker c;
    memset(&c,0,sizeof(c));
    c.fs=fs;
    c.report=report;
    c.repair=repair;
    c.used_pages=zj_div_up(zj_div_up(total,8U),4096U);
    c.refs_pages=zj_div_up(ninodes*2U,4096U);
    c.alloc_pages=zj_div_up(zj_div_up(ninodes,8U),4096U);
    c.queue_pages=zj_div_up(ninodes*4U,4096U);
    if (c.used_pages>64U || c.refs_pages>64U || c.queue_pages>128U) {
        (void)zj_close(fs);
        return -SE_NOTSUP;      /* large filesystems: use the host fsck */
    }
    c.used=(uint8_t *)page_alloc_contiguous(c.used_pages);
    c.refs=(uint16_t *)page_alloc_contiguous(c.refs_pages);
    c.subdirs=(uint16_t *)page_alloc_contiguous(c.refs_pages);
    c.allocated=(uint8_t *)page_alloc_contiguous(c.alloc_pages);
    c.queue=(uint32_t *)page_alloc_contiguous(c.queue_pages);
    if (!c.used || !c.refs || !c.subdirs || !c.allocated || !c.queue) {
        rc=-SE_NOMEM;
    } else {
        memset(c.used,0,c.used_pages*4096U);
        memset(c.refs,0,c.refs_pages*4096U);
        memset(c.subdirs,0,c.refs_pages*4096U);
        memset(c.allocated,0,c.alloc_pages*4096U);
        kmutex_lock(&fs->lock);
        rc=zjc_run(&c);
        if (rc==0 && repair) {
            if (report->fatal==0)
                fs->sb.state&=~ZJ_STATE_ERROR;
            fs->sb.last_check_time=zj_now();
            rc=zj_begin(fs,1);
            if (rc==0)
                rc=zj_sb_to_txn(fs);
        }
        kmutex_unlock(&fs->lock);
    }
    if (c.used) page_free_contiguous(c.used,c.used_pages);
    if (c.refs) page_free_contiguous(c.refs,c.refs_pages);
    if (c.subdirs) page_free_contiguous(c.subdirs,c.refs_pages);
    if (c.allocated) page_free_contiguous(c.allocated,c.alloc_pages);
    if (c.queue) page_free_contiguous(c.queue,c.queue_pages);
    int close_rc=zj_close(fs);
    if (rc==0)
        rc=close_rc;
    klog("ZEROOS: fsck %s: inodes=%llu blocks=%llu checksum_errors=%u structure_errors=%u leaked_blocks=%u leaked_inodes=%u link_errors=%u counter_errors=%u repaired=%u fatal=%u.",
         device->name,report->inodes_used,report->blocks_used,report->checksum_errors,
         report->structure_errors,report->leaked_blocks,report->leaked_inodes,
         report->link_errors,report->counter_errors,report->repaired,report->fatal);
    return rc;
}
