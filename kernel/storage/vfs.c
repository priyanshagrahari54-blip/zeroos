/*
 * VFS core: mounts, inode cache, dentry (name) cache, path resolution,
 * permission checks, open file descriptions, descriptor tables.
 * Contracts: vfs.h and docs/VFS.md.
 */
#include "vfs.h"
#include "zjfs.h"
#include "../kstring.h"
#include "../memory.h"
#include "../task.h"
#include "../timer.h"
#include "../wait.h"

#define VFS_INODE_HASH 64U
#define VFS_I_LOADING 0x100U
#define VFS_I_BUSY    0x200U        /* claimed for eviction/release */
#define VFS_DNAME_MAX 47U
#define VFS_INT64_MAX 0x7fffffffffffffffLL

struct vfs_mount_entry {
    char name[32];                  /* "" = "/" */
    struct vfs_superblock *sb;
    uint32_t in_use;
};

struct vfs_dentry {
    struct vfs_superblock *sb;
    uint64_t parent;
    uint64_t ino;
    uint64_t tick;
    uint32_t length;
    char name[VFS_DNAME_MAX + 1U];
};

struct vfs_fdtable {
    uint64_t pid;
    uint32_t generation;
    uint32_t state;                 /* 0 free, 1 live, 2 closing */
    struct vfs_file *fds[VFS_MAX_FDS];
};

static struct spinlock vfs_lock;
static struct wait_queue vfs_waitq;
static struct kmutex vfs_ns_lock;
static struct kmutex vfs_mount_lock;
static struct vfs_mount_entry mounts[VFS_MAX_MOUNTS];
static struct vfs_superblock superblocks[VFS_MAX_MOUNTS];
static struct vfs_inode inodes[VFS_MAX_INODES];
static struct vfs_inode *inode_hash[VFS_INODE_HASH];
static struct vfs_file files[VFS_MAX_FILES];
static struct vfs_dentry dcache[VFS_DCACHE_SIZE];
static struct vfs_fdtable fdtables[VFS_MAX_FDTABLES];
static struct vfs_metrics metrics;
static uint64_t boot_epoch_ns;
static uint64_t inode_clock;
static int vfs_ready;

static void vfs_count(uint64_t *counter) {
    __atomic_fetch_add(counter,1ULL,__ATOMIC_RELAXED);
}

uint64_t vfs_now_ns(void) {
    return boot_epoch_ns+timer_monotonic_ns();
}

int vfs_init(void) {
    if (vfs_ready)
        return 0;
    spinlock_init(&vfs_lock);
    wait_queue_init(&vfs_waitq);
    kmutex_init(&vfs_ns_lock,"vfs-namespace");
    kmutex_init(&vfs_mount_lock,"vfs-mount");
    uint64_t wall=timer_wallclock_unix_seconds();
    uint64_t mono=timer_monotonic_ns();
    boot_epoch_ns=wall ? wall*1000000000ULL-(mono<wall*1000000000ULL ? mono : 0) : 0;
    vfs_ready=1;
    return 0;
}

static void vfs_wait_locked(uint64_t *flags) {
    uint64_t inner;
    if (wait_queue_prepare(&vfs_waitq,&inner)!=0) {
        spin_unlock_irqrestore(&vfs_lock,*flags);
        task_yield();
        *flags=spin_lock_irqsave(&vfs_lock);
        return;
    }
    spin_unlock(&vfs_lock);
    (void)wait_queue_commit(*flags);
    *flags=spin_lock_irqsave(&vfs_lock);
}

/* ---------------------------------------------------------- permissions */

int vfs_permission(const struct vfs_cred *cred, struct vfs_inode *inode, uint32_t mask) {
    if (cred->uid==0)
        return 0;
    uint32_t bits;
    if (cred->uid==inode->uid)
        bits=(inode->mode>>6)&7U;
    else if (cred->gid==inode->gid)
        bits=(inode->mode>>3)&7U;
    else
        bits=inode->mode&7U;
    if ((bits&mask)==mask)
        return 0;
    vfs_count(&metrics.permission_denials);
    return -SE_ACCES;
}

/* ---------------------------------------------------------- inode cache */

static uint32_t inode_hash_of(struct vfs_superblock *sb, uint64_t ino) {
    return (uint32_t)((((uint64_t)sb>>4)^(ino*0x9e3779b97f4a7c15ULL))>>32)%VFS_INODE_HASH;
}

static struct vfs_inode *inode_lookup_locked(struct vfs_superblock *sb, uint64_t ino) {
    for (struct vfs_inode *i=inode_hash[inode_hash_of(sb,ino)]; i; i=i->hash_next)
        if (i->sb==sb && i->ino==ino && !(i->flags&VFS_I_DEAD))
            return i;
    return 0;
}

static void inode_unhash_locked(struct vfs_inode *inode) {
    struct vfs_inode **cursor=&inode_hash[inode_hash_of(inode->sb,inode->ino)];
    while (*cursor && *cursor!=inode)
        cursor=&(*cursor)->hash_next;
    if (*cursor)
        *cursor=inode->hash_next;
    inode->hash_next=0;
}

static void inode_free_slot_locked(struct vfs_inode *inode) {
    inode_unhash_locked(inode);
    inode->sb=0;
    inode->ino=0;
    inode->flags=0;
    inode->refcount=0;
    inode->mapping.in_use=0;
    if (metrics.inodes_cached)
        --metrics.inodes_cached;
}

struct vfs_inode *vfs_inode_cached(struct vfs_superblock *sb, uint64_t ino) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    struct vfs_inode *inode=inode_lookup_locked(sb,ino);
    if (inode && !(inode->flags&(VFS_I_LOADING|VFS_I_BUSY)))
        ++inode->refcount;
    else
        inode=0;
    spin_unlock_irqrestore(&vfs_lock,flags);
    return inode;
}

void vfs_inode_get(struct vfs_inode *inode) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    ++inode->refcount;
    spin_unlock_irqrestore(&vfs_lock,flags);
}

/* Evict one unreferenced cached inode (writes back its data first).
 * Returns 1 if a slot was freed. */
static int inode_evict_one(void) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    struct vfs_inode *victim=0;
    for (uint32_t i=0; i<VFS_MAX_INODES; ++i) {
        struct vfs_inode *c=&inodes[i];
        if (!c->sb || c->refcount || c->mmap_count ||
            (c->flags&(VFS_I_LOADING|VFS_I_BUSY)) || c==c->sb->root)
            continue;
        if (!victim || c->lru_tick<victim->lru_tick)
            victim=c;
    }
    if (!victim) {
        spin_unlock_irqrestore(&vfs_lock,flags);
        return 0;
    }
    victim->flags|=VFS_I_BUSY;
    spin_unlock_irqrestore(&vfs_lock,flags);
    (void)pc_writeback_mapping(&victim->mapping,1,BLOCK_PRIO_NORMAL);
    int rc=pc_invalidate(&victim->mapping,0);
    if (rc==0 && victim->sb->ops->evict_inode)
        victim->sb->ops->evict_inode(victim);
    flags=spin_lock_irqsave(&vfs_lock);
    victim->flags&=~VFS_I_BUSY;
    int freed=0;
    if (rc==0 && victim->refcount==0) {
        inode_free_slot_locked(victim);
        freed=1;
    }
    spin_unlock_irqrestore(&vfs_lock,flags);
    (void)wait_queue_wake_all(&vfs_waitq);
    return freed;
}

