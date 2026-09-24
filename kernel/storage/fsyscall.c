/*
 * File/VFS system calls (Stage 3, additive to syscall ABI v1).
 *
 * Every user pointer goes through syscall_copy_{from,to}_user (validated
 * against the calling process). Data transfers use a one-page kernel
 * bounce buffer and are capped at ZEROOS_FILE_MAX_TRANSFER per call
 * (short counts are returned, never partial silent loss). Paths are
 * copied once (TOCTOU-safe) with a hard ZEROOS_PATH_MAX bound.
 *
 * mmap: shared file mappings of at most ZEROOS_MMAP_MAX_PAGES pages,
 * ZEROOS_MMAP_MAX_REGIONS per process, eagerly mapped from the page cache
 * (the user PTEs point at the page-cache frames, so read()/write() and
 * the mapping are coherent). Mapped pages are referenced and pinned:
 * they cannot be evicted, and truncating below a mapped page fails with
 * EBUSY. Stores through a writable mapping are made dirty at
 * msync/munmap/process exit (no hardware dirty-bit scanning — PARTIAL,
 * documented); msync additionally performs fsync(data).
 */
#include "fsyscall.h"
#include "pagecache.h"
#include "vfs.h"
#include "zjfs.h"
#include "../interrupts.h"
#include "../kstring.h"
#include "../ksync.h"
#include "../memory.h"
#include "../process.h"
#include "../syscall.h"
#include "../user.h"
#include "../vmm.h"

_Static_assert(sizeof(struct zeroos_stat)==sizeof(struct vfs_stat), "stat ABI");
_Static_assert(sizeof(struct zeroos_statfs)==sizeof(struct vfs_statfs), "statfs ABI");
_Static_assert(sizeof(struct zeroos_dirent)==sizeof(struct vfs_dirent), "dirent ABI");
_Static_assert(ZEROOS_PATH_MAX==VFS_PATH_MAX && ZEROOS_MAX_FDS==VFS_MAX_FDS, "limits");
_Static_assert(ZEROOS_O_CREAT==VFS_O_CREAT && ZEROOS_O_TRUNC==VFS_O_TRUNC &&
               ZEROOS_O_APPEND==VFS_O_APPEND && ZEROOS_O_EXCL==VFS_O_EXCL &&
               ZEROOS_O_DIRECTORY==VFS_O_DIRECTORY && ZEROOS_O_RDWR==VFS_O_RDWR,
               "open flags");

#define FS_MMAP_WINDOW (ZEROOS_USER_BASE + 0x40000000ULL)
#define FS_MMAP_SLOT_SPAN 0x100000ULL           /* 1 MiB per region slot */
#define FS_MMAP_TOTAL 32U                        /* system-wide regions */

enum { REGION_FREE=0, REGION_LIVE=1, REGION_DEAD=2, REGION_BUSY=3 };

struct fs_region {
    uint32_t state;
    uint32_t slot;
    uint64_t pid;
    uint32_t generation;
    uint32_t writable;
    uint64_t address;
    uint64_t pages;
    struct vfs_file *file;
    struct pc_page *page[ZEROOS_MMAP_MAX_PAGES];
    uint64_t block[ZEROOS_MMAP_MAX_PAGES];
};

static struct fs_region regions[FS_MMAP_TOTAL];
static struct spinlock region_lock;     /* zero-initialised = unlocked */

static struct vfs_cred fs_cred(const struct process *process) {
    struct vfs_cred cred={process->uid,process->gid};
    return cred;
}

/* Copies a NUL-terminated path; never reads across into an unmapped page
 * past the terminator. */
static int fs_copy_path(uint64_t user, char *out) {
    if (!user)
        return -SE_FAULT;
    uint64_t done=0;
    while (done<ZEROOS_PATH_MAX) {
        uint64_t address=user+done;
        uint64_t to_page=4096U-(address&4095U);
        uint64_t chunk=ZEROOS_PATH_MAX-done;
        if (chunk>to_page)
            chunk=to_page;
        if (chunk>64U)
            chunk=64U;
        if (syscall_copy_from_user(out+done,address,chunk)!=0)
            return -SE_FAULT;
        for (uint64_t i=0; i<chunk; ++i)
            if (out[done+i]==0)
                return done+i==0 ? -SE_NOENT : 0;
        done+=chunk;
    }
    return -SE_NAMETOOLONG;
}

