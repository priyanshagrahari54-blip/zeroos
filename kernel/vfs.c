#include "vfs.h"
#include "timer.h"
#include "memory.h"

static struct spinlock vfs_lock;
static struct zeroos_vfs_superblock superblocks[ZEROOS_VFS_MAX_MOUNTS];
static struct zeroos_vfs_inode inodes[ZEROOS_VFS_MAX_INODES];
static struct zeroos_vfs_file files[ZEROOS_VFS_MAX_FILES];
static uint64_t next_super_id;
static uint64_t next_inode_id;
static uint64_t next_file_id;

static int path_validate(const char *path) {
    if (!path || path[0]!='/') return -1;
    uint64_t len=0;
    while (path[len] && len<ZEROOS_VFS_MAX_PATH) len++;
    if (len==0 || len>=ZEROOS_VFS_MAX_PATH) return -1;
    /* No //, no null bytes, canonical */
    for (uint64_t i=0;i<len-1;++i) if (path[i]=='/' && path[i+1]=='/') return -1;
    return 0;
}

int vfs_system_init(void) {
    spinlock_init(&vfs_lock);
    next_super_id=1;
    next_inode_id=1;
    next_file_id=1;
    for (uint32_t i=0;i<ZEROOS_VFS_MAX_MOUNTS;++i) {
        superblocks[i].used=0;
        superblocks[i].generation=0;
        superblocks[i].state=ZEROOS_VFS_MOUNT_STOPPED;
        spinlock_init(&superblocks[i].lock);
    }
    for (uint32_t i=0;i<ZEROOS_VFS_MAX_INODES;++i) {
        inodes[i].used=0;
        inodes[i].generation=0;
        inodes[i].state=ZEROOS_VFS_INODE_UNUSED;
        inodes[i].refcount=0;
        spinlock_init(&inodes[i].lock);
        wait_queue_init(&inodes[i].waiters);
    }
    for (uint32_t i=0;i<ZEROOS_VFS_MAX_FILES;++i) {
        files[i].used=0;
        files[i].generation=0;
        files[i].refcount=0;
        spinlock_init(&files[i].lock);
        wait_queue_init(&files[i].waiters);
    }
    return 0;
}

static struct zeroos_vfs_superblock *super_lookup_locked(uint64_t id) {
    uint32_t slot=(uint32_t)(id & 0xffffULL);
    uint32_t gen=(uint32_t)(id>>16);
    if (slot>=ZEROOS_VFS_MAX_MOUNTS || gen==0) return 0;
    struct zeroos_vfs_superblock *sb=&superblocks[slot];
    if (!sb->used || sb->generation!=gen) return 0;
    return sb;
}

static struct zeroos_vfs_inode *inode_lookup_locked(uint64_t id) {
    uint32_t slot=(uint32_t)(id & 0xffffULL);
    uint32_t gen=(uint32_t)(id>>16);
    if (slot>=ZEROOS_VFS_MAX_INODES || gen==0) return 0;
    struct zeroos_vfs_inode *in=&inodes[slot];
    if (!in->used || in->generation!=gen) return 0;
    return in;
}

static struct zeroos_vfs_file *file_lookup_locked(uint64_t id) {
    uint32_t slot=(uint32_t)(id & 0xffffULL);
    uint32_t gen=(uint32_t)(id>>16);
    if (slot>=ZEROOS_VFS_MAX_FILES || gen==0) return 0;
    struct zeroos_vfs_file *f=&files[slot];
    if (!f->used || f->generation!=gen) return 0;
    return f;
}