static int vfs_iget(struct vfs_superblock *sb, uint64_t ino, struct vfs_inode **out) {
    for (uint32_t attempt=0; attempt<VFS_MAX_INODES+2U; ++attempt) {
        uint64_t flags=spin_lock_irqsave(&vfs_lock);
        struct vfs_inode *inode=inode_lookup_locked(sb,ino);
        if (inode) {
            if (inode->flags&(VFS_I_LOADING|VFS_I_BUSY)) {
                vfs_wait_locked(&flags);
                spin_unlock_irqrestore(&vfs_lock,flags);
                --attempt;
                continue;
            }
            ++inode->refcount;
            inode->lru_tick=++inode_clock;
            spin_unlock_irqrestore(&vfs_lock,flags);
            vfs_count(&metrics.inode_cache_hits);
            *out=inode;
            return 0;
        }
        struct vfs_inode *slot=0;
        for (uint32_t i=0; i<VFS_MAX_INODES && !slot; ++i)
            if (!inodes[i].sb && !(inodes[i].flags&VFS_I_BUSY))
                slot=&inodes[i];
        if (!slot) {
            spin_unlock_irqrestore(&vfs_lock,flags);
            if (!inode_evict_one())
                return -SE_NFILE;
            continue;
        }
        memset(slot,0,sizeof(*slot));
        slot->sb=sb;
        slot->ino=ino;
        slot->refcount=1;
        slot->flags=VFS_I_LOADING;
        slot->lru_tick=++inode_clock;
        uint32_t bucket=inode_hash_of(sb,ino);
        slot->hash_next=inode_hash[bucket];
        inode_hash[bucket]=slot;
        ++metrics.inodes_cached;
        spin_unlock_irqrestore(&vfs_lock,flags);
        vfs_count(&metrics.inode_cache_misses);
        kmutex_init(&slot->lock,"inode");
        pc_mapping_init(&slot->mapping,sb->device,slot);
        int rc=sb->ops->read_inode(sb,ino,slot);
        flags=spin_lock_irqsave(&vfs_lock);
        if (rc) {
            inode_free_slot_locked(slot);
        } else {
            slot->flags=(slot->flags&~VFS_I_LOADING)|VFS_I_VALID;
        }
        spin_unlock_irqrestore(&vfs_lock,flags);
        (void)wait_queue_wake_all(&vfs_waitq);
        if (rc)
            return rc;
        *out=slot;
        return 0;
    }
    return -SE_NFILE;
}

void vfs_inode_put(struct vfs_inode *inode) {
    if (!inode)
        return;
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    if (inode->refcount==0) {
        spin_unlock_irqrestore(&vfs_lock,flags);
        klog("ZEROOS: vfs: inode %llu reference underflow.",inode->ino);
        return;
    }
    if (--inode->refcount || inode->links || (inode->flags&VFS_I_DEAD) ||
        inode->mmap_count) {
        spin_unlock_irqrestore(&vfs_lock,flags);
        return;
    }
    /* Last reference to an unlinked inode: destroy it. */
    inode->flags|=VFS_I_BUSY;
    spin_unlock_irqrestore(&vfs_lock,flags);
    inode->sb->ops->release_inode(inode);
    flags=spin_lock_irqsave(&vfs_lock);
    inode->flags&=~VFS_I_BUSY;
    if (inode->refcount==0)
        inode_free_slot_locked(inode);
    spin_unlock_irqrestore(&vfs_lock,flags);
    (void)wait_queue_wake_all(&vfs_waitq);
}

/* ---------------------------------------------------------- dentry cache */

static int dcache_lookup(struct vfs_superblock *sb, uint64_t parent, const char *name,
                         uint32_t length, uint64_t *ino) {
    if (length>VFS_DNAME_MAX)
        return 0;
    int found=0;
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    for (uint32_t i=0; i<VFS_DCACHE_SIZE; ++i) {
        struct vfs_dentry *d=&dcache[i];
        if (d->sb==sb && d->parent==parent && d->length==length &&
            memcmp(d->name,name,length)==0) {
            *ino=d->ino;
            d->tick=++inode_clock;
            found=1;
            break;
        }
    }
    spin_unlock_irqrestore(&vfs_lock,flags);
    vfs_count(found ? &metrics.dcache_hits : &metrics.dcache_misses);
    return found;
}

static void dcache_insert(struct vfs_superblock *sb, uint64_t parent, const char *name,
                          uint32_t length, uint64_t ino) {
    if (length>VFS_DNAME_MAX)
        return;
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    struct vfs_dentry *victim=&dcache[0];
    for (uint32_t i=0; i<VFS_DCACHE_SIZE; ++i) {
        if (!dcache[i].sb) {
            victim=&dcache[i];
            break;
        }
        if (dcache[i].tick<victim->tick)
            victim=&dcache[i];
    }
    victim->sb=sb;
    victim->parent=parent;
    victim->ino=ino;
    victim->length=length;
    memcpy(victim->name,name,length);
    victim->name[length]=0;
    victim->tick=++inode_clock;
    spin_unlock_irqrestore(&vfs_lock,flags);
}

/* Invalidate by name (namespace change) or everything of `sb` (name=0). */
static void dcache_invalidate(struct vfs_superblock *sb, uint64_t parent,
                              const char *name, uint32_t length) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    for (uint32_t i=0; i<VFS_DCACHE_SIZE; ++i) {
        struct vfs_dentry *d=&dcache[i];
        if (d->sb!=sb)
            continue;
        if (!name || (d->parent==parent && d->length==length &&
                      memcmp(d->name,name,length)==0))
            memset(d,0,sizeof(*d));
    }
    spin_unlock_irqrestore(&vfs_lock,flags);
}

/* -------------------------------------------------------- path handling */

struct vfs_path {
    struct vfs_superblock *sb;
    struct vfs_inode *parent;       /* referenced (want_parent) */
    struct vfs_inode *inode;        /* referenced, 0 if final missing */
    char name[VFS_NAME_MAX + 1U];
    uint32_t length;
};

static int vfs_find_mount(const char *path, uint32_t *consumed, struct vfs_superblock **sb) {
    /* "/<name>" mounts first, then "/". */
    const char *p=path+1;
    uint32_t len=0;
    while (p[len] && p[len]!='/')
        ++len;
    struct vfs_superblock *root=0;
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    for (uint32_t i=0; i<VFS_MAX_MOUNTS; ++i) {
        if (!mounts[i].in_use || (mounts[i].sb->flags&VFS_SB_DYING))
            continue;
        uint32_t mlen=(uint32_t)kstrnlen(mounts[i].name,sizeof(mounts[i].name));
        if (mlen==0) {
            root=mounts[i].sb;
        } else if (mlen==len && len && memcmp(mounts[i].name,p,len)==0) {
            *sb=mounts[i].sb;
            *consumed=1U+len;
            spin_unlock_irqrestore(&vfs_lock,flags);
            return 0;
        }
    }
    spin_unlock_irqrestore(&vfs_lock,flags);
    if (!root)
        return -SE_NOENT;
    *sb=root;
    *consumed=1;
    return 0;
}