static struct vfs_fdtable *fs_table(struct process *process, int create) {
    return vfs_fdtable_lookup(process->pid,process->generation,create);
}

static int fs_file(struct process *process, uint64_t fd, struct vfs_file **out) {
    struct vfs_fdtable *table=fs_table(process,0);
    if (!table || fd>=ZEROOS_MAX_FDS)
        return -SE_BADF;
    *out=vfs_fd_get(table,(int)fd);
    return *out ? 0 : -SE_BADF;
}

/* ------------------------------------------------------------ data I/O */

static int64_t fs_transfer(struct vfs_file *file, uint64_t user, uint64_t length,
                           int writing, int positional, uint64_t offset) {
    if (length>ZEROOS_FILE_MAX_TRANSFER)
        length=ZEROOS_FILE_MAX_TRANSFER;
    if (length==0)
        return 0;
    uint8_t *bounce=(uint8_t *)page_alloc();
    if (!bounce)
        return -SE_NOMEM;
    uint64_t done=0;
    int64_t rc=0;
    while (done<length) {
        uint64_t chunk=length-done<4096U ? length-done : 4096U;
        if (writing) {
            if (syscall_copy_from_user(bounce,user+done,chunk)!=0) {
                rc=-SE_FAULT;
                break;
            }
            rc=positional ? vfs_pwrite(file,bounce,chunk,offset+done) :
                            vfs_write(file,bounce,chunk);
        } else {
            rc=positional ? vfs_pread(file,bounce,chunk,offset+done) :
                            vfs_read(file,bounce,chunk);
            if (rc>0 && syscall_copy_to_user(user+done,bounce,(uint64_t)rc)!=0)
                rc=-SE_FAULT;
        }
        if (rc<=0)
            break;
        done+=(uint64_t)rc;
        if ((uint64_t)rc<chunk)
            break;                                  /* EOF / short write */
    }
    page_free(bounce);
    return done ? (int64_t)done : rc;
}

/* ---------------------------------------------------------------- mmap */

static void fs_region_release(struct fs_region *r) {
    struct vfs_inode *inode=r->file->inode;
    for (uint64_t i=0; i<r->pages; ++i) {
        if (!r->page[i])
            continue;
        if (r->writable)
            pc_mark_dirty(r->page[i],r->block[i]);
        pc_unpin(r->page[i]);
        pc_put(r->page[i]);
        r->page[i]=0;
    }
    vfs_inode_mmap_put(inode);
    vfs_file_put(r->file);
    r->file=0;
}