int vfs_mount(uint64_t block_device_id, const char *fs_type, const char *mount_point,
              uint64_t *superblock_id_out) {
    if (!fs_type || !mount_point || !superblock_id_out) return -1;
    if (path_validate(mount_point)!=0) return -1;
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    for (uint32_t i=0;i<ZEROOS_VFS_MAX_MOUNTS;++i) {
        if (superblocks[i].used) continue;
        if (superblocks[i].generation==0xffffffffU) continue;
        superblocks[i].generation++;
        if (superblocks[i].generation==0) continue;
        superblocks[i].used=1;
        superblocks[i].state=ZEROOS_VFS_MOUNT_WARM;
        superblocks[i].block_device_id=block_device_id;
        superblocks[i].block_size=4096;
        superblocks[i].total_blocks=1024*1024;
        superblocks[i].free_blocks=1024*1024;
        superblocks[i].total_inodes=ZEROOS_VFS_MAX_INODES;
        superblocks[i].free_inodes=ZEROOS_VFS_MAX_INODES;
        superblocks[i].mount_count=1;
        superblocks[i].last_mount_ticks=timer_ticks();
        uint32_t n=0;
        while (n<31 && fs_type[n]) { superblocks[i].fs_type[n]=fs_type[n]; n++; }
        superblocks[i].fs_type[n]=0;
        superblocks[i].id = ((uint64_t)superblocks[i].generation<<16) | (uint64_t)(i+1);
        *superblock_id_out=superblocks[i].id;
        spin_unlock_irqrestore(&vfs_lock,flags);
        return 0;
    }
    spin_unlock_irqrestore(&vfs_lock,flags);
    return -1;
}

int vfs_unmount(uint64_t superblock_id) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    struct zeroos_vfs_superblock *sb=super_lookup_locked(superblock_id);
    if (!sb) { spin_unlock_irqrestore(&vfs_lock,flags); return -1; }
    uint64_t sflags=spin_lock_irqsave(&sb->lock);
    if (sb->state!=ZEROOS_VFS_MOUNT_WARM && sb->state!=ZEROOS_VFS_MOUNT_ACTIVE) {
        spin_unlock_irqrestore(&sb->lock,sflags);
        spin_unlock_irqrestore(&vfs_lock,flags);
        return -1;
    }
    sb->state=ZEROOS_VFS_MOUNT_STOPPED;
    sb->used=0;
    spin_unlock_irqrestore(&sb->lock,sflags);
    spin_unlock_irqrestore(&vfs_lock,flags);
    return 0;
}

int vfs_open(const char *path, uint32_t flags, uint32_t mode, uint64_t *file_id_out) {
    if (!path || !file_id_out) return -1;
    if (path_validate(path)!=0) return -1;
    if ((flags & ZEROOS_VFS_O_RDWR)==0 && (flags & ZEROOS_VFS_O_RDONLY)==0 && (flags & ZEROOS_VFS_O_WRONLY)==0) return -1;
    uint64_t vflags=spin_lock_irqsave(&vfs_lock);
    /* Find free file slot */
    for (uint32_t i=0;i<ZEROOS_VFS_MAX_FILES;++i) {
        if (files[i].used) continue;
        if (files[i].generation==0xffffffffU) continue;
        files[i].generation++;
        if (files[i].generation==0) continue;
        files[i].used=1;
        files[i].flags=flags;
        files[i].offset=0;
        files[i].refcount=1;
        /* For now, allocate a dummy inode for regular file */
        uint64_t inode_id=0;
        for (uint32_t j=0;j<ZEROOS_VFS_MAX_INODES;++j) {
            if (inodes[j].used) continue;
            if (inodes[j].generation==0xffffffffU) continue;
            inodes[j].generation++;
            if (inodes[j].generation==0) continue;
            inodes[j].used=1;
            inodes[j].state=ZEROOS_VFS_INODE_ACTIVE;
            inodes[j].type=ZEROOS_VFS_FILE_REGULAR;
            inodes[j].size=0;
            inodes[j].blocks=0;
            inodes[j].mode=mode;
            inodes[j].refcount=1;
            inodes[j].id = ((uint64_t)inodes[j].generation<<16) | (uint64_t)(j+1);
            inode_id=inodes[j].id;
            break;
        }
        if (!inode_id) { files[i].used=0; spin_unlock_irqrestore(&vfs_lock,vflags); return -1; }
        files[i].inode_id=inode_id;
        files[i].id = ((uint64_t)files[i].generation<<16) | (uint64_t)(i+1);
        *file_id_out=files[i].id;
        spin_unlock_irqrestore(&vfs_lock,vflags);
        return 0;
    }
    spin_unlock_irqrestore(&vfs_lock,vflags);
    return -1;
}