static int vfs_lookup_child(struct vfs_inode *dir, const char *name, uint32_t length,
                            struct vfs_inode **out) {
    uint64_t ino;
    vfs_count(&metrics.lookups);
    if (!dcache_lookup(dir->sb,dir->ino,name,length,&ino)) {
        int rc=dir->sb->ops->lookup(dir,name,length,&ino);
        if (rc)
            return rc;
        dcache_insert(dir->sb,dir->ino,name,length,ino);
    }
    int rc=vfs_iget(dir->sb,ino,out);
    if (rc==-SE_STALE)
        dcache_invalidate(dir->sb,dir->ino,name,length);
    return rc;
}

/* Resolve `path`. want_parent: stop at the last component (returned in
 * name, parent referenced; inode referenced if it exists). */
static int vfs_resolve(const struct vfs_cred *cred, const char *path, int want_parent,
                       struct vfs_path *out) {
    memset(out,0,sizeof(*out));
    if (!path || path[0]!='/')
        return -SE_INVAL;
    uint64_t path_len=kstrnlen(path,VFS_PATH_MAX);
    if (path_len>=VFS_PATH_MAX)
        return -SE_NAMETOOLONG;
    uint32_t consumed;
    struct vfs_superblock *sb;
    int rc=vfs_find_mount(path,&consumed,&sb);
    if (rc)
        return rc;
    out->sb=sb;
    struct vfs_inode *current=sb->root;
    vfs_inode_get(current);
    const char *p=path+consumed;
    for (;;) {
        while (*p=='/')
            ++p;
        if (!*p) {
            /* Path ends at a directory. */
            if (want_parent) {
                /* No final component ("/" or "/mnt"): parent is itself. */
                out->parent=current;
                vfs_inode_get(current);
                out->inode=current;
                out->length=0;
                return 0;
            }
            out->inode=current;
            return 0;
        }
        const char *start=p;
        uint32_t len=0;
        while (p[len] && p[len]!='/')
            ++len;
        if (len>VFS_NAME_MAX) {
            vfs_inode_put(current);
            return -SE_NAMETOOLONG;
        }
        const char *next=p+len;
        while (*next=='/')
            ++next;
        int last=*next==0;
        if (!VFS_S_ISDIR(current->mode)) {
            vfs_inode_put(current);
            return -SE_NOTDIR;
        }
        rc=vfs_permission(cred,current,VFS_MAY_EXEC);
        if (rc) {
            vfs_inode_put(current);
            return rc;
        }
        if (last && want_parent) {
            memcpy(out->name,start,len);
            out->name[len]=0;
            out->length=len;
            out->parent=current;
            if (len==1 && start[0]=='.')
                return -SE_INVAL;
            if (len==2 && start[0]=='.' && start[1]=='.')
                return -SE_INVAL;
            rc=vfs_lookup_child(current,start,len,&out->inode);
            if (rc==-SE_NOENT) {
                out->inode=0;
                return 0;
            }
            if (rc) {
                out->inode=0;
                return rc;
            }
            return 0;
        }
        struct vfs_inode *child;
        if (len==1 && start[0]=='.') {
            child=current;
            vfs_inode_get(child);
        } else if (len==2 && start[0]=='.' && start[1]=='.') {
            uint64_t parent=current->fs_parent ? current->fs_parent : current->ino;
            if (current==sb->root)
                parent=current->ino;       /* never escape the mount */
            rc=vfs_iget(sb,parent,&child);
            if (rc) {
                vfs_inode_put(current);
                return rc;
            }
        } else {
            rc=vfs_lookup_child(current,start,len,&child);
            if (rc) {
                vfs_inode_put(current);
                return rc;
            }
        }
        vfs_inode_put(current);
        current=child;
        p=next;
    }
}

static void vfs_path_release(struct vfs_path *path) {
    if (path->inode)
        vfs_inode_put(path->inode);
    if (path->parent)
        vfs_inode_put(path->parent);
    path->inode=path->parent=0;
}

static void vfs_fill_stat(struct vfs_inode *inode, struct vfs_stat *out) {
    memset(out,0,sizeof(*out));
    out->ino=inode->ino;
    out->size=inode->size;
    out->blocks=inode->blocks*8U;
    out->mode=inode->mode;
    out->links=inode->links;
    out->uid=inode->uid;
    out->gid=inode->gid;
    out->atime_ns=inode->atime_ns;
    out->mtime_ns=inode->mtime_ns;
    out->ctime_ns=inode->ctime_ns;
    out->dev=inode->sb->dev_id;
    out->block_size=4096;
}

static int vfs_sb_writable(struct vfs_superblock *sb) {
    if (sb->flags&VFS_SB_ERROR)
        return -SE_IO;
    if (sb->flags&VFS_SB_RDONLY)
        return -SE_ROFS;
    return 0;
}

/* ---------------------------------------------------------------- files */

static struct vfs_file *file_alloc(void) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    for (uint32_t i=0; i<VFS_MAX_FILES; ++i)
        if (!files[i].in_use) {
            memset(&files[i],0,sizeof(files[i]));
            files[i].in_use=1;
            files[i].refcount=1;
            ++metrics.files_open;
            spin_unlock_irqrestore(&vfs_lock,flags);
            kmutex_init(&files[i].pos_lock,"file-pos");
            return &files[i];
        }
    spin_unlock_irqrestore(&vfs_lock,flags);
    return 0;
}

void vfs_file_get(struct vfs_file *file) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    ++file->refcount;
    spin_unlock_irqrestore(&vfs_lock,flags);
}

void vfs_file_put(struct vfs_file *file) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    if (file->refcount==0 || --file->refcount) {
        spin_unlock_irqrestore(&vfs_lock,flags);
        return;
    }
    struct vfs_inode *inode=file->inode;
    struct vfs_superblock *sb=inode ? inode->sb : 0;
    file->inode=0;
    file->in_use=0;
    if (metrics.files_open)
        --metrics.files_open;
    if (sb && sb->refcount)
        --sb->refcount;
    spin_unlock_irqrestore(&vfs_lock,flags);
    vfs_count(&metrics.closes);
    vfs_inode_put(inode);
}