static int fs_mmap(struct process *process, uint64_t fd, uint64_t offset,
                   uint64_t length, uint64_t prot, uint64_t *address_out) {
    if ((offset&4095U) || length==0 || prot&~(uint64_t)ZEROOS_MMAP_PROT_WRITE)
        return -SE_INVAL;
    uint64_t pages=(length+4095U)/4096U;
    if (pages>ZEROOS_MMAP_MAX_PAGES)
        return -SE_INVAL;
    struct vfs_file *file;
    int rc=fs_file(process,fd,&file);
    if (rc)
        return rc;
    struct vfs_inode *inode=file->inode;
    int writable=(prot&ZEROOS_MMAP_PROT_WRITE)!=0;
    uint32_t access=file->flags&VFS_O_ACCMODE;
    if (!VFS_S_ISREG(inode->mode))
        rc=-SE_NODEV;
    else if (access==VFS_O_WRONLY || (writable && access==VFS_O_RDONLY))
        rc=-SE_ACCES;
    else if (writable)
        rc=vfs_sb_check_writable(inode->sb);
    if (rc) {
        vfs_file_put(file);
        return rc;
    }
    /* Claim a region + per-process slot. */
    uint64_t flags=spin_lock_irqsave(&region_lock);
    uint32_t used_slots=0, count=0;
    struct fs_region *r=0;
    for (uint32_t i=0; i<FS_MMAP_TOTAL; ++i) {
        if (regions[i].state!=REGION_FREE && regions[i].pid==process->pid &&
            regions[i].generation==process->generation) {
            used_slots|=1U<<regions[i].slot;
            ++count;
        } else if (regions[i].state==REGION_FREE && !r) {
            r=&regions[i];
        }
    }
    if (count>=ZEROOS_MMAP_MAX_REGIONS || !r) {
        spin_unlock_irqrestore(&region_lock,flags);
        vfs_file_put(file);
        return -SE_NOMEM;
    }
    uint32_t slot=0;
    while (used_slots&(1U<<slot))
        ++slot;
    r->state=REGION_BUSY;
    r->slot=slot;
    r->pid=process->pid;
    r->generation=process->generation;
    spin_unlock_irqrestore(&region_lock,flags);
    r->writable=(uint32_t)writable;
    r->address=FS_MMAP_WINDOW+slot*FS_MMAP_SLOT_SPAN;
    r->pages=0;
    r->file=file;
    vfs_inode_mmap_get(inode);
    /* Populate: map existing blocks (allocate for writable mappings so
     * msync has somewhere to write). Mapping past EOF is refused. */
    kmutex_lock(&inode->lock);
    if (offset+pages*4096U>((inode->size+4095U)&~4095ULL))
        rc=-SE_NXIO;
    for (uint64_t i=0; rc==0 && i<pages; ++i) {
        uint64_t index=offset/4096U+i;
        uint64_t block;
        int is_new;
        rc=inode->sb->ops->map_page(inode,index,writable,&block,&is_new);
        struct pc_page *page=0;
        if (rc==0)
            rc=pc_get(&inode->mapping,index,block,1,BLOCK_PRIO_FOREGROUND,&page);
        if (rc==0) {
            pc_pin(page);
            r->page[i]=page;
            r->block[i]=block;
            r->pages=i+1U;
            if (is_new)
                pc_mark_dirty(page,block);
        }
    }
    if (rc==0 && writable)
        (void)inode->sb->ops->update_inode(inode);
    kmutex_unlock(&inode->lock);
    for (uint64_t i=0; rc==0 && i<pages; ++i) {
        uint64_t pte=VMM_USER|VMM_NO_EXECUTE|(writable ? VMM_WRITABLE : 0ULL);
        if (process_address_space_map_page(process,r->address+i*4096U,
                                           r->page[i]->physical,pte)!=0) {
            for (uint64_t j=0; j<i; ++j)
                (void)process_address_space_unmap_page(process,r->address+j*4096U);
            rc=-SE_NOMEM;
        }
    }
    if (rc) {
        r->writable=0;                  /* nothing was stored through it */
        fs_region_release(r);
        flags=spin_lock_irqsave(&region_lock);
        r->state=REGION_FREE;
        spin_unlock_irqrestore(&region_lock,flags);
        return rc;
    }
    flags=spin_lock_irqsave(&region_lock);
    r->state=REGION_LIVE;
    spin_unlock_irqrestore(&region_lock,flags);
    *address_out=r->address;
    return 0;
}

static struct fs_region *fs_region_claim(struct process *process, uint64_t address) {
    uint64_t flags=spin_lock_irqsave(&region_lock);
    for (uint32_t i=0; i<FS_MMAP_TOTAL; ++i) {
        struct fs_region *r=&regions[i];
        if (r->state==REGION_LIVE && r->pid==process->pid &&
            r->generation==process->generation && r->address==address) {
            r->state=REGION_BUSY;
            spin_unlock_irqrestore(&region_lock,flags);
            return r;
        }
    }
    spin_unlock_irqrestore(&region_lock,flags);
    return 0;
}

static void fs_region_set(struct fs_region *r, uint32_t state) {
    uint64_t flags=spin_lock_irqsave(&region_lock);
    r->state=state;
    spin_unlock_irqrestore(&region_lock,flags);
}

static int fs_munmap(struct process *process, uint64_t address) {
    struct fs_region *r=fs_region_claim(process,address);
    if (!r)
        return -SE_INVAL;
    for (uint64_t i=0; i<r->pages; ++i)
        (void)process_address_space_unmap_page(process,r->address+i*4096U);
    fs_region_release(r);
    fs_region_set(r,REGION_FREE);
    return 0;
}

static int fs_msync(struct process *process, uint64_t address) {
    struct fs_region *r=fs_region_claim(process,address);
    if (!r)
        return -SE_INVAL;
    int rc=0;
    if (r->writable) {
        for (uint64_t i=0; i<r->pages; ++i)
            pc_mark_dirty(r->page[i],r->block[i]);
        rc=vfs_fsync(r->file,1);
    }
    fs_region_set(r,REGION_LIVE);
    return rc;
}