int vfs_close(uint64_t file_id) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    struct zeroos_vfs_file *f=file_lookup_locked(file_id);
    if (!f) { spin_unlock_irqrestore(&vfs_lock,flags); return -1; }
    uint64_t fflags=spin_lock_irqsave(&f->lock);
    if (f->refcount) f->refcount--;
    if (f->refcount==0) {
        f->used=0;
        /* Also release inode ref */
        struct zeroos_vfs_inode *in=inode_lookup_locked(f->inode_id);
        if (in) {
            uint64_t iflags=spin_lock_irqsave(&in->lock);
            if (in->refcount) in->refcount--;
            if (in->refcount==0 && in->state==ZEROOS_VFS_INODE_DELETED) {
                in->used=0;
                in->state=ZEROOS_VFS_INODE_UNUSED;
            }
            spin_unlock_irqrestore(&in->lock,iflags);
        }
    }
    spin_unlock_irqrestore(&f->lock,fflags);
    spin_unlock_irqrestore(&vfs_lock,flags);
    return 0;
}

int vfs_read(uint64_t file_id, void *buffer, uint64_t capacity, uint64_t *bytes_read_out) {
    if (!buffer || !bytes_read_out || capacity==0) return -1;
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    struct zeroos_vfs_file *f=file_lookup_locked(file_id);
    if (!f) { spin_unlock_irqrestore(&vfs_lock,flags); return -1; }
    /* For now, return EOF for regular file with size 0 */
    *bytes_read_out=0;
    spin_unlock_irqrestore(&vfs_lock,flags);
    return 0;
}

int vfs_write(uint64_t file_id, const void *buffer, uint64_t length, uint64_t *bytes_written_out) {
    if (!buffer || !bytes_written_out || length==0) return -1;
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    struct zeroos_vfs_file *f=file_lookup_locked(file_id);
    if (!f) { spin_unlock_irqrestore(&vfs_lock,flags); return -1; }
    struct zeroos_vfs_inode *in=inode_lookup_locked(f->inode_id);
    if (!in) { spin_unlock_irqrestore(&vfs_lock,flags); return -1; }
    /* Bounded write, update size */
    uint64_t ilock=spin_lock_irqsave(&in->lock);
    in->size += length;
    in->state=ZEROOS_VFS_INODE_DIRTY;
    spin_unlock_irqrestore(&in->lock,ilock);
    *bytes_written_out=length;
    spin_unlock_irqrestore(&vfs_lock,flags);
    return 0;
}

int vfs_pread(uint64_t file_id, void *buffer, uint64_t capacity, uint64_t offset, uint64_t *bytes_read_out) {
    (void)offset;
    return vfs_read(file_id,buffer,capacity,bytes_read_out);
}

int vfs_pwrite(uint64_t file_id, const void *buffer, uint64_t length, uint64_t offset, uint64_t *bytes_written_out) {
    (void)offset;
    return vfs_write(file_id,buffer,length,bytes_written_out);
}

int vfs_seek(uint64_t file_id, int64_t offset, enum zeroos_vfs_seek_whence whence, uint64_t *new_offset_out) {
    if (!new_offset_out) return -1;
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    struct zeroos_vfs_file *f=file_lookup_locked(file_id);
    if (!f) { spin_unlock_irqrestore(&vfs_lock,flags); return -1; }
    uint64_t fflags=spin_lock_irqsave(&f->lock);
    uint64_t new_off=0;
    if (whence==ZEROOS_VFS_SEEK_SET) {
        if (offset<0) { spin_unlock_irqrestore(&f->lock,fflags); spin_unlock_irqrestore(&vfs_lock,flags); return -1; }
        new_off=(uint64_t)offset;
    } else if (whence==ZEROOS_VFS_SEEK_CUR) {
        if (offset<0 && (uint64_t)(-offset)>f->offset) { spin_unlock_irqrestore(&f->lock,fflags); spin_unlock_irqrestore(&vfs_lock,flags); return -1; }
        new_off = offset>=0 ? f->offset+(uint64_t)offset : f->offset-(uint64_t)(-offset);
    } else if (whence==ZEROOS_VFS_SEEK_END) {
        struct zeroos_vfs_inode *in=inode_lookup_locked(f->inode_id);
        if (!in) { spin_unlock_irqrestore(&f->lock,fflags); spin_unlock_irqrestore(&vfs_lock,flags); return -1; }
        uint64_t ilock=spin_lock_irqsave(&in->lock);
        uint64_t size=in->size;
        spin_unlock_irqrestore(&in->lock,ilock);
        if (offset<0 && (uint64_t)(-offset)>size) { spin_unlock_irqrestore(&f->lock,fflags); spin_unlock_irqrestore(&vfs_lock,flags); return -1; }
        new_off = offset>=0 ? size+(uint64_t)offset : size-(uint64_t)(-offset);
    } else {
        spin_unlock_irqrestore(&f->lock,fflags);
        spin_unlock_irqrestore(&vfs_lock,flags);
        return -1;
    }
    f->offset=new_off;
    *new_offset_out=new_off;
    spin_unlock_irqrestore(&f->lock,fflags);
    spin_unlock_irqrestore(&vfs_lock,flags);
    return 0;
}