int vfs_open(const struct vfs_cred *cred, const char *path, uint32_t flags,
             uint32_t mode, struct vfs_file **out) {
    if ((flags&~VFS_O_VALID) || (flags&VFS_O_ACCMODE)==VFS_O_ACCMODE)
        return -SE_INVAL;
    uint32_t access=flags&VFS_O_ACCMODE;
    int writing=access!=VFS_O_RDONLY;
    struct vfs_path p;
    struct vfs_inode *inode=0;
    int created=0;
    int rc;
    if (flags&VFS_O_CREAT) {
        kmutex_lock(&vfs_ns_lock);
        rc=vfs_resolve(cred,path,1,&p);
        if (rc==0 && p.length==0)
            rc=-SE_ISDIR;
        if (rc==0 && p.inode && (flags&VFS_O_EXCL))
            rc=-SE_EXIST;
        if (rc==0 && !p.inode) {
            rc=vfs_sb_writable(p.sb);
            if (rc==0)
                rc=vfs_permission(cred,p.parent,VFS_MAY_WRITE|VFS_MAY_EXEC);
            uint64_t ino=0;
            if (rc==0)
                rc=p.sb->ops->create(p.parent,p.name,p.length,
                                     VFS_S_IFREG|(mode&0777U),cred->uid,cred->gid,&ino);
            if (rc==0) {
                dcache_insert(p.sb,p.parent->ino,p.name,p.length,ino);
                rc=vfs_iget(p.sb,ino,&p.inode);
                if (rc)
                    p.inode=0;
            }
            /* The creator gets the requested access even if `mode`
             * lacks the corresponding bits (POSIX). */
            if (rc==0)
                created=1;
        } else if (rc==0) {
            if (VFS_S_ISDIR(p.inode->mode) && writing)
                rc=-SE_ISDIR;
        }
        if (rc==0) {
            inode=p.inode;
            p.inode=0;
        }
        vfs_path_release(&p);
        kmutex_unlock(&vfs_ns_lock);
        if (rc)
            return rc;
    } else {
        rc=vfs_resolve(cred,path,0,&p);
        if (rc)
            return rc;
        inode=p.inode;
    }
    if (VFS_S_ISDIR(inode->mode) && writing)
        rc=-SE_ISDIR;
    if (rc==0 && (flags&VFS_O_DIRECTORY) && !VFS_S_ISDIR(inode->mode))
        rc=-SE_NOTDIR;
    if (rc==0 && writing)
        rc=vfs_sb_writable(inode->sb);
    if (rc==0 && access!=VFS_O_WRONLY && !created)
        rc=vfs_permission(cred,inode,VFS_MAY_READ);
    if (rc==0 && writing && !created)
        rc=vfs_permission(cred,inode,VFS_MAY_WRITE);
    if (rc==0 && (flags&VFS_O_TRUNC) && writing && VFS_S_ISREG(inode->mode) &&
        inode->size) {
        kmutex_lock(&inode->lock);
        rc=inode->sb->ops->set_size(inode,0);
        kmutex_unlock(&inode->lock);
    }
    struct vfs_file *file=0;
    if (rc==0) {
        file=file_alloc();
        if (!file)
            rc=-SE_NFILE;
    }
    if (rc) {
        vfs_inode_put(inode);
        return rc;
    }
    file->inode=inode;
    file->flags=flags&(VFS_O_ACCMODE|VFS_O_APPEND);
    file->wb_error_seen=inode->mapping.error_seq;
    uint64_t lf=spin_lock_irqsave(&vfs_lock);
    ++inode->sb->refcount;
    spin_unlock_irqrestore(&vfs_lock,lf);
    vfs_count(&metrics.opens);
    *out=file;
    return 0;
}

/* -------------------------------------------------------------- data I/O */

static int64_t vfs_do_read(struct vfs_file *file, uint8_t *buffer, uint64_t length,
                           uint64_t offset) {
    struct vfs_inode *inode=file->inode;
    if (VFS_S_ISDIR(inode->mode))
        return -SE_ISDIR;
    if ((file->flags&VFS_O_ACCMODE)==VFS_O_WRONLY)
        return -SE_BADF;
    kmutex_lock(&inode->lock);
    uint64_t size=inode->size;
    if (offset>=size || length==0) {
        kmutex_unlock(&inode->lock);
        return 0;
    }
    if (length>size-offset)
        length=size-offset;
    uint64_t done=0;
    int rc=0;
    while (done<length) {
        uint64_t position=offset+done;
        uint64_t index=position/4096U;
        uint32_t in_page=(uint32_t)(position%4096U);
        uint64_t chunk=4096U-in_page;
        if (chunk>length-done)
            chunk=length-done;
        struct pc_page *page=pc_find(&inode->mapping,index);
        if (!page) {
            uint64_t block;
            int is_new;
            rc=inode->sb->ops->map_page(inode,index,0,&block,&is_new);
            if (rc==0)
                rc=pc_get(&inode->mapping,index,block,1,BLOCK_PRIO_FOREGROUND,&page);
            if (rc)
                break;
        }
        memcpy(buffer+done,pc_data(page)+in_page,chunk);
        pc_put(page);
        done+=chunk;
    }
    /* relatime: persist atime only when it is older than mtime/ctime. */
    if (done && inode->atime_ns<=inode->mtime_ns && vfs_sb_writable(inode->sb)==0) {
        inode->atime_ns=vfs_now_ns();
        (void)inode->sb->ops->update_inode(inode);
    }
    kmutex_unlock(&inode->lock);
    vfs_count(&metrics.reads);
    __atomic_fetch_add(&metrics.read_bytes,done,__ATOMIC_RELAXED);
    if (done)
        return (int64_t)done;
    return rc;
}

static int64_t vfs_do_write(struct vfs_file *file, const uint8_t *buffer, uint64_t length,
                            uint64_t offset, int append, uint64_t *end_out) {
    struct vfs_inode *inode=file->inode;
    if (VFS_S_ISDIR(inode->mode))
        return -SE_ISDIR;
    if ((file->flags&VFS_O_ACCMODE)==VFS_O_RDONLY)
        return -SE_BADF;
    int rc=vfs_sb_writable(inode->sb);
    if (rc)
        return rc;
    kmutex_lock(&inode->lock);
    if (append)
        offset=inode->size;
    uint64_t max=ZJ_MAX_FILE_BLOCKS*4096ULL;
    if (offset>=max) {
        kmutex_unlock(&inode->lock);
        return -SE_FBIG;
    }
    if (length>max-offset)
        length=max-offset;
    uint64_t done=0;
    while (done<length) {
        uint64_t position=offset+done;
        uint64_t index=position/4096U;
        uint32_t in_page=(uint32_t)(position%4096U);
        uint64_t chunk=4096U-in_page;
        if (chunk>length-done)
            chunk=length-done;
        uint64_t block;
        int is_new;
        rc=inode->sb->ops->map_page(inode,index,1,&block,&is_new);
        if (rc)
            break;
        int partial=chunk!=4096U;
        int fill=!is_new && partial && index*4096U<inode->size;
        struct pc_page *page;
        rc=pc_get(&inode->mapping,index,block,fill,BLOCK_PRIO_NORMAL,&page);
        if (rc)
            break;
        memcpy(pc_data(page)+in_page,buffer+done,chunk);
        pc_mark_dirty(page,block);
        pc_put(page);
        done+=chunk;
    }
    if (done) {
        uint64_t end=offset+done;
        if (end>inode->size)
            inode->size=end;
        inode->mtime_ns=inode->ctime_ns=vfs_now_ns();
        int urc=inode->sb->ops->update_inode(inode);
        if (urc && !rc)
            rc=urc;
        if (end_out)
            *end_out=end;
    }
    kmutex_unlock(&inode->lock);
    pc_balance_dirty(&inode->mapping);
    vfs_count(&metrics.writes);
    __atomic_fetch_add(&metrics.write_bytes,done,__ATOMIC_RELAXED);
    if (done)
        return (int64_t)done;            /* short write on ENOSPC/EIO */
    return rc;
}