void storage_process_exit(uint64_t pid, uint32_t generation) {
    int dead=0;
    uint64_t flags=spin_lock_irqsave(&region_lock);
    for (uint32_t i=0; i<FS_MMAP_TOTAL; ++i)
        if (regions[i].state==REGION_LIVE && regions[i].pid==pid &&
            regions[i].generation==generation) {
            regions[i].state=REGION_DEAD;
            dead=1;
        }
    spin_unlock_irqrestore(&region_lock,flags);
    vfs_process_exit(pid,generation);          /* wakes worker if fds queued */
    if (dead)
        vfs_kick_worker();
}

void fsyscall_deferred_work(void) {
    for (uint32_t i=0; i<FS_MMAP_TOTAL; ++i) {
        uint64_t flags=spin_lock_irqsave(&region_lock);
        int dead=regions[i].state==REGION_DEAD;
        if (dead)
            regions[i].state=REGION_BUSY;
        spin_unlock_irqrestore(&region_lock,flags);
        if (!dead)
            continue;
        /* The address space was destroyed by process_reap; the PTE
         * references to the frames are already gone. */
        fs_region_release(&regions[i]);
        fs_region_set(&regions[i],REGION_FREE);
    }
}

/* ------------------------------------------------------------ dispatch */

void fsyscall_dispatch(struct interrupt_frame *frame, struct process *process) {
    char path[ZEROOS_PATH_MAX];
    char path2[ZEROOS_PATH_MAX];
    struct vfs_cred cred=fs_cred(process);
    struct vfs_file *file=0;
    int64_t rc;

    switch (frame->rax) {
    case ZEROOS_SYS_OPEN: {
        rc=fs_copy_path(frame->rdi,path);
        if (rc)
            break;
        if (frame->rsi&~(uint64_t)VFS_O_VALID) {
            rc=-SE_INVAL;
            break;
        }
        struct vfs_fdtable *table=fs_table(process,1);
        if (!table) {
            rc=-SE_NFILE;
            break;
        }
        rc=vfs_open(&cred,path,(uint32_t)frame->rsi,(uint32_t)frame->rdx&07777U,&file);
        if (rc==0) {
            rc=vfs_fd_install(table,file);
            if (rc<0)
                vfs_file_put(file);
        }
        file=0;
        break;
    }
    case ZEROOS_SYS_CLOSE: {
        struct vfs_fdtable *table=fs_table(process,0);
        rc=table && frame->rdi<ZEROOS_MAX_FDS ? vfs_fd_close(table,(int)frame->rdi) : -SE_BADF;
        break;
    }
    case ZEROOS_SYS_DUP: {
        struct vfs_fdtable *table=fs_table(process,0);
        rc=table && frame->rdi<ZEROOS_MAX_FDS ? vfs_fd_dup(table,(int)frame->rdi) : -SE_BADF;
        break;
    }
    case ZEROOS_SYS_READ:
    case ZEROOS_SYS_FILE_WRITE:
    case ZEROOS_SYS_PREAD:
    case ZEROOS_SYS_PWRITE: {
        rc=fs_file(process,frame->rdi,&file);
        if (rc)
            break;
        int writing=frame->rax==ZEROOS_SYS_FILE_WRITE || frame->rax==ZEROOS_SYS_PWRITE;
        int positional=frame->rax==ZEROOS_SYS_PREAD || frame->rax==ZEROOS_SYS_PWRITE;
        if (positional && (int64_t)frame->r10<0)
            rc=-SE_INVAL;
        else
            rc=fs_transfer(file,frame->rsi,frame->rdx,writing,positional,frame->r10);
        break;
    }
    case ZEROOS_SYS_SEEK:
        rc=fs_file(process,frame->rdi,&file);
        if (rc==0)
            rc=vfs_seek(file,(int64_t)frame->rsi,(int)frame->rdx);
        break;
    case ZEROOS_SYS_FSTAT:
    case ZEROOS_SYS_STAT: {
        struct vfs_stat st;
        if (frame->rax==ZEROOS_SYS_FSTAT) {
            rc=fs_file(process,frame->rdi,&file);
            if (rc==0)
                rc=vfs_fstat(file,&st);
        } else {
            rc=fs_copy_path(frame->rdi,path);
            if (rc==0)
                rc=vfs_stat(&cred,path,&st);
        }
        if (rc==0 && syscall_copy_to_user(frame->rsi,&st,sizeof(st))!=0)
            rc=-SE_FAULT;
        break;
    }
    case ZEROOS_SYS_READDIR: {
        struct vfs_dirent *d=(struct vfs_dirent *)page_alloc();
        if (!d) {
            rc=-SE_NOMEM;
            break;
        }
        rc=fs_file(process,frame->rdi,&file);
        if (rc==0)
            rc=vfs_readdir(file,d);
        if (rc==1 && syscall_copy_to_user(frame->rsi,d,sizeof(*d))!=0)
            rc=-SE_FAULT;
        page_free(d);
        break;
    }
    case ZEROOS_SYS_MKDIR:
    case ZEROOS_SYS_UNLINK:
    case ZEROOS_SYS_RMDIR:
    case ZEROOS_SYS_CHMOD:
    case ZEROOS_SYS_CHOWN:
        rc=fs_copy_path(frame->rdi,path);
        if (rc)
            break;
        if (frame->rax==ZEROOS_SYS_MKDIR)
            rc=vfs_mkdir(&cred,path,(uint32_t)frame->rsi&07777U);
        else if (frame->rax==ZEROOS_SYS_UNLINK)
            rc=vfs_unlink(&cred,path);
        else if (frame->rax==ZEROOS_SYS_RMDIR)
            rc=vfs_rmdir(&cred,path);
        else if (frame->rax==ZEROOS_SYS_CHMOD)
            rc=vfs_chmod(&cred,path,(uint32_t)frame->rsi&07777U);
        else
            rc=vfs_chown(&cred,path,(uint32_t)frame->rsi,(uint32_t)frame->rdx);
        break;
    case ZEROOS_SYS_RENAME:
    case ZEROOS_SYS_LINK:
        rc=fs_copy_path(frame->rdi,path);
        if (rc==0)
            rc=fs_copy_path(frame->rsi,path2);
        if (rc==0)
            rc=frame->rax==ZEROOS_SYS_RENAME ? vfs_rename(&cred,path,path2) :
                                               vfs_link(&cred,path,path2);
        break;
    case ZEROOS_SYS_FSYNC:
        rc=fs_file(process,frame->rdi,&file);
        if (rc==0)
            rc=frame->rsi&~(uint64_t)ZEROOS_FSYNC_DATAONLY ? -SE_INVAL :
               vfs_fsync(file,(frame->rsi&ZEROOS_FSYNC_DATAONLY)!=0);
        break;
    case ZEROOS_SYS_FTRUNCATE:
        rc=fs_file(process,frame->rdi,&file);
        if (rc==0)
            rc=vfs_ftruncate(file,frame->rsi);
        break;
    case ZEROOS_SYS_STATFS: {
        struct vfs_statfs sf;
        rc=fs_copy_path(frame->rdi,path);
        if (rc==0)
            rc=vfs_statfs(path,&sf);
        if (rc==0 && syscall_copy_to_user(frame->rsi,&sf,sizeof(sf))!=0)
            rc=-SE_FAULT;
        break;
    }
    case ZEROOS_SYS_GETCRED:
        rc=(int64_t)((uint64_t)process->uid|((uint64_t)process->gid<<32));
        break;
    case ZEROOS_SYS_SETCRED:
        if (process->uid!=0)
            rc=-SE_PERM;                    /* only root may change identity */
        else if (frame->rdi>0xffffffffULL || frame->rsi>0xffffffffULL)
            rc=-SE_INVAL;
        else {
            process->uid=(uint32_t)frame->rdi;
            process->gid=(uint32_t)frame->rsi;
            rc=0;
        }
        break;
    case ZEROOS_SYS_MMAP: {
        uint64_t address=0;
        rc=fs_mmap(process,frame->rdi,frame->rsi,frame->rdx,frame->r10,&address);
        if (rc==0)
            rc=(int64_t)address;
        break;
    }
    case ZEROOS_SYS_MUNMAP:
        rc=fs_munmap(process,frame->rdi);
        break;
    case ZEROOS_SYS_MSYNC:
        rc=fs_msync(process,frame->rdi);
        break;
    default:
        rc=-SE_NOSYS;
        break;
    }
    if (file)
        vfs_file_put(file);
    frame->rax=rc<0 ? 0ULL-(uint64_t)(-rc) : (uint64_t)rc;
}