int vfs_stat(const char *path, struct zeroos_vfs_stat *stat_out) {
    if (!path || !stat_out) return -1;
    if (path_validate(path)!=0) return -1;
    /* For now, return dummy stat */
    stat_out->inode_id=1;
    stat_out->type=ZEROOS_VFS_FILE_REGULAR;
    stat_out->size=0;
    stat_out->blocks=0;
    stat_out->mode=0644;
    return 0;
}

int vfs_fstat(uint64_t file_id, struct zeroos_vfs_stat *stat_out) {
    if (!stat_out) return -1;
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    struct zeroos_vfs_file *f=file_lookup_locked(file_id);
    if (!f) { spin_unlock_irqrestore(&vfs_lock,flags); return -1; }
    struct zeroos_vfs_inode *in=inode_lookup_locked(f->inode_id);
    if (!in) { spin_unlock_irqrestore(&vfs_lock,flags); return -1; }
    uint64_t ilock=spin_lock_irqsave(&in->lock);
    stat_out->inode_id=in->id;
    stat_out->type=in->type;
    stat_out->size=in->size;
    stat_out->blocks=in->blocks;
    stat_out->mode=in->mode;
    stat_out->uid=in->uid;
    stat_out->gid=in->gid;
    stat_out->atime=in->atime;
    stat_out->mtime=in->mtime;
    stat_out->ctime=in->ctime;
    spin_unlock_irqrestore(&in->lock,ilock);
    spin_unlock_irqrestore(&vfs_lock,flags);
    return 0;
}

int vfs_readdir(uint64_t file_id, struct zeroos_vfs_dentry *entry_out, uint64_t *index_inout) {
    if (!entry_out || !index_inout) return -1;
    (void)file_id;
    /* Dummy: no entries */
    return -1;
}

int vfs_create(const char *path, uint32_t mode) {
    if (!path) return -1;
    if (path_validate(path)!=0) return -1;
    (void)mode;
    return 0;
}

int vfs_delete(const char *path) {
    if (!path) return -1;
    if (path_validate(path)!=0) return -1;
    return 0;
}

int vfs_rename(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) return -1;
    if (path_validate(old_path)!=0 || path_validate(new_path)!=0) return -1;
    return 0;
}

int vfs_fsync(uint64_t file_id) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    struct zeroos_vfs_file *f=file_lookup_locked(file_id);
    if (!f) { spin_unlock_irqrestore(&vfs_lock,flags); return -1; }
    struct zeroos_vfs_inode *in=inode_lookup_locked(f->inode_id);
    if (!in) { spin_unlock_irqrestore(&vfs_lock,flags); return -1; }
    uint64_t ilock=spin_lock_irqsave(&in->lock);
    in->dirty=0;
    in->state=ZEROOS_VFS_INODE_ACTIVE;
    spin_unlock_irqrestore(&in->lock,ilock);
    spin_unlock_irqrestore(&vfs_lock,flags);
    return 0;
}

int vfs_debug_validate(void) {
    uint64_t flags=spin_lock_irqsave(&vfs_lock);
    for (uint32_t i=0;i<ZEROOS_VFS_MAX_MOUNTS;++i) {
        if (!superblocks[i].used) continue;
        if (superblocks[i].block_size==0 || superblocks[i].total_blocks==0) {
            spin_unlock_irqrestore(&vfs_lock,flags);
            return -1;
        }
    }
    spin_unlock_irqrestore(&vfs_lock,flags);
    return 0;
}