int64_t vfs_read(struct vfs_file *file, void *buffer, uint64_t length) {
    kmutex_lock(&file->pos_lock);
    int64_t rc=vfs_do_read(file,(uint8_t *)buffer,length,file->offset);
    if (rc>0)
        file->offset+=(uint64_t)rc;
    kmutex_unlock(&file->pos_lock);
    return rc;
}

int64_t vfs_write(struct vfs_file *file, const void *buffer, uint64_t length) {
    kmutex_lock(&file->pos_lock);
    uint64_t end=0;
    int64_t rc=vfs_do_write(file,(const uint8_t *)buffer,length,file->offset,
                            (file->flags&VFS_O_APPEND)!=0,&end);
    if (rc>0)
        file->offset=end;
    kmutex_unlock(&file->pos_lock);
    return rc;
}

int64_t vfs_pread(struct vfs_file *file, void *buffer, uint64_t length, uint64_t offset) {
    if ((int64_t)offset<0)
        return -SE_INVAL;
    return vfs_do_read(file,(uint8_t *)buffer,length,offset);
}

int64_t vfs_pwrite(struct vfs_file *file, const void *buffer, uint64_t length,
                   uint64_t offset) {
    if ((int64_t)offset<0)
        return -SE_INVAL;
    return vfs_do_write(file,(const uint8_t *)buffer,length,offset,0,0);
}

int64_t vfs_seek(struct vfs_file *file, int64_t offset, int whence) {
    kmutex_lock(&file->pos_lock);
    int64_t base;
    switch (whence) {
    case VFS_SEEK_SET: base=0; break;
    case VFS_SEEK_CUR: base=(int64_t)file->offset; break;
    case VFS_SEEK_END: base=(int64_t)file->inode->size; break;
    default:
        kmutex_unlock(&file->pos_lock);
        return -SE_INVAL;
    }
    if ((offset>0 && base>VFS_INT64_MAX-offset)) {
        kmutex_unlock(&file->pos_lock);
        return -SE_OVERFLOW;
    }
    int64_t target=base+offset;
    if (target<0) {
        kmutex_unlock(&file->pos_lock);
        return -SE_INVAL;
    }
    if (VFS_S_ISDIR(file->inode->mode)) {
        if (target!=0) {
            kmutex_unlock(&file->pos_lock);
            return -SE_INVAL;
        }
        file->dir_cookie=0;
    }
    file->offset=(uint64_t)target;
    kmutex_unlock(&file->pos_lock);
    return target;
}

int vfs_fstat(struct vfs_file *file, struct vfs_stat *out) {
    vfs_fill_stat(file->inode,out);
    return 0;
}

int vfs_fsync(struct vfs_file *file, int data_only) {
    struct vfs_inode *inode=file->inode;
    vfs_count(&metrics.fsyncs);
    int rc=pc_writeback_mapping(&inode->mapping,1,BLOCK_PRIO_FOREGROUND);
    int frc=inode->sb->ops->fsync(inode,data_only);
    /* errseq: every file description observes each writeback error
     * (including errors hit by background writeback) exactly once. */
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    uint32_t seq=inode->mapping.error_seq;
    int mapping_error=inode->mapping.error;
    int unseen=seq!=file->wb_error_seen;
    file->wb_error_seen=seq;
    spin_unlock_irqrestore(&vfs_lock,flags);
    if (!rc && unseen)
        rc=mapping_error ? mapping_error : -SE_IO;
    if (!rc)
        rc=frc;
    if (rc)
        vfs_count(&metrics.fsync_errors);
    return rc;
}

int vfs_ftruncate(struct vfs_file *file, uint64_t size) {
    struct vfs_inode *inode=file->inode;
    if (!VFS_S_ISREG(inode->mode))
        return -SE_INVAL;
    if ((file->flags&VFS_O_ACCMODE)==VFS_O_RDONLY)
        return -SE_BADF;
    if ((int64_t)size<0)
        return -SE_INVAL;
    int rc=vfs_sb_writable(inode->sb);
    if (rc)
        return rc;
    kmutex_lock(&inode->lock);
    rc=inode->sb->ops->set_size(inode,size);
    kmutex_unlock(&inode->lock);
    return rc;
}

int vfs_readdir(struct vfs_file *file, struct vfs_dirent *out) {
    struct vfs_inode *inode=file->inode;
    if (!VFS_S_ISDIR(inode->mode))
        return -SE_NOTDIR;
    kmutex_lock(&file->pos_lock);
    int rc=inode->sb->ops->readdir(inode,&file->dir_cookie,out);
    kmutex_unlock(&file->pos_lock);
    return rc;
}

/* ----------------------------------------------------- namespace changes */

int vfs_mkdir(const struct vfs_cred *cred, const char *path, uint32_t mode) {
    kmutex_lock(&vfs_ns_lock);
    struct vfs_path p;
    int rc=vfs_resolve(cred,path,1,&p);
    if (rc==0 && (p.inode || p.length==0))
        rc=-SE_EXIST;
    if (rc==0)
        rc=vfs_sb_writable(p.sb);
    if (rc==0)
        rc=vfs_permission(cred,p.parent,VFS_MAY_WRITE|VFS_MAY_EXEC);
    uint64_t ino;
    if (rc==0)
        rc=p.sb->ops->create(p.parent,p.name,p.length,VFS_S_IFDIR|(mode&0777U),
                             cred->uid,cred->gid,&ino);
    if (rc==0)
        dcache_insert(p.sb,p.parent->ino,p.name,p.length,ino);
    vfs_path_release(&p);
    kmutex_unlock(&vfs_ns_lock);
    return rc;
}

static int vfs_remove(const struct vfs_cred *cred, const char *path, int directory) {
    kmutex_lock(&vfs_ns_lock);
    struct vfs_path p;
    int rc=vfs_resolve(cred,path,1,&p);
    if (rc==0 && p.length==0)
        rc=directory ? -SE_BUSY : -SE_ISDIR;      /* mount root */
    if (rc==0 && !p.inode)
        rc=-SE_NOENT;
    if (rc==0 && directory && !VFS_S_ISDIR(p.inode->mode))
        rc=-SE_NOTDIR;
    if (rc==0 && !directory && VFS_S_ISDIR(p.inode->mode))
        rc=-SE_ISDIR;
    if (rc==0 && p.inode->ino==2U && p.parent==p.sb->root && directory)
        rc=-SE_PERM;                             /* keep lost+found */
    if (rc==0)
        rc=vfs_sb_writable(p.sb);
    if (rc==0)
        rc=vfs_permission(cred,p.parent,VFS_MAY_WRITE|VFS_MAY_EXEC);
    if (rc==0)
        rc=p.sb->ops->unlink(p.parent,p.name,p.length,p.inode,directory);
    if (rc==0)
        dcache_invalidate(p.sb,p.parent->ino,p.name,p.length);
    vfs_path_release(&p);           /* may destroy the inode (last ref) */
    kmutex_unlock(&vfs_ns_lock);
    return rc;
}

int vfs_unlink(const struct vfs_cred *cred, const char *path) {
    return vfs_remove(cred,path,0);
}

int vfs_rmdir(const struct vfs_cred *cred, const char *path) {
    return vfs_remove(cred,path,1);
}

int vfs_rename(const struct vfs_cred *cred, const char *old_path, const char *new_path) {
    kmutex_lock(&vfs_ns_lock);
    struct vfs_path from, to;
    int rc=vfs_resolve(cred,old_path,1,&from);
    if (rc) {
        vfs_path_release(&from);
        kmutex_unlock(&vfs_ns_lock);
        return rc;
    }
    rc=vfs_resolve(cred,new_path,1,&to);
    if (rc==0 && (from.length==0 || to.length==0))
        rc=-SE_BUSY;
    if (rc==0 && !from.inode)
        rc=-SE_NOENT;
    if (rc==0 && from.sb!=to.sb)
        rc=-SE_XDEV;
    if (rc==0)
        rc=vfs_sb_writable(from.sb);
    if (rc==0)
        rc=vfs_permission(cred,from.parent,VFS_MAY_WRITE|VFS_MAY_EXEC);
    if (rc==0)
        rc=vfs_permission(cred,to.parent,VFS_MAY_WRITE|VFS_MAY_EXEC);
    if (rc==0 && to.inode==from.inode) {
        /* Same inode (same entry or hard links): POSIX no-op. */
        vfs_path_release(&from);
        vfs_path_release(&to);
        kmutex_unlock(&vfs_ns_lock);
        return 0;
    }
    int directory=rc==0 && VFS_S_ISDIR(from.inode->mode);
    if (rc==0 && to.inode) {
        if (directory && !VFS_S_ISDIR(to.inode->mode))
            rc=-SE_NOTDIR;
        else if (!directory && VFS_S_ISDIR(to.inode->mode))
            rc=-SE_ISDIR;
    }
    if (rc==0 && directory) {
        /* The destination parent must not be the moved directory or one
         * of its descendants (walk parent pointers up to the root). */
        struct vfs_inode *cursor=to.parent;
        vfs_inode_get(cursor);
        for (uint32_t depth=0; depth<4096U; ++depth) {
            if (cursor->ino==from.inode->ino) {
                rc=-SE_INVAL;
                break;
            }
            if (cursor==from.sb->root || cursor->fs_parent==0 ||
                cursor->fs_parent==cursor->ino)
                break;
            struct vfs_inode *up;
            int grc=vfs_iget(from.sb,cursor->fs_parent,&up);
            vfs_inode_put(cursor);
            if (grc) {
                rc=grc;
                cursor=0;
                break;
            }
            cursor=up;
        }
        if (cursor)
            vfs_inode_put(cursor);
    }
    if (rc==0)
        rc=from.sb->ops->rename(from.parent,from.name,from.length,to.parent,to.name,
                                to.length,from.inode,to.inode);
    if (rc==0) {
        dcache_invalidate(from.sb,from.parent->ino,from.name,from.length);
        dcache_invalidate(to.sb,to.parent->ino,to.name,to.length);
    }
    vfs_path_release(&from);
    vfs_path_release(&to);
    kmutex_unlock(&vfs_ns_lock);
    return rc;
}

int vfs_link(const struct vfs_cred *cred, const char *old_path, const char *new_path) {
    kmutex_lock(&vfs_ns_lock);
    struct vfs_path from, to;
    int rc=vfs_resolve(cred,old_path,0,&from);
    if (rc) {
        kmutex_unlock(&vfs_ns_lock);
        return rc;
    }
    rc=vfs_resolve(cred,new_path,1,&to);
    if (rc==0 && VFS_S_ISDIR(from.inode->mode))
        rc=-SE_PERM;
    if (rc==0 && (to.inode || to.length==0))
        rc=-SE_EXIST;
    if (rc==0 && from.sb!=to.sb)
        rc=-SE_XDEV;
    if (rc==0)
        rc=vfs_sb_writable(to.sb);
    if (rc==0)
        rc=vfs_permission(cred,to.parent,VFS_MAY_WRITE|VFS_MAY_EXEC);
    if (rc==0)
        rc=to.sb->ops->link(to.parent,to.name,to.length,from.inode);
    if (rc==0)
        dcache_insert(to.sb,to.parent->ino,to.name,to.length,from.inode->ino);
    vfs_path_release(&from);
    vfs_path_release(&to);
    kmutex_unlock(&vfs_ns_lock);
    return rc;
}

int vfs_stat(const struct vfs_cred *cred, const char *path, struct vfs_stat *out) {
    struct vfs_path p;
    int rc=vfs_resolve(cred,path,0,&p);
    if (rc)
        return rc;
    vfs_fill_stat(p.inode,out);
    vfs_path_release(&p);
    return 0;
}

int vfs_statfs(const char *path, struct vfs_statfs *out) {
    static const struct vfs_cred root={0,0};
    struct vfs_path p;
    int rc=vfs_resolve(&root,path,0,&p);
    if (rc)
        return rc;
    rc=p.sb->ops->statfs(p.sb,out);
    vfs_path_release(&p);
    return rc;
}

int vfs_chmod(const struct vfs_cred *cred, const char *path, uint32_t mode) {
    struct vfs_path p;
    int rc=vfs_resolve(cred,path,0,&p);
    if (rc)
        return rc;
    struct vfs_inode *inode=p.inode;
    if (cred->uid!=0 && cred->uid!=inode->uid)
        rc=-SE_PERM;
    if (rc==0)
        rc=vfs_sb_writable(inode->sb);
    if (rc==0) {
        kmutex_lock(&inode->lock);
        inode->mode=(inode->mode&VFS_S_IFMT)|(mode&07777U);
        inode->ctime_ns=vfs_now_ns();
        rc=inode->sb->ops->update_inode(inode);
        kmutex_unlock(&inode->lock);
    }
    vfs_path_release(&p);
    return rc;
}

int vfs_chown(const struct vfs_cred *cred, const char *path, uint32_t uid, uint32_t gid) {
    struct vfs_path p;
    int rc=vfs_resolve(cred,path,0,&p);
    if (rc)
        return rc;
    struct vfs_inode *inode=p.inode;
    if (cred->uid!=0)
        rc=-SE_PERM;
    if (rc==0)
        rc=vfs_sb_writable(inode->sb);
    if (rc==0) {
        kmutex_lock(&inode->lock);
        inode->uid=uid;
        inode->gid=gid;
        inode->mode&=~06000U;              /* drop setuid/setgid */
        inode->ctime_ns=vfs_now_ns();
        rc=inode->sb->ops->update_inode(inode);
        kmutex_unlock(&inode->lock);
    }
    vfs_path_release(&p);
    return rc;
}

/* --------------------------------------------------------------- mounts */

int vfs_mount(struct block_device *device, const char *path, uint32_t flags) {
    if (!path || path[0]!='/')
        return -SE_INVAL;
    const char *name=path+1;
    uint64_t len=kstrnlen(name,32);
    if (len>=31U)
        return -SE_NAMETOOLONG;
    for (uint64_t i=0; i<len; ++i)
        if (name[i]=='/')
            return -SE_INVAL;
    kmutex_lock(&vfs_mount_lock);
    int slot=-1;
    for (uint32_t i=0; i<VFS_MAX_MOUNTS; ++i) {
        if (mounts[i].in_use) {
            if (mounts[i].sb->device==device ||
                kstrneq(mounts[i].name,name,32)) {
                kmutex_unlock(&vfs_mount_lock);
                return -SE_BUSY;
            }
        } else if (slot<0) {
            slot=(int)i;
        }
    }
    if (slot<0) {
        kmutex_unlock(&vfs_mount_lock);
        return -SE_NFILE;
    }
    int rc=block_open(device);
    if (rc) {
        kmutex_unlock(&vfs_mount_lock);
        return rc;
    }
    struct vfs_superblock *sb=&superblocks[slot];
    memset(sb,0,sizeof(*sb));
    sb->dev_id=(uint32_t)slot+1U;
    sb->flags=flags&VFS_SB_RDONLY;
    rc=zjfs_mount(device,flags,sb);
    if (rc==0) {
        sb->in_use=1;
        rc=vfs_iget(sb,ZJ_ROOT_INO,&sb->root);
        if (rc==0 && !VFS_S_ISDIR(sb->root->mode)) {
            vfs_inode_put(sb->root);
            rc=-SE_UCLEAN;
        }
        if (rc) {
            sb->root=0;
            (void)sb->ops->unmount(sb);
            sb->in_use=0;
        }
    }
    if (rc) {
        block_close(device);
        kmutex_unlock(&vfs_mount_lock);
        klog("ZEROOS: vfs: mount of %s at %s failed (%d).",device->name,path,rc);
        return rc;
    }
    memset(mounts[slot].name,0,sizeof(mounts[slot].name));
    memcpy(mounts[slot].name,name,len);
    mounts[slot].sb=sb;
    uint64_t lf=spin_lock_irqsave(&vfs_lock);
    mounts[slot].in_use=1;
    spin_unlock_irqrestore(&vfs_lock,lf);
    kmutex_unlock(&vfs_mount_lock);
    klog("ZEROOS: vfs: mounted %s (%s) at %s%s.",device->name,sb->fs_name,path,
         (sb->flags&VFS_SB_RDONLY) ? " read-only" : "");
    return 0;
}

/* Write back every cached inode of `sb`; returns first error. */
static int vfs_writeback_sb(struct vfs_superblock *sb) {
    int first=0;
    for (uint32_t i=0; i<VFS_MAX_INODES; ++i) {
        struct vfs_inode *inode=&inodes[i];
        uint64_t flags=spin_lock_irqsave(&vfs_lock);
        if (inode->sb!=sb || (inode->flags&(VFS_I_BUSY|VFS_I_LOADING|VFS_I_DEAD))) {
            spin_unlock_irqrestore(&vfs_lock,flags);
            continue;
        }
        ++inode->refcount;
        spin_unlock_irqrestore(&vfs_lock,flags);
        int rc=pc_writeback_mapping(&inode->mapping,1,BLOCK_PRIO_NORMAL);
        /* Surface errors that background writeback hit earlier too. */
        if (!rc && inode->mapping.error && inode->mapping.ndirty)
            rc=inode->mapping.error;
        if (rc && !first)
            first=rc;
        vfs_inode_put(inode);
    }
    return first;
}

int vfs_sync_all(void) {
    int first=0;
    kmutex_lock(&vfs_mount_lock);
    for (uint32_t i=0; i<VFS_MAX_MOUNTS; ++i) {
        if (!mounts[i].in_use)
            continue;
        struct vfs_superblock *sb=mounts[i].sb;
        int rc=vfs_writeback_sb(sb);
        int src=sb->ops->sync(sb);
        if (!rc)
            rc=src;
        if (rc && !first)
            first=rc;
    }
    kmutex_unlock(&vfs_mount_lock);
    return first;
}

int vfs_unmount(const char *path, int force) {
    if (!path || path[0]!='/')
        return -SE_INVAL;
    const char *name=path+1;
    kmutex_lock(&vfs_mount_lock);
    int slot=-1;
    for (uint32_t i=0; i<VFS_MAX_MOUNTS; ++i)
        if (mounts[i].in_use && kstrneq(mounts[i].name,name,32))
            slot=(int)i;
    if (slot<0) {
        kmutex_unlock(&vfs_mount_lock);
        return -SE_NOENT;
    }
    struct vfs_superblock *sb=mounts[slot].sb;
    uint64_t lf=spin_lock_irqsave(&vfs_lock);
    int busy=sb->refcount!=0;
    for (uint32_t i=0; i<VFS_MAX_INODES; ++i)
        if (inodes[i].sb==sb && inodes[i].mmap_count)
            busy=1;
    if (!busy || force)
        sb->flags|=VFS_SB_DYING;           /* no new lookups */
    spin_unlock_irqrestore(&vfs_lock,lf);
    if (busy && !force) {
        kmutex_unlock(&vfs_mount_lock);
        return -SE_BUSY;
    }
    int rc=vfs_writeback_sb(sb);
    if (rc && !force) {
        lf=spin_lock_irqsave(&vfs_lock);
        sb->flags&=~VFS_SB_DYING;
        spin_unlock_irqrestore(&vfs_lock,lf);
        kmutex_unlock(&vfs_mount_lock);
        klog("ZEROOS: vfs: unmount %s refused: dirty data write-back failed (%d).",path,rc);
        return rc;
    }
    /* Drop the root and every unreferenced cached inode. */
    struct vfs_inode *root=sb->root;
    sb->root=0;
    vfs_inode_put(root);
    int still_busy=0;
    for (uint32_t i=0; i<VFS_MAX_INODES; ++i) {
        lf=spin_lock_irqsave(&vfs_lock);
        struct vfs_inode *inode=&inodes[i];
        if (inode->sb!=sb) {
            spin_unlock_irqrestore(&vfs_lock,lf);
            continue;
        }
        if (inode->refcount || (inode->flags&(VFS_I_BUSY|VFS_I_LOADING))) {
            still_busy=1;
            spin_unlock_irqrestore(&vfs_lock,lf);
            continue;
        }
        inode->flags|=VFS_I_BUSY;
        spin_unlock_irqrestore(&vfs_lock,lf);
        (void)pc_invalidate(&inode->mapping,0);
        if (sb->ops->evict_inode)
            sb->ops->evict_inode(inode);
        lf=spin_lock_irqsave(&vfs_lock);
        inode->flags&=~VFS_I_BUSY;
        inode_free_slot_locked(inode);
        spin_unlock_irqrestore(&vfs_lock,lf);
    }
    if (still_busy)
        klog("ZEROOS: vfs: unmount %s: inodes still referenced (forced).",path);
    dcache_invalidate(sb,0,0,0);
    int urc=sb->ops->unmount(sb);
    if (!rc)
        rc=urc;
    block_close(sb->device);
    lf=spin_lock_irqsave(&vfs_lock);
    mounts[slot].in_use=0;
    mounts[slot].sb=0;
    sb->in_use=0;
    spin_unlock_irqrestore(&vfs_lock,lf);
    kmutex_unlock(&vfs_mount_lock);
    klog("ZEROOS: vfs: unmounted %s (%s, result %d).",path,sb->device->name,rc);
    return rc;
}

int vfs_unmount_all(void) {
    int first=0;
    for (uint32_t i=0; i<VFS_MAX_MOUNTS; ++i) {
        if (!mounts[i].in_use)
            continue;
        char path[34];
        (void)ksnprintf(path,sizeof(path),"/%s",mounts[i].name);
        int rc=vfs_unmount(path,1);
        if (rc && !first)
            first=rc;
    }
    return first;
}

void vfs_metrics_snapshot(struct vfs_metrics *out) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    *out=metrics;
    spin_unlock_irqrestore(&vfs_lock,flags);
}

/* ------------------------------------------------------ descriptor tables */

struct vfs_fdtable *vfs_fdtable_lookup(uint64_t pid, uint32_t generation, int create) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    struct vfs_fdtable *free_slot=0;
    for (uint32_t i=0; i<VFS_MAX_FDTABLES; ++i) {
        struct vfs_fdtable *t=&fdtables[i];
        if (t->state==1 && t->pid==pid && t->generation==generation) {
            spin_unlock_irqrestore(&vfs_lock,flags);
            return t;
        }
        if (t->state==0 && !free_slot)
            free_slot=t;
    }
    if (!create || !free_slot) {
        spin_unlock_irqrestore(&vfs_lock,flags);
        return 0;
    }
    memset(free_slot,0,sizeof(*free_slot));
    free_slot->pid=pid;
    free_slot->generation=generation;
    free_slot->state=1;
    spin_unlock_irqrestore(&vfs_lock,flags);
    return free_slot;
}

int vfs_fd_install(struct vfs_fdtable *table, struct vfs_file *file) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    if (table->state!=1) {
        spin_unlock_irqrestore(&vfs_lock,flags);
        return -SE_BADF;
    }
    for (uint32_t fd=0; fd<VFS_MAX_FDS; ++fd)
        if (!table->fds[fd]) {
            table->fds[fd]=file;
            spin_unlock_irqrestore(&vfs_lock,flags);
            return (int)fd;
        }
    spin_unlock_irqrestore(&vfs_lock,flags);
    return -SE_MFILE;
}

struct vfs_file *vfs_fd_get(struct vfs_fdtable *table, int fd) {
    if (fd<0 || (uint32_t)fd>=VFS_MAX_FDS)
        return 0;
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    struct vfs_file *file=table->state==1 ? table->fds[fd] : 0;
    if (file)
        ++file->refcount;
    spin_unlock_irqrestore(&vfs_lock,flags);
    return file;
}

int vfs_fd_close(struct vfs_fdtable *table, int fd) {
    if (fd<0 || (uint32_t)fd>=VFS_MAX_FDS)
        return -SE_BADF;
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    struct vfs_file *file=table->state==1 ? table->fds[fd] : 0;
    if (file)
        table->fds[fd]=0;
    spin_unlock_irqrestore(&vfs_lock,flags);
    if (!file)
        return -SE_BADF;
    vfs_file_put(file);
    return 0;
}

int vfs_fd_dup(struct vfs_fdtable *table, int fd) {
    struct vfs_file *file=vfs_fd_get(table,fd);
    if (!file)
        return -SE_BADF;
    int rc=vfs_fd_install(table,file);
    if (rc<0)
        vfs_file_put(file);
    return rc;
}

uint32_t vfs_fdtable_count(void) {
    uint32_t count=0;
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    for (uint32_t i=0; i<VFS_MAX_FDTABLES; ++i)
        if (fdtables[i].state)
            ++count;
    spin_unlock_irqrestore(&vfs_lock,flags);
    return count;
}

static struct kcompletion *vfs_worker_event;

void vfs_set_worker_event(struct kcompletion *event) {
    vfs_worker_event=event;
}

/* Process teardown: never blocks (may run under process locks). The
 * table is detached and closed later by the storage worker. */
void vfs_process_exit(uint64_t pid, uint32_t generation) {
    int queued=0;
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    for (uint32_t i=0; i<VFS_MAX_FDTABLES; ++i) {
        struct vfs_fdtable *t=&fdtables[i];
        if (t->state==1 && t->pid==pid && t->generation==generation) {
            t->state=2;
            queued=1;
        }
    }
    spin_unlock_irqrestore(&vfs_lock,flags);
    if (queued && vfs_worker_event)
        kcompletion_signal(vfs_worker_event);
}

void vfs_deferred_work(void) {
    for (uint32_t i=0; i<VFS_MAX_FDTABLES; ++i) {
        struct vfs_fdtable *t=&fdtables[i];
        uint64_t flags=spin_lock_irqsave(&vfs_lock);
        if (t->state!=2) {
            spin_unlock_irqrestore(&vfs_lock,flags);
            continue;
        }
        struct vfs_file *closing[VFS_MAX_FDS];
        for (uint32_t fd=0; fd<VFS_MAX_FDS; ++fd) {
            closing[fd]=t->fds[fd];
            t->fds[fd]=0;
        }
        t->state=0;
        spin_unlock_irqrestore(&vfs_lock,flags);
        for (uint32_t fd=0; fd<VFS_MAX_FDS; ++fd)
            if (closing[fd])
                vfs_file_put(closing[fd]);
    }
}

int vfs_periodic(void) {
    int pending=0;
    if (!kmutex_trylock(&vfs_mount_lock))
        return 1;
    for (uint32_t i=0; i<VFS_MAX_MOUNTS; ++i)
        if (mounts[i].in_use && zjfs_periodic(mounts[i].sb))
            pending=1;
    kmutex_unlock(&vfs_mount_lock);
    return pending;
}

/* mmap accounting: a mapped inode is never evicted or destroyed and its
 * filesystem cannot be unmounted. Callers release mmap before the file. */
void vfs_inode_mmap_get(struct vfs_inode *inode) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    ++inode->mmap_count;
    spin_unlock_irqrestore(&vfs_lock,flags);
    vfs_count(&metrics.mmaps);
}

void vfs_inode_mmap_put(struct vfs_inode *inode) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    if (inode->mmap_count)
        --inode->mmap_count;
    spin_unlock_irqrestore(&vfs_lock,flags);
}

int vfs_sb_check_writable(struct vfs_superblock *sb) {
    return vfs_sb_writable(sb);
}

void vfs_kick_worker(void) {
    if (vfs_worker_event)
        kcompletion_signal(vfs_worker_event);
}
